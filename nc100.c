/*
 *	Amstrad NC100
 *
 *	CMOS Z80 at 6MHz
 *	64K Internal RAM
 *	256 Internal ROM
 *	PCMCIA slot (type I only)
 *	TC8521AP/AM RTC
 *	µPD71051 UART (8251A)
 *	µPD4711A
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

#include "keymatrix.h"

#include "libz80/z80.h"

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[480 * 64];

struct keymatrix *matrix;

static uint8_t ram[65336];
static uint8_t rom[262144];
static uint8_t *pcmcia;			/* Battery backed usually */
static uint32_t pcmcia_size;

static uint16_t tstate_steps = 369;	/* RC2014 speed */

static Z80Context cpu_z80;
static uint8_t irqmask;
static uint8_t irqstat;
#define IRQ_TICK		0x08	/* 10ms tick */
#define IRQ_ACK			0x04	/* Printer ACK */
#define IRQ_TXR			0x02	/* UART TX ready */
#define IRQ_RXR			0x01	/* UART RX ready */
static uint8_t vidbase;		/* Top 4 bits of video fetch */
static uint8_t baudmisc = 0xFF;
static uint8_t pplatch;
static uint8_t bankr[4];
static uint8_t sound[4];
#define CSTAT_PRESENT		0x80
#define CSTAT_WP		0x40
#define CSTAT_5V		0x20
#define CSTAT_CBLOW		0x10
#define CSTAT_BLOW		0x08
#define CSTAT_LBLOW		0x04
#define CSTAT_LPBUSY		0x02
#define CSTAT_LPACK		0x01
static uint8_t cardstat = CSTAT_LPACK|CSTAT_CBLOW|CSTAT_BLOW;

static uint8_t fast;
volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002

static int trace = 0;

static uint8_t *mmu(uint16_t addr, bool write)
{
	uint32_t pa;
	uint8_t bank = bankr[addr >> 14];
	addr &= 0x3FFF;
	switch(bank & 0xC0) {
	case 0x00:
		if (write)
			return NULL;
		return &rom[((bank & 0x0F) << 14) + addr];
	case 0x40:
		return &ram[((bank & 3) << 14) + addr];
	case 0x80:
		if (pcmcia == NULL)
			return NULL;
		/* TODO: PCMCIA attribute space and WP ? */
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
	uint8_t *p = mmu(addr, 0);
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
	uint8_t *p = mmu(addr, 1);
	if (p) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X <- %02X\n", addr, val);
		*p = val;
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X ROM (write %02X fail)\n", addr, val);
	}
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

	if (select(2, &i, NULL, NULL, &tv) == -1) {
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
 *	Interrupt controller
 */

static void raise_irq(uint8_t n)
{
}

/*
 *	Very hacky RTC (borrowed from nc100em). Needs fixing up
 */

static uint8_t rtc_page;
static uint8_t rtc_ram[24];
static struct tm *rtc_tm;

static uint8_t tc8521_read(uint8_t addr)
{
	addr &= 0x0F;
	if (addr == 0x0D)
		return 0xF0 | rtc_page;
	if (addr == 0x0E || addr == 0x0F)
		return 0xFF;
	/* Ok read is valid - deal with the page selected */
	/* Pages 2 and 3 are the NVRAM */
	if (rtc_page == 2)
		return rtc_ram[addr];
	if (rtc_page == 3)
		return rtc_ram[addr];
	/* Page 0 is the time */
	if (rtc_page == 0) {
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
	/* Ok page 1: control stuff */
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
	case 0x0D:		/* Page */
		rtc_page = val & 3;
		time(&t);
		rtc_tm = localtime(&t);
		break;
	case 0x0E:		/* Test */
	case 0x0F:		/* Reset */
		break;
	default:
		if (rtc_page == 2)
			rtc_ram[addr] = val;
		else if(rtc_page == 3)
			rtc_ram[addr + 12] = val;
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
		return keymatrix_input(matrix, addr & 0x0F);
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

void io_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t dev = addr & 0xF0;
	if (trace & TRACE_IO)
		fprintf(stderr, "OUT %02X, %02X\n", addr, val);
	switch(dev) {
	case 0x00:	/* Display control (W)*/
		vidbase = val & 0xF0;
		break;
	case 0x10:	/* Memory management (RW) */
		bankr[addr & 3] = val;
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
		fprintf(stderr, "IN %02X = %02X\n", addr & 0xFF, r);
	return r;
}

/* We maybe shouldn't do it all every frame but who cares 8) */
static void nc100_rasterize(void)
{
	uint8_t *vscan = ram + (vidbase << 8);
	uint32_t *tp = texturebits;
	unsigned int y, x, b;
	uint8_t bits;
	fprintf(stderr, "vscan begin (ram %p, scan %p)\n", ram, vscan);
	for (y = 0; y < 64 ; y++) {
		for (x = 0; x < 60; x++) {
			bits = *vscan++;
			for (b = 0; b < 8; b++) {
				if (bits & 0x80)
					*tp++ = 0xFFFFFFFF;
				else
					*tp++ = 0xFF000000;
				bits <<= 1;
			}
		}
		vscan += 4;	/* 4 unused bytes per line */
	}
	fprintf(stderr, "vscan end\n");
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

static void ui_event(void)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_QUIT:
			emulator_done = 1;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			keymatrix_SDL2event(matrix, &ev);
			break;
		}
	}
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
	if (read(fd, rom, 262144) < 262144) {
		fprintf(stderr, "nc100: short rom '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	close(fd);
	
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
		cardstat |= CSTAT_PRESENT;
	}

	matrix = keymatrix_create(10, 8, NULL/*FIXME*/);

	atexit(SDL_Quit);
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "nc100: unable to initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}
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

	/* TODO: map PCMCIA */
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

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 50 times then poll the
	   slow stuff and nap for 2.5ms to get 50Hz on the TMS99xx */
	while (!emulator_done) {
		int i;
		/* 36400 T states for base RC2014 - varies for others */
		for (i = 0; i < 200; i++) {
			Z80ExecuteTStates(&cpu_z80, tstate_steps);
			/* TODO */
		}

		/* We want to run UI events before we rasterize */
		ui_event();

		nc100_rasterize();
		nc100_render();

		raise_irq(IRQ_TICK);

		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
