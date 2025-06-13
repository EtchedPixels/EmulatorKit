/*
 *	Ohio Scientific Superboard (original) with 6502 CPU
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
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "6502.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"

static uint8_t mem[65536];	/* Mostly usually absent */
static unsigned ram_mask;

#define CWIDTH 8
#define CHEIGHT 8

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[32 * CWIDTH * 32 * CHEIGHT];

static unsigned fast;
volatile int emulator_done;
struct acia *acia;
static unsigned basic;
static uint8_t last_key = 0x80;

static uint8_t font[512];

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_ACIA	0x000010

static int trace = 0;

uint8_t do_read_6502(uint16_t addr, unsigned debug)
{
	if (addr >= 0xF000 && addr < 0xFE00 && !debug)
		return acia_read(acia, addr & 1);
	if ((addr & 0xFF00)== 0xDF00)
		return last_key;
	if (basic && addr >= 0xA000 && addr <= 0xBFFF)
		return mem[addr];
	if (addr < 0xA000)
		addr &= ram_mask;
	return mem[addr];
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
	if (addr >= 0xF000 && addr < 0xFE00) {
		acia_write(acia, addr & 1, val);
		return;
	}
	/* There is 1K of RAM on a 440. The graphics extension makes it 4K
	   with plottable 'pixel' graphics at 128x128 but we don't do that yet */
	if (addr >= 0xD000 && addr <= 0xD400)  {
		mem[addr] = val;
		return;
	}

	if (addr < 0xA000 || (!basic && addr < 0xD000)) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X <- %02X\n", addr, val);
		addr &= ram_mask;
		mem[addr] = val;
	} else {
		if (trace & (TRACE_MEM|TRACE_CPU))
			fprintf(stderr, "%04X ROM (write %02X fail)\n", addr, val);
	}
}

/* We do this in the 6502 loop instead. Provide a dummy for the device models */
void recalc_interrupts(void)
{
}

/* Raster a single symbol */

static void raster_char(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp = &font[8 * c];
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + 32 * CWIDTH * y * CHEIGHT;
	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp++;
		/* On the OSI 4xx the low bit is left side */
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x01)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits >>= 1;
		}
		/* We moved on one char, move on the other 31 */
		pixp += 31 * CWIDTH;
	}
}

static void osi440_rasterize(void)
{
	unsigned int lines, cols;
	uint8_t *ptr = mem + 0xD000;
	for (lines = 0; lines < 32; lines ++) {
		for (cols = 0; cols < 32; cols ++)
			raster_char(lines, cols, *ptr++ & 0x3F);
	}
	/* For the extended one we need to then raster a 128 x 128 bitmap
	   on top (logical or) TODO */
}

static void osi440_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = 32 * CWIDTH;
	rect.h = 32 * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, 32 * CWIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

static const uint8_t numshift[] = { ')','!', '"', 0x80,'$', '%', '^', '&', '*', '(' };

static uint8_t sdl_to_key(unsigned s, unsigned mod)
{
	/* Just do some basic keyboarding */
	if (s >= 'a' && s <= 'z') {
		if (mod & (KMOD_LSHIFT|KMOD_RSHIFT))
			s &= 0xDF;
		if (mod & (KMOD_LCTRL|KMOD_RCTRL))
			s &= 31;
		return s;
	}
	if (s >= '0' && s <= '9') {
		if (mod & (KMOD_LSHIFT|KMOD_RSHIFT))
			s = numshift[s - '0'];
		return s;
	}
	if (mod & (KMOD_LSHIFT|KMOD_RSHIFT)) {
		switch(s) {
		case SDLK_COMMA:
			return '<';
		case SDLK_PERIOD:
			return '>';
		case SDLK_MINUS:
			return '_';
		case SDLK_SEMICOLON:
			return ':';
		case SDLK_LEFTPAREN:
			return '{';
		case SDLK_RIGHTPAREN:
			return '}';
		case SDLK_EQUALS:
			return '+';
		case SDLK_SLASH:
			return '?';
		case SDLK_HASH:
			return '~';
		case SDLK_BACKSLASH:
			return '|';
		}
	}
	if (s == SDLK_DELETE)
		return 127;
	if (s < 0x80)
		return s;
	return 0x80;
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
			/* FIXME: SDL2 doesn't have a nice keymapping
			   set it seems */
			last_key = sdl_to_key(ev.key.keysym.sym, ev.key.keysym.mod);
			break;
		case SDL_KEYUP:
			last_key |= 0x80;
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
	fprintf(stderr, "osi400: [-f] [-m mem] [-r monitor] [-b basic] [-F font] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	static int tstates = 100;	/* 1MHz */
	int opt;
	unsigned memsize = 1;
	unsigned romsize;
	char *rom_path = "osi400.rom";
	char *font_path = "osi440.font";
	char *basic_path = NULL;

	while ((opt = getopt(argc, argv, "d:fr:m:b:")) != -1) {
		switch (opt) {
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'r':
			rom_path = optarg;
			break;
		case 'F':
			font_path = optarg;
			break;
		case 'm':
			memsize = atoi(optarg);
			break;
		case 'b':
			basic_path = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (memsize == 0 || (memsize & (memsize - 1))) {
		fprintf(stderr, "osi400: only power of 2 memory sizes supported.\n");
		exit(1);
	}
	if (memsize > 52) {
		fprintf(stderr, "osi400: maximum ram of 52K\n");
		exit(1);
	}
	ram_mask = (memsize * 1024) - 1;

	acia = acia_create();
	acia_attach(acia, &console);
	acia_trace(acia, trace & TRACE_ACIA);

	romsize = romload(rom_path, mem + 0xFE00, 0x0200);
	if (romsize == 0x0100)
		memcpy(mem + 0xFF00, mem + 0xFE00, 256);
	else if (romsize != 0x0200) {
		fprintf(stderr, "osi400: invalid ROM size '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	romload(font_path, font, 512);	/* 64 chars 8x8 */
	if (basic_path) {
		basic = 1;
		romload(basic_path, mem + 0xA000, 8192);
	}

	atexit(SDL_Quit);
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "osi400: unable to initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}
	window = SDL_CreateWindow("OSI 440",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			32 * CWIDTH, 32 * CHEIGHT,
			SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "osi400: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "osi400: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		32 * CWIDTH, 32 * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr, "osi400: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, 32 * CWIDTH,  32 * CHEIGHT);

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
	reset6502();

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int i;
		for (i = 0; i < 100; i++) {
			exec6502(tstates);
		}
		/* We want to run UI events before we rasterize */
		ui_event();
		osi440_rasterize();
		osi440_render();
		acia_timer(acia);
		/* Do 10ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
