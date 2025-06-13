/*
 *	Amstrad NC100
 *
 *	CMOS Z80 at 6MHz
 *	64K Internal RAM
 *	256K Internal ROM
 *	PCMCIA slot (type I only)
 *	TC8521AP/AM RTC
 *	µPD71051 UART (8251A)
 *	µPD4711A
 *
 *	Should also handle the NC150
 *	512K internal ROM
 *	(no emulation of the ranger serial floppy)
 *
 *	The current keyboard encoding is UK. Support for other mappings
 *	can be added if someone needs them.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include <SDL2/SDL.h>

#include "event.h"
#include "keymatrix.h"

#include "libz80/z80.h"
#include "z80dis.h"

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[480 * 64];

struct keymatrix *matrix;

static uint8_t ram[131072];
static uint8_t rom[524288];
static uint8_t rom_mask = 0x0F;
static uint8_t ram_mask = 0x03;
static uint8_t *pcmcia;			/* Battery backed usually */
static uint32_t pcmcia_size;

static Z80Context cpu_z80;
static uint8_t irqmask = 0x00;
static uint8_t irqstat = 0x0F;		/* Active low */
#define IRQ_TICK		0x08	/* 10ms tick */
#define IRQ_ACK			0x04	/* Printer ACK */
#define IRQ_TXR			0x02	/* UART TX ready */
#define IRQ_RXR			0x01	/* UART RX ready */
static uint8_t vidbase;		/* Top 4 bits of video fetch */
static uint8_t baudmisc = 0xFF;
static uint8_t pplatch;
static uint8_t bankr[4];
static uint8_t sound[4];
/* These are mostly inverted signals */
#define CSTAT_PRESENT		0x80
#define CSTAT_WP		0x40
#define CSTAT_5V		0x20
#define CSTAT_CBLOW		0x10
#define CSTAT_BLOW		0x08
#define CSTAT_LBLOW		0x04
#define CSTAT_LPBUSY		0x02
#define CSTAT_LPACK		0x01
static uint8_t cardstat = CSTAT_PRESENT | CSTAT_5V;

static uint8_t fast;
volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_BANK	0x000010
#define TRACE_KEY	0x000020

static int trace = 0;

static uint8_t *mmu(uint16_t addr, bool write)
{
	uint32_t pa;
	uint16_t bank = bankr[addr >> 14];

	addr &= 0x3FFF;

	switch(bank & 0xC0) {
	case 0x00:
		if (write)
			return NULL;
		return &rom[((bank & rom_mask) << 14) + addr];
	case 0x40:
		return &ram[((bank & ram_mask) << 14) + addr];
	case 0x80:
		if (pcmcia == NULL)
			return NULL;
		/* The NC100 firmware never seems to even glance at attribute
		   space - but protect from scribbles just in case */
		if (!(baudmisc & 0x80))
			/* Pretend to be a type 1 card with no CIS */
			return NULL;
		pa = ((bank & 0x3F) << 14) + addr;
		if (pa >= pcmcia_size)
			return NULL;
		return pcmcia + pa;
	case 0xC0:
		return NULL;
	}
	return NULL;
}

uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t *p = mmu(addr, false);
	if (p == NULL) {
		fprintf(stderr, "%04X not readable\n", addr);
		return 0xFF;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "%04X -> %02X\n", addr, *p);
	return *p;
}

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t *p = mmu(addr, true);

	if (p) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X <- %02X\n", addr, val);
		*p = val;
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X ROM (write %02X fail)\n", addr, val);
	}
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t *p = mmu(addr, 0);
	if (p == NULL) {
		fprintf(stderr, "??");
		return 0xFF;
	}
	fprintf(stderr, "%02X ", *p);
	nbytes++;
	return *p;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	uint8_t *p = mmu(addr, 0);
	if (p == NULL)
		return 0xFF;
	return *p;
}

static void nc100_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z80.R1.br.A, cpu_z80.R1.br.F,
		cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL,
		cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}

int check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;

	FD_ZERO(&i);
	FD_SET(0, &i);
	FD_ZERO(&o);
	FD_SET(1, &o);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(2, &i, &o, NULL, &tv) == -1) {
		if (errno == EINTR)
			return 0;
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

unsigned int next_char(void)
{
	char c;
	if (read(0, &c, 1) != 1) {
		printf("(tty read without ready byte)\n");
		return 0xFF;
	}
	if (c == 0x0A)
		c = '\r';
	return c;
}

/*
 *	Interrupt controller: not very clear how this is actually meant to
 *	work.
 *
 *	irqstat is 4 bits that clear when the IRQ source is present, writing
 *	0 back sets the bits allowing another event to occur.
 *
 *	irqmask is a 4 bit user writable mask. Set bits permit the interrupt
 *	to be a source.
 *
 *	There are four source bits
 *	3	key scan		cleared at source by reading B9
 *	2	printer ack
 *	1	tx ready from uart	}	cleared on uart
 *	0	rx ready from uart	}
 *
 */

static void raise_irq(uint8_t n)
{
	/* If this interrupt is not masked on the controller then
	   raise it */
	irqstat &= ~n;
}

/*
 *	Keyboard mapping
 */

static SDL_Keycode keyboard[] = {
	SDLK_LSHIFT, SDLK_RSHIFT, 0, SDLK_LEFT, SDLK_RETURN, 0, 0, 0,
	SDLK_LALT, SDLK_LCTRL, SDLK_ESCAPE, SDLK_SPACE, 0, 0, SDLK_5, 0,
	SDLK_CAPSLOCK, SDLK_RALT, SDLK_1, SDLK_TAB, 0, 0, 0, 0,
	SDLK_3, SDLK_2, SDLK_q, SDLK_w, SDLK_e, 0, SDLK_s, SDLK_d,
	SDLK_4, 0, SDLK_z, SDLK_x, SDLK_a, 0, SDLK_r, SDLK_f,
	0, 0, SDLK_b, SDLK_v, SDLK_t, SDLK_y, SDLK_g, SDLK_c,
	SDLK_6, SDLK_DOWN, SDLK_DELETE, SDLK_RIGHT, SDLK_HASH, SDLK_SLASH, SDLK_h, SDLK_n,
	SDLK_EQUALS, SDLK_7, SDLK_BACKSLASH, SDLK_UP, 0, SDLK_u, SDLK_m, SDLK_k,
	SDLK_8, SDLK_MINUS, SDLK_RIGHTBRACKET, SDLK_LEFTBRACKET,
				SDLK_AT, SDLK_i, SDLK_j, SDLK_COMMA,
	SDLK_0, SDLK_9, SDLK_BACKSPACE, SDLK_p,
				SDLK_COLON, SDLK_l, SDLK_o, SDLK_PERIOD
};

/*
 *	Very hacky RTC (borrowed from nc100em). Needs fixing up
 *
 *	The 8521 has 4 pages (0-3)
 *	Page 0: Time
 *	Page 1: Alarm
 *	Page 2: RAM (13 x 4bit)
 *	Page 3: RAM (13 x 4bit)
 */

static uint8_t rtc_page;
static uint8_t rtc_ram[26];
static struct tm *rtc_tm;

static uint8_t tc8521_read(uint8_t addr)
{
	uint8_t page;

	addr &= 0x0F;
	page = rtc_page & 0x03;
	/* Read pack the page register we set (all banks) */
	if (addr == 0x0D)
		return 0xF0 | rtc_page;
	/* Write only test and reset */
	if (addr == 0x0E || addr == 0x0F)
		return 0xF0;
	/* Ok read is valid - deal with the page selected */
	/* Pages 2 and 3 are the NVRAM */
	if (page == 2)
		return rtc_ram[addr];
	if (page == 3)
		return rtc_ram[addr + 13];
	/* Page 0 is the time */
	if (page == 0) {
		if (rtc_tm == NULL)
			return 0xF0;
		switch(addr) {
		case 0x00:
			return (rtc_tm->tm_sec % 10) | 0xF0;
		case 0x01:
			return (rtc_tm->tm_sec / 10) | 0xF0;
		case 0x02:
			return (rtc_tm->tm_min % 10) | 0xF0;
		case 0x03:
			return (rtc_tm->tm_min / 10) | 0xF0;
		case 0x04:
			return (rtc_tm->tm_hour % 10) | 0xF0;
		case 0x05:
			return (rtc_tm->tm_hour / 10) | 0xF0;
		case 0x06:
			return rtc_tm->tm_wday | 0xF0;
		case 0x07:
			return (rtc_tm->tm_mday % 10) | 0xF0;
		case 0x08:
			return (rtc_tm->tm_mday / 10) | 0xF0;
		case 0x09:
			return ((rtc_tm->tm_mon + 1) % 10) | 0xF0;
		case 0x0A:
			return ((rtc_tm->tm_mon + 1) / 10) | 0xF0;
		case 0x0B:
			return ((rtc_tm->tm_year - 90) % 10) | 0xF0;
		case 0x0C:
			return ((rtc_tm->tm_year - 90) / 10) | 0xF0;
		}
	}
	/* Ok page 1: Alarm and control (fake it) */
	switch(addr) {
	case 0x0A:
		return 0xF1;		/* Hack for 24hr mode */
	case 0x0B:
		return (rtc_tm->tm_year & 3) | 0xF0;	/* Leap hack */
	default:
		return 0xF0;
	}
}

static void tc8521_write(uint8_t addr, uint8_t val)
{
	time_t t;
	addr &= 0x0F;
	switch(addr) {
	case 0x0D:/* Page : bit 3 is timer enable 2 alarm enable - we ignore */
		rtc_page = val & 3;
		time(&t);
		rtc_tm = localtime(&t);
		break;
	case 0x0E:		/* Test */
	case 0x0F:		/* Reset */
		break;
	default:
		if (rtc_page == 2)
			rtc_ram[addr] = val | 0xF0;
		else if(rtc_page == 3)
			rtc_ram[addr + 13] = val | 0xF0;
		/* Ignore the other bits for now */
		break;
	}
}

/*
 *	8251 UART
 *
 *	Need to emulate, for now just return some suitable dummy bits
 */

static uint8_t i8251_read(uint8_t addr)
{
	addr &= 1;
	if (addr == 1)
		return 0x01;
	return 0x00;
}

static void i8251_write(uint8_t addr, uint8_t val)
{
	/* No op for now */
}

/*
 *	Keyboard scanning is handled by the matrix keyboard module
 */
static uint8_t keymatrix(uint8_t addr)
{
	addr &= 0x0F;
	if (addr == 9)
		irqstat |= IRQ_TICK;
	if (addr <= 9)
		return keymatrix_input(matrix, 1 << addr);
	return 0xFF;
}

/*
 *	Miscellaneous controls.
 */
static void do_misc(uint8_t val)
{
	uint8_t delta = val ^ baudmisc;
	/* Printer strobe going low - fake an ACK interrupt */
	if ((delta & 0x40) && !(val & 0x40)) {
		raise_irq(IRQ_ACK);
		/* We would print the byte in pplatch */
		// lpt_output(pplatch);
	}
	baudmisc = val;
}

static void dump_banks(void)
{
	unsigned int i;
	fprintf(stderr, "** Banking now ");
	for (i = 0; i < 4; i++) {
		switch(bankr[i] & 0xC0) {
		case 0x00:
			fprintf(stderr, "ROM%02X, ", bankr[i] & 0x0F);
			if (bankr[i] & 0x30)
				fprintf(stderr, "[BAD bits %02X]", bankr[i]);
			break;
		case 0x40:
			fprintf(stderr, "RAM%02X, ", bankr[i] & 0x03);
			if (bankr[i] & 0x3C)
				fprintf(stderr, "[BAD bits %02X]", bankr[i]);
			break;
		case 0x80:
			fprintf(stderr, "PCM%02X, ", bankr[i] & 0x3F);
			break;
		case 0xC0:
			fprintf(stderr, "INVALID, ");
			break;
		}
	}
	fprintf(stderr, "\n");
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t dev = addr & 0xF0;
	if (trace & TRACE_IO)
		fprintf(stderr, "=== OUT %02X, %02X\n", addr & 0xFF, val);
	switch(dev) {
	case 0x00:	/* Display control (W)*/
		vidbase = val & 0xF0;
		break;
	case 0x10:	/* Memory management (RW) */
		bankr[addr & 3] = val;
		if (trace & TRACE_BANK)
			dump_banks();
		return;
	case 0x20:	/* Card control (W) */
		/* Not emulated - adds a wait state to the PCMCIA if bit 7 */
		return;
	case 0x30:	/* Baud generator (W) and misc */
		/* 7 set 0 to to read PCMCIA attribute space */
		/* 6 strobes parallel */
		/* 4 low turns on the line driver */
		/* 3 low turns on the UART clock */
		/* 2-0 control the bit rate 000=150/300/600/1200/2400/4800/9600/19200 */
		do_misc(val);
		break;
	case 0x40:	/* Parallel port (W) */
		pplatch = val;
		return;
	case 0x50:	/* Speaker (W) */
		sound[addr & 3] = val;
		return;
	case 0x60:	/* IRQ glue (W) */
		irqmask = val;
		return;
	case 0x70:	/* Power control */
		if ((val & 1) == 0)
			emulator_done = 1;
		return;
	case 0x80:	/* Unused */
		break;
	case 0x90:	/* IRQ status (RW) */
		irqstat |= ~val;	/* Re-enable if bit set */
		return;
	case 0xA0:	/* Card status (R) */
		break;
	case 0xB0:	/* Keyboard matrix (R) */
		break;
	case 0xC0:	/* uPD71051 (RW) */
		i8251_write(addr, val);
		return;
	case 0xD0:	/* TC8521 (RW) */
		tc8521_write(addr, val);
		return;
	case 0xE0:	/* Unused */
		break;
	case 0xF0:	/* Unused */
		break;
	}
}

static uint8_t do_io_read(int unused, uint16_t addr)
{
	uint8_t dev = addr & 0xF0;
	switch(dev) {
	case 0x00:	/* Display control (W)*/
		break;
	case 0x10:	/* Memory management (RW) */
		return bankr[addr & 3];
	case 0x20:	/* Card control (W) */
		break;
	case 0x30:	/* Baud generator (W) */
		break;
	case 0x40:	/* Parallel port (W) */
		break;
	case 0x50:	/* Speaker (W) */
		break;
	case 0x60:	/* IRQ glue (W) */
		break;
	case 0x70:	/* Power control */
		break;
	case 0x80:	/* Unused */
		break;
	case 0x90:	/* IRQ status (RW) */
		return irqstat;
	case 0xA0:	/* Card status (R) */
		return cardstat;
	case 0xB0:	/* Keyboard matrix (R) */
		/* Read they keys - B9 also resets the IRQ */
		return keymatrix(addr);
	case 0xC0:	/* uPD71051 (RW) */
		return i8251_read(addr);
	case 0xD0:	/* TC8521 (RW) */
		return tc8521_read(addr);
	case 0xE0:	/* Unused */
		break;
	case 0xF0:	/* Unused */
		break;
	}
	return 0xFF;
}

uint8_t io_read(int unused, uint16_t addr)
{
	uint8_t r = do_io_read(unused, addr);
	if (trace & TRACE_IO)
		fprintf(stderr, "=== IN %02X = %02X\n", addr & 0xFF, r);
	return r;
}

/* We maybe shouldn't do it all every frame but who cares 8) */
static void nc100_rasterize(void)
{
	uint8_t *vscan = ram + (vidbase << 8);
	uint32_t *tp = texturebits;
	unsigned int y, x, b;
	uint8_t bits;
	for (y = 0; y < 64 ; y++) {
		for (x = 0; x < 60; x++) {
			bits = *vscan++;
			for (b = 0; b < 8; b++) {
				if (bits & 0x80)
					*tp++ = 0xFF333333;
				else
					*tp++ = 0xFFCCCCBB;
				bits <<= 1;
			}
		}
		vscan += 4;	/* 4 unused bytes per line */
	}
}

static void nc100_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = 480;
	rect.h = 64;

	SDL_UpdateTexture(texture, NULL, texturebits, 480 * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	emulator_done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "nc100: [-f] [-p pcmcia] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rom_path = "nc100.rom";
	char *pcmcia_path = NULL;
	int romsize;

	while ((opt = getopt(argc, argv, "p:r:d:f")) != -1) {
		switch (opt) {
		case 'p':
			pcmcia_path = optarg;
			break;
		case 'r':
			rom_path = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	fd = open(rom_path, O_RDONLY);
	if (fd == -1) {
		perror(rom_path);
		exit(EXIT_FAILURE);
	}
	romsize = read(fd, rom, 524288);
	if (romsize == 524288) {
		/* NC150 style ROM so 512K ROM 128K RAM */
		rom_mask = 0x1F;
		ram_mask = 0x07;
	} else if (romsize != 262144) {
		fprintf(stderr, "nc100: invalid ROM size '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	close(fd);
	fd = open("nc100.ram", O_RDONLY);
	if (fd != -1) {
		read(fd, ram, 65536);
		read(fd, rtc_ram, 26);
		close(fd);
	}
	if (pcmcia_path) {
		off_t size;
		fd = open(pcmcia_path, O_RDWR);
		if (fd == -1) {
			perror(pcmcia_path);
			exit(EXIT_FAILURE);
		}
		size = lseek(fd, 0, SEEK_END);
		if (size == -1) {
			fprintf(stderr, "nc100: unable to get size of '%s'.\n", pcmcia_path);
			exit(EXIT_FAILURE);
		}
		pcmcia = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED,
			fd, 0);
		if (pcmcia == MAP_FAILED) {
			fprintf(stderr, "nc100: unable to map PCMCIA card image '%s'.\n",
				pcmcia_path);
			exit(EXIT_FAILURE);
		}
		cardstat &= ~CSTAT_PRESENT;
		pcmcia_size = size;
		fprintf(stderr, "nc100: mapped %dKB PCMCIA image.\n", pcmcia_size);
	}

	ui_init();

	matrix = keymatrix_create(10, 8, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_add_events(matrix);

	window = SDL_CreateWindow("NC100",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			480, 64,
			SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "nc100: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "nc100: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		480, 64);
	if (texture == NULL) {
		fprintf(stderr, "nc100: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, 480, 64);

	/* 10ms - it's a balance between nice behaviour and simulation
	   smoothness */

	tc.tv_sec = 0;
	tc.tv_nsec = 10000000L;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON | ECHO);
		term.c_cc[VMIN] = 0;
		term.c_cc[VTIME] = 1;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VSTOP] = 0;
		tcsetattr(0, TCSADRAIN, &term);
	}

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;
	cpu_z80.trace = nc100_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int i;
		for (i = 0; i < 100; i++) {
			Z80ExecuteTStates(&cpu_z80, 600);
		}

		/* We want to run UI events before we rasterize */
		if (ui_event())
			Z80NMI(&cpu_z80);

		nc100_rasterize();
		nc100_render();
		raise_irq(IRQ_TICK);
		if ((~irqstat & irqmask) & 0x0F) {
			Z80INT(&cpu_z80, 0xFF);
		}
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	fd = open("nc100.ram", O_RDWR|O_CREAT, 0600);
	if (fd != -1) {
		write(fd, ram, 65536);
		write(fd, rtc_ram, 26);
		close(fd);
	}
	exit(0);
}
