/*
 *	Microtan 65
 *	6502 CPU at 750KHz (or 1.5MHz with mods)
 *	1K of RAM at 0000-03FF
 *	1K of ROM at FC00-FFFF
 *	32x16 video using 0200-03FF
 *	Uppercase - lowercase option covering 00-1F/60-7F
 *
 *	Keyboard or matrix
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
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "6502.h"
#include "asciikbd.h"

#define CWIDTH 8
#define CHEIGHT 16

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[32 * CWIDTH * 16 * CHEIGHT];

static uint8_t mem[65536];
static uint8_t vgfx[512];	/* Extra bit for video */
static uint8_t font[8192];	/* Chars in 8x16 (repeated twice) then gfx */

static struct asciikbd *kbd;

static uint8_t fast;
volatile int emulator_done;

static uint8_t mem_be;
static unsigned gfx_bit;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008

static int trace = 0;

uint8_t do_read_6502(uint16_t addr, unsigned debug)
{
	if (addr <= 0x8000)
		return mem[addr & 0x03FF];
	if (addr >= 0xC000)
		return mem[0xFC00 + (addr & 0x03FF)];
	/* I/O space */
	switch(addr & 0x0F) {
	case 0x00:
		gfx_bit = 1;
		break;
	case 0x03:
		return (asciikbd_read(kbd) & 0x7F) | (asciikbd_ready(kbd) ? 0x80 : 0x00);
	}
	return 0xFF;
}

uint8_t read6502(uint16_t addr)
{
	uint8_t r = do_read_6502(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "%04X -> %02X\n", addr, r);
	return r;
}

uint8_t read6502_debug(uint16_t addr)
{
	return do_read_6502(addr, 1);
}

void write6502(uint16_t addr, uint8_t val)
{
	if (addr <= 0x8000) {
		mem[addr & 0x03FF] = val;
		/* TODO: should wrap likewise */
		if (addr >= 0x200 && addr <= 0x03FF)
			vgfx[addr & 0x01FF] = gfx_bit;
	} else if (addr >= 0xC000) {
		if (addr >= 0xFFF0)
			mem_be = val;
		return;
	} else switch(addr & 0x0F) {
	case 0x00:
		asciikbd_ack(kbd);
		break;
	case 0x01:
		/* Delayed NMI - not implemented yet */
		break;
	case 0x02:
		/* Set kbd pattern */
	case 0x03:
		gfx_bit = 0;
		break;
	}
}

/*
 *	32x16 video
 */

static void raster_char(unsigned int y, unsigned int x, uint8_t c, uint8_t gfx)
{
	uint8_t *fp = &font[CHEIGHT * (c + (gfx ? 0x100: 0))];
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + 32 * CWIDTH * y * CHEIGHT;
	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp++;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x80)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 31 */
		pixp += 31 * CWIDTH;
	}
}
		
static void utan_rasterize(void)
{
	unsigned int lines, cols;
	uint8_t *ptr = mem + 0x0200;
	uint8_t *gptr = vgfx;
	for (lines = 0; lines < 16; lines ++) {
		for (cols = 0; cols < 32; cols ++)
			raster_char(lines, cols, *ptr++, *gptr++);
	}
}

static void utan_render(void)
{
	SDL_Rect rect;
	
	rect.x = rect.y = 0;
	rect.w = 32 * CWIDTH;
	rect.h = 16 * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, 32 * CWIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

/* Most PC layouts don't have a colon key so use # */

/* We do this in the 6502 loop instead. Provide a dummy for the device models */
void recalc_interrupts(void)
{
}

static void irqnotify(void)
{
	if (asciikbd_ready(kbd))
		irq6502();
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

static void make_blockfont(void)
{
	uint8_t *p = font + 4096;
	unsigned i, j;

	for(i = 0; i < 256; i++) {
		uint8_t m = i;
		for(j = 0; j < 4; j++) {
			uint8_t v = 0;
			if (m & 1)
				v |= 0xF0;
			if (m & 2)
				v |= 0x0F;
			*p++ = v;
			*p++ = v;
			*p++ = v;
			*p++ = v;
			m >>= 2;
		}
	}
}

static int romload(const char *path, uint8_t *mem, unsigned int maxsize)
{
	int fd;
	int size;
	fd = open(path, O_RDONLY);
	if (fd == -1) {
		perror(path);
		exit(1);
	}
	size = read(fd, mem, maxsize);
	close(fd);
	return size;
}

static void usage(void)
{
	fprintf(stderr, "utan: [-f] [-r monitor] [-F font] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	static int tstates = 377;	/* 750KHz ish. FIXME */
	int opt;
	char *rom_path = "microtan.rom";
	char *font_path = "microtan.font";
	int romsize;

	while ((opt = getopt(argc, argv, "d:fr:F:")) != -1) {
		switch (opt) {
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'F':
			font_path = optarg;
			break;
		case 'r':
			rom_path = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	romsize = romload(rom_path, mem + 0xFC00, 0x0400);
	if (romsize != 0x0400) {
		fprintf(stderr, "microtan: invalid ROM size '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	if (romload(font_path, font, 4096) != 4096) {
		fprintf(stderr, "microtan: invalid font ROM\n");
		exit(EXIT_FAILURE);
	}
	make_blockfont();

	kbd = asciikbd_create();

	atexit(SDL_Quit);
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "microtan: unable to initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}
	window = SDL_CreateWindow("Microtan",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			32 * CWIDTH, 16 * CHEIGHT,
			SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "microtan: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "microtan: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		32 * CWIDTH, 16 * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr, "microtan: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, 32 * CWIDTH,  16 * CHEIGHT);

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

	if (trace & TRACE_CPU)
		log_6502 = 1;

	init6502();
	hookexternal(irqnotify);
	reset6502();
	
	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int i;
		for (i = 0; i < 10; i++) {
			exec6502(tstates);
		}
		/* We want to run UI events before we rasterize */
		asciikbd_event(kbd);
		utan_rasterize();
		utan_render();
		/* Do 10ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
