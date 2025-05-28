/*
 *	Poly88. Basic configuration to begin with.
 *
 *	8080 at 1.8432MHz
 *	512 bytes of RAM
 *	3K onboad EPROM, 1K used for system monitor.
 *
 *	As with a lot of early systems the design didn't quite fit
 *	CP/M when it arrived so mods and reworks were done. In the case
 *	of the Poly88 this mostly consisted of moving the video to F800
 *	(as became default anyway in the 4.0 monitor), and also some
 *	small changes to allow the paging out of the internal memory
 *	by accessing free sides of internal I/O ports).
 *
 *	The "official" Poly88 CP/M had an 8K RAM at moved from E000-FFFF
 *	to 0000-1FFF when the internal devices paged out, and there was even
 *	a rare "twin" two user system where you could switch between two
 *	video/memory banks between 2000-DFFF and the keyboard I/O switched
 *	with it.
 *
 *	Currently however we emulate the more normal setup with RAM mapped
 *	in the low 62K.
 *
 *	TODO:
 *	- 8251 interrupt emulation
 *	- Tape load as serial dev plugin
 *	- Render the video screen properly
 *	- Keyboard
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "intel_8080_emulator.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "i8251.h"
#include "ide.h"
#include "asciikbd.h"
#include "nasfont.h"	/* Near enough correct as makes no difference */

static uint8_t rom[3072];
static uint8_t highrom[4096];		/* High ROM off the I/O bus */
static uint8_t iram[512];
static unsigned rom_size;
/* The real font is 7 x 9 but we expand it all into words for
   convenience and to deal with the 10x15 block graphics */
static uint16_t font[4096];
static uint8_t ram[65536];		/* External RAM */
static uint16_t ramsize = 63488;	/* Up to the video and ROM space */

static unsigned int fast;
static volatile int done;
static unsigned emulator_done;

static struct ide_controller *ide;
static struct i8251 *uart;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	4
#define TRACE_SERIAL	8
#define TRACE_IRQ	16
#define TRACE_CPU	32

static int trace = 0;

static unsigned live_irq;

static uint8_t idle_bus = 0xFF;
static uint16_t iobase = 0x0000;
static uint16_t vidbase = 0xF800; /* F800 and 8800 commonly used */
static uint8_t brg_latch;
static uint8_t sstep;		/* TODO : need to make ifetches visible
				   from emulation */
static unsigned rtc_int;
static uint8_t vram[1024];	/* 512byte or 1K video RAM */

/* 7 x 9 for char matrix in a 10 x 15 field */
#define CWIDTH 10
#define CHEIGHT 15

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[64 * CWIDTH * 16 * CHEIGHT];
static struct asciikbd *kbd;

/* Simple setup to begin with */
static uint8_t *mem_map(uint16_t addr, bool wr)
{
	if (!(brg_latch & 0x20)) {
		if ((addr & 0xF000) == iobase) {
			addr &= 0x0FFF;
			if (addr < 0x0C00) {
				if (!wr) {
					if (addr >= rom_size)
						return &idle_bus;
					return rom + addr;
				}
				return NULL;
			}
			return iram + (addr & 0x01FF);
		}
	}
	/* TODO: base system brg latch doesn't affect video. Later setups
	   it is paged with internal I/O, twin system two videos are paged
	   this way ! */
	if ((addr & 0xFC00) == vidbase)
		return vram + (addr & 0x03FF);	/* Assume 1K fitted */
	/* Upper ROM from an I/O card */		
	if ((addr & 0xFC00) == 0xFC00) {
		if (wr)
			return NULL;
		return highrom + (addr & 0x3FF);
	}
	/* External memory goes here */
	if (addr < ramsize)	/* PHANTOM is off */
		return ram + addr;
	if (wr)
		return NULL;
	return &idle_bus;
}
	
uint8_t i8080_read(uint16_t addr)
{
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	r = *mem_map(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);
	return r;
}

uint8_t i8080_debug_read(uint16_t addr)
{
	return *mem_map(addr, 0);
}

void i8080_write(uint16_t addr, uint8_t val)
{
	uint8_t *p;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	p = mem_map(addr,  1);
	if (p)
		*p = val;
}

/* For now just hand back RST 7 */
uint8_t i8080_get_vector(void)
{
	/* No single step yet - RST 7 */
	if (rtc_int)
		return 0xF7;	/* RST 6 */
	if (asciikbd_ready(kbd))
		return 0xEF;	/* RST 5 */
	/* TODO i8251 interrupt support to do RST 4 */
	return 0xFF;		/* Beats me */
}

/* TODO: vectors and vector support on 8080.c */
void recalc_interrupts(void)
{
	/* No single step yet. TODO add 8251 int support */
	live_irq = asciikbd_ready(kbd) | rtc_int;
	if (live_irq)
		i8080_set_int(INT_IRQ);
	else
		i8080_clear_int(INT_IRQ);
}

static void poll_irq_event(void)
{
	recalc_interrupts();
}

/* TODO: recalc ints if we touch the 8251 */
static uint8_t onboard_in(uint8_t addr)
{
	if (addr < 4)
		return i8251_read(uart, addr & 1);
	/* TODO side effects on the other ports */
	return 0xFF;
}

static void onboard_out(uint8_t addr, uint8_t val)
{
	if (addr < 4)
		i8251_write(uart, addr & 1, val);
	else if (addr < 8)
		brg_latch = val;
	else if (addr < 0x0C) {
		rtc_int = 0;
		recalc_interrupts();
	} else
		sstep = 2;
}

/* The video I/O ports are used for the parallel keyboard */
static uint8_t video_in(uint8_t addr)
{
	if (addr & 1)
		return 0xFF;	/* TODO: decode option on some boards */
	asciikbd_ack(kbd);
	recalc_interrupts();
	return asciikbd_read(kbd);
}

static void video_out(uint8_t addr, uint8_t val)
{
}

uint8_t i8080_inport(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr & 0xF0) == (iobase >> 8))
		return onboard_in(addr);
	if ((addr & 0xFC) == (vidbase >> 8))
		return video_in(addr);
	if (ide && addr >= 0x30 && addr <= 0x37)
		return ide_read8(ide, addr);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void i8080_outport(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr & 0xF0) == (iobase >> 8))
		onboard_out(addr, val);
	else if ((addr & 0xFC) == (vidbase >> 8))
		video_out(addr, val);
	else if (ide && addr >= 0x30 && addr <= 0x37)
		ide_write8(ide, addr, val);
	else if (addr == 0xFD)
		trace = val;
	else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
	poll_irq_event();
}

static void raster_char(unsigned int y, unsigned int x, uint8_t c)
{
	uint16_t *fp = &font[16 * c];
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + 64 * CWIDTH * y * CHEIGHT;
	for (rows = 0; rows < CHEIGHT; rows++) {
		uint16_t bits = *fp++;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x8000)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 63 */
		pixp += 63 * CWIDTH;
	}
}
		
static void poly_rasterize(void)
{
	unsigned int lines, cols;
	uint8_t *ptr = vram;

	for (lines = 0; lines < 16; lines ++) {
		for (cols = 0; cols < 64; cols ++)
			raster_char(lines, cols, *ptr++);
	}
}

static void poly_render(void)
{
	SDL_Rect rect;
	
	rect.x = rect.y = 0;
	rect.w = 64 * CWIDTH;
	rect.h = 16 * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, 64 * CWIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

static void make_graphics(void)
{
	/* The lower font is really made by logic and is 10x15 pixels
	   in a 2 x 3 grid */
	unsigned c;
	uint16_t v;
	uint16_t *p = font;
	uint8_t *n = nascom_font_raw;

	for (c = 0; c < 127; c++) {
		switch(c & 0x24) {
		case 0x00:
			v = 0xFFC;
			break;
		case 0x04:
			v = 0xF80;
			break;
		case 0x20:
			v = 0x07C;
			break;
		case 0x24:
			v = 0x000;
			break;
		}
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		switch(c & 0x12) {
		case 0x00:
			v = 0xFFC;
			break;
		case 0x02:
			v = 0xF80;
			break;
		case 0x10:
			v = 0x07C;
			break;
		case 0x12:
			v = 0x000;
			break;
		}
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		switch(c & 0x09) {
		case 0x00:
			v = 0xFFC;
			break;
		case 0x01:
			v = 0xF80;
			break;
		case 0x08:
			v = 0x07C;
			break;
		case 0x09:
			v = 0x000;
			break;
		}
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		p++;
	}
	p = font + 128 * 16;
	for (c = 0; c < 128 * 16; c++)
		*p++ = (uint16_t)(*n++) << 8;
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "poly88: [-f] [-r path] [-m mem Kb] [-d debug] [-i ide]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "poly88.rom";
	char *idepath = NULL;

	while ((opt = getopt(argc, argv, "d:F:fi:m:r:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'i':
			idepath = optarg;
			break;
		case 'm':
			ramsize = 1024 * atol(optarg);
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	rom_size = read(fd, rom, 3072);
	if (rom_size < 1024) {
		fprintf(stderr, "poly88: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	/* TODO so for now make it look like bus idle */
	memset(highrom, 0xFF, sizeof(highrom));

	/* Build the render font from the logic tables and also the
	   MCM667x */
	make_graphics();

	if (idepath) {
		ide = ide_allocate("cf0");
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		}
		ide_attach(ide, 0, fd);
	}
		
	atexit(SDL_Quit);
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "poly88: unable to initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}
	window = SDL_CreateWindow("Poly88",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			64 * CWIDTH, 16 * CHEIGHT,
			SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "poly88: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "poly88: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		64 * CWIDTH, 16 * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr, "poly88: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, 64 * CWIDTH,  16 * CHEIGHT);

	/* 20ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;

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

	uart = i8251_create();
	if (trace & TRACE_SERIAL)
		i8251_trace(uart, 1);
	i8251_attach(uart, &console);

	kbd = asciikbd_create();

	i8080_reset();
	if (trace & TRACE_CPU) {
		i8080_log = stderr;
	}

	/* We run 1843200 t-states per second */
	while (!emulator_done) {
		int i;
		for (i = 0; i < 144; i++) {
			i8080_exec(256);
			i8251_timer(uart);
			poll_irq_event();
			/* We want to run UI events regularly it seems */
			poly_rasterize();
			poly_render();
			asciikbd_event(kbd);
		}
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		rtc_int = 1;
		poll_irq_event();
	}
	exit(0);
}
