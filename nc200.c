/*
 *	Amstrad NC200
 *
 *	CMOS Z80 at 6MHz
 *	128K Internal RAM
 *	512K Internal ROM
 *	PCMCIA slot (type I only)
 *	TC8521AP/AM RTC
 *	µPD71051 UART (8251A)
 *	µPD4711A
 *
 *	The NC200 is a close relative of the NC100.
 *
 *	The differences on the I/O ports are
 *	0x0x - the display is twice as big so A12 is taken from the video
 *	       scan hardware not this port
 *
 *	0x10:
 *	There is now twice as much RAM so another bit matters for paging RAM
 *
 *	0x30:
 *	Unused bit 5 is now used for the disk interface
 *
 *	0x60: all re-arranged
 *	6 = RTC int
 *	5 = FDC
 *	4 = Power off int (button)
 *	3 = Key scan
 *	2 = serial (RX | TX)
 *	1 = unused
 *	0 = printer ACK
 *
 *	0x70:
 *	is TC in here ??
 *	bit 2: 0 = backlight
 *	bit 1: 0 = disk motor
 *	bit 0: 0 = power on/off
 *
 *	0xA0:
 *	bit 0 is now a battery good indicator (0 = good enough for disk)
 *	bit 1 and 3 are no longer used it seems
 *
 *
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
#include "lib765/include/765.h"
#include "z80dis.h"

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[480 * 128];

static FDC_PTR fdc;
static FDRV_PTR drive;
static char *disk_path[4];

struct keymatrix *matrix;

static uint8_t ram[131072];
static uint8_t rom[524288];
static uint8_t *pcmcia;			/* Battery backed usually */
static uint32_t pcmcia_size;

static Z80Context cpu_z80;
static uint8_t irqmask = 0x00;
static uint8_t irqstat = 0xFF;		/* Active low */
#define IRQ_RTC			0x40	/* RTC alarm ? */
#define IRQ_FDC			0x20	/* 765 */
#define IRQ_POWER_OFF		0x10	/* Power off button */
#define IRQ_TICK		0x08	/* 10ms tick */
#define IRQ_SERIAL		0x04	/* Serial */
#define IRQ_ACK			0x01	/* Printer ACK */
static uint8_t vidbase;		/* Top 3 bits of video fetch */
static uint8_t baudmisc = 0xFF;
static uint8_t pplatch;
static uint8_t bankr[4];
static uint8_t sound[4];
/* Mostly inverted - differ from NC100 */
#define CSTAT_PRESENT		0x80
#define CSTAT_WP		0x40
#define CSTAT_5V		0x20
#define CSTAT_CBLOW		0x10
#define CSTAT_LBLOW		0x04
#define CSTAT_BATGOOD		0x01	/* Low if can drive floppy */
static uint8_t cardstat = CSTAT_PRESENT | CSTAT_CBLOW;
static uint8_t pctrl;
#define PCTRL_BACKLIGHT		0x04

static uint8_t fast;
volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_BANK	0x000010
#define TRACE_KEY	0x000020
#define TRACE_FDC	0x000040

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
		return &rom[((bank & 0x1F) << 14) + addr];
	case 0x40:
		return &ram[((bank & 7) << 14) + addr];
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
		fprintf(stderr, "?? ");
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

static void nc200_trace(unsigned unused)
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
 *	Interrupt controller.
 */

static void raise_irq(uint8_t n)
{
	/* If this interrupt is not masked on the controller then
	   raise it */
	irqstat &= ~n;
}

/*
 *	Keyboard mapping (Similar but not quite the same as the NC100)
 */

static SDL_Keycode keyboard[] = {
	SDLK_LSHIFT, SDLK_RSHIFT, SDLK_4, SDLK_LEFT, SDLK_RETURN, 0, 0, 0,
	SDLK_LALT, SDLK_LCTRL, SDLK_ESCAPE, SDLK_SPACE, 0, 0, 0, SDLK_9,
	SDLK_CAPSLOCK, SDLK_RALT, SDLK_1, SDLK_TAB, SDLK_5, 0, SDLK_6, 0,
	SDLK_3, SDLK_2, SDLK_q, SDLK_w, SDLK_e, 0, SDLK_s, SDLK_d,
	SDLK_8, SDLK_7, SDLK_z, SDLK_x, SDLK_a, 0, SDLK_r, SDLK_f,
	0, 0, SDLK_b, SDLK_v, SDLK_t, SDLK_y, SDLK_g, SDLK_c,
	0, SDLK_DOWN, SDLK_DELETE, SDLK_RIGHT, SDLK_HASH, SDLK_SLASH, SDLK_h, SDLK_n,
	0, SDLK_EQUALS, SDLK_BACKSLASH, SDLK_UP, 0, SDLK_u, SDLK_m, SDLK_k,
	0, SDLK_MINUS, SDLK_RIGHTBRACKET, SDLK_LEFTBRACKET,
				SDLK_AT, SDLK_i, SDLK_j, SDLK_PERIOD,
	0, SDLK_0, SDLK_BACKSPACE, SDLK_p,
				SDLK_COLON, SDLK_l, SDLK_o, SDLK_COMMA
};

/*
 *	The NC200 has a rather conventional PC like RTC
 *	It is interfaced on the usual A0 odd/even register/data scheme
 *
 *	The NC200 uses it in binary mode, so we don't emulate BCD mode
 *	Ditto with 12 hour
 */

static uint8_t rtc_page;
static uint8_t rtc_ram[64];

static uint8_t mc146818_read(uint8_t addr)
{
	time_t t = time(NULL);
	struct tm *rtc_tm = localtime(&t);

	/* Should never occur but don't crash if we are in nonsenseville */
	if (rtc_tm == NULL)
		return 0xFF;

	addr &= 1;
	if (addr == 0)
		return 0x00;		/* Not readable */
	switch(rtc_page) {
		case 0x00:
			return rtc_tm->tm_sec;
		case 0x02:
			return rtc_tm->tm_min;
		case 0x04:
			return rtc_tm->tm_hour;
		case 0x06:
			return rtc_tm->tm_wday;
		case 0x07:
			return rtc_tm->tm_mday;
		case 0x08:
			return rtc_tm->tm_mon + 1;
		case 0x09:
			return rtc_tm->tm_year - 90;
		/* Hacks for control registers */
		case 0x0A:
			return rtc_ram[rtc_page] & 0x7F;
		case 0x0B:
			return 4;
		case 0x0C:
			return 0;
		case 0x0D:
			return 0x80;
		default:
			return rtc_ram[rtc_page];
	}
}

/*
 *	Very minimal write support. We don't actually change or honour
 *	anything!
 */
static void mc146818_write(uint8_t addr, uint8_t val)
{
	addr &= 1;
	if (addr == 0) {
		rtc_page = val & 0x3F;
		return;
	}
	if (rtc_page >= 0x0A || rtc_page == 0x01 || rtc_page == 0x03 || rtc_page == 0x05)
		rtc_ram[rtc_page] = val;
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

/*
 *	Floppy disk interface
 */

static void fdc_log(int debuglevel, char *fmt, va_list ap)
{
	if ((trace & TRACE_FDC) || debuglevel == 0) {
		vfprintf(stderr, fmt, ap);
	}
}

static void fdc_isr(FDC_PTR fdc, int status)
{
	raise_irq(IRQ_FDC);
}

static void fdc_write(uint8_t addr, uint8_t val)
{
	if (addr == 1)
		fdc_write_data(fdc, val);
}

static uint8_t fdc_read(uint8_t addr)
{
	uint8_t r;
	if (addr == 0)
		r = fdc_read_ctrl(fdc);
	else
		r = fdc_read_data(fdc);
	return r;
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

static void nc200_reset(void)
{
	irqmask = 0xFF;
	irqstat = 0xFF;
	bankr[0] = bankr[1] = bankr[2] = bankr[3] = 0;
	vidbase = 0;
	pplatch = 0;
	/* ?? sound reset if we do sound */
	Z80RESET(&cpu_z80);
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t dev = addr & 0xF0;
	if (trace & TRACE_IO)
		fprintf(stderr, "=== OUT %02X, %02X\n", addr & 0xFF, val);
	switch(dev) {
	case 0x00:	/* Display control (W)*/
		vidbase = val & 0xE0;
		break;
	case 0x10:	/* Memory management (RW) */
		bankr[addr & 3] = val;
		if (trace & TRACE_BANK)
			dump_banks();
		return;
	case 0x20:	/* Card control (W). Also FDC control */
		fdc_set_motor(fdc, (val & 4) ? 0 : 1);
		fdc_set_terminal_count(fdc, val & 1);
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
		pctrl = val;
		if ((val & 0x01) == 0) {
			/* We don't quite the program in this case because
			   the system uses self reset a lot to go back to
			   main menu */
			nc200_reset();
		}
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
		mc146818_write(addr, val);
		return;
	case 0xE0:	/* Floppy controller */
		fdc_write(addr & 1, val);
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
		return mc146818_read(addr);
	case 0xE0:	/* Floppy */
		return fdc_read(addr & 1);
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
static void nc200_rasterize(void)
{
	uint8_t *vscan = ram + (vidbase << 8);
	uint32_t *tp = texturebits;
	unsigned int y, x, b;
	uint8_t bits;
	for (y = 0; y < 128 ; y++) {
		for (x = 0; x < 60; x++) {
			bits = *vscan++;
			for (b = 0; b < 8; b++) {
				if (bits & 0x80)
					*tp++ = 0xFF333333;
				else if (!(pctrl & PCTRL_BACKLIGHT))
					*tp++ = 0xFFEEEEDD;
				else
					*tp++ = 0xFFBBBBAA;
				bits <<= 1;
			}
		}
		vscan += 4;	/* 4 unused bytes per line */
	}
}

static void nc200_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = 480;
	rect.h = 128;

	SDL_UpdateTexture(texture, NULL, texturebits, 480 * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

static void swap_disk(int n)
{
	if (disk_path[n]) {
		fprintf(stderr, "Swapping to disk '%s'\n", disk_path[n]);
		fd_eject(drive);
		fdd_setfilename(drive, disk_path[n]);
	}
}

/* Ok so this isn't quite what it was intended for but it works out */
static void keytranslate(SDL_Event *ev)
{
	if (ev->key.keysym.sym == SDLK_F1)
		swap_disk(0);
	if (ev->key.keysym.sym == SDLK_F2)
		swap_disk(1);
	if (ev->key.keysym.sym == SDLK_F3)
		swap_disk(2);
	if (ev->key.keysym.sym == SDLK_F4)
		swap_disk(3);
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
	fprintf(stderr, "nc200: [-f] [-p pcmcia] [-r rompath] [-A diskpath] [-[1234] disk{n}] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rom_path = "nc200.rom";
	char *pcmcia_path = NULL;
	char *fd_path = NULL;

	while ((opt = getopt(argc, argv, "p:r:d:fA:1:2:3:4:")) != -1) {
		switch (opt) {
		case 'A':
			fd_path = optarg;
			break;
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
		case '1':
		case '2':
		case '3':
		case '4':
			disk_path[opt - '1'] = optarg;
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
	if (read(fd, rom, 524288) < 524288) {
		fprintf(stderr, "nc200: short rom '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	close(fd);
	fd = open("nc200.ram", O_RDONLY);
	if (fd != -1) {
		read(fd, ram, 131072);
		read(fd, rtc_ram, 64);
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
			fprintf(stderr, "nc200: unable to get size of '%s'.\n", pcmcia_path);
			exit(EXIT_FAILURE);
		}
		pcmcia = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED,
			fd, 0);
		if (pcmcia == MAP_FAILED) {
			fprintf(stderr, "nc200: unable to map PCMCIA card image '%s'.\n",
				pcmcia_path);
			exit(EXIT_FAILURE);
		}
		cardstat &= ~CSTAT_PRESENT;
		pcmcia_size = size;
		fprintf(stderr, "nc200: mapped %dKB PCMCIA image.\n", pcmcia_size);
	}

	ui_init();
	matrix = keymatrix_create(10, 8, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_translator(matrix, keytranslate);
	keymatrix_add_events(matrix);

	fdc = fdc_new();

	lib765_register_error_function(fdc_log);

	drive = fd_newdsk();
	if (fd_path)
		fdd_setfilename(drive, fd_path);

	fd_settype(drive, FD_35);
	fd_setheads(drive, 2);
	fd_setcyls(drive, 80);

	fdc_reset(fdc);
	fdc_setisr(fdc, fdc_isr);
	fdc_setdrive(fdc, 0, drive);

	window = SDL_CreateWindow("NC200",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			480, 128,
			SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "nc200: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "nc200: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		480, 128);
	if (texture == NULL) {
		fprintf(stderr, "nc200: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, 480, 128);

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
	cpu_z80.trace = nc200_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int i;
		for (i = 0; i < 100; i++) {
			Z80ExecuteTStates(&cpu_z80, 600);
			fdc_tick(fdc);
		}

		/* We want to run UI events before we rasterize */
		if (ui_event())
			emulator_done = 1;
		nc200_rasterize();
		nc200_render();
		raise_irq(IRQ_TICK);
		if ((~irqstat & irqmask) & 0x0F)
			Z80INT(&cpu_z80, 0xFF);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	fd = open("nc200.ram", O_RDWR|O_CREAT, 0600);
	if (fd != -1) {
		write(fd, ram, 131072);
		write(fd, rtc_ram, 64);
		close(fd);
	}
	fd_eject(drive);
	fdc_destroy(&fdc);
	fd_destroy(&drive);
	exit(0);
}
