/*
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
#include "event.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"
#include "keymatrix.h"

#define CWIDTH 8
#define CHEIGHT 16

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[48 * CWIDTH * 16 * CHEIGHT];

struct keymatrix *matrix;
struct acia *acia;

static uint8_t mem[65536];
static uint8_t font[2048];

static unsigned int video_upgrade;

static uint8_t fast;
volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_ACIA	0x000010
#define TRACE_KEY	0x000020

static int trace = 0;

static uint8_t keylatch;

uint8_t do_read_6502(uint16_t addr, unsigned debug)
{
	if (addr >= 0xDF00  && addr < 0xDFFF && !debug) {
		uint8_t r = keymatrix_input(matrix, (uint8_t)~keylatch);
		return ~r;
	}
	if (addr >= 0xF000 && addr < 0xF800 && !debug)
		return acia_read(acia, addr & 1);
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
	int is_ram = 0;
	if (addr >= 0xDF00 && addr <= 0xDFFF) {
		keylatch = val;
		return;
	}
	if (addr >= 0xF000 && addr < 0xF800) {
		acia_write(acia, addr & 1, val);
		return;
	}
	if (addr < 0x8000 || (addr >= 0xD000 && addr < 0xD800))
		is_ram = 1;
	if (is_ram) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X <- %02X\n", addr, val);
		mem[addr] = val;
	} else {
		if (trace & (TRACE_MEM|TRACE_CPU))
			fprintf(stderr, "%04X ROM (write %02X fail)\n", addr, val);
	}
}

/*
 *	Keyboard mapping (TODO)
 */

static SDL_Keycode keyboard[] = {
	SDLK_CAPSLOCK, SDLK_RSHIFT, SDLK_LSHIFT, 0, 0, SDLK_ESCAPE, SDLK_LCTRL,  0, /* REPEAT?? */
	0, SDLK_p, SDLK_SEMICOLON, SDLK_SLASH, SDLK_SPACE, SDLK_z, SDLK_a, SDLK_q,
	0, SDLK_LESS, SDLK_m, SDLK_n, SDLK_b, SDLK_v, SDLK_c, SDLK_x,
	0, SDLK_k, SDLK_j, SDLK_h, SDLK_g, SDLK_f, SDLK_d, SDLK_s,
	0, SDLK_i, SDLK_u, SDLK_y, SDLK_t, SDLK_r, SDLK_e, SDLK_w,
	0, 0, 0, SDLK_RETURN, 0/*SDLK_CARET*/, SDLK_o, SDLK_l, SDLK_GREATER,
	0, 0, SDLK_BACKSPACE, SDLK_MINUS, SDLK_HASH/*:*/, SDLK_0, SDLK_9, SDLK_8,
	0, SDLK_7, SDLK_6, SDLK_5, SDLK_4, SDLK_3, SDLK_2, SDLK_1
};

/*
 *	The uk101 video is built from standard logic. It provides a
 *	composite 64x16 display with about 48 actually visible characters.
 *	Accessing video memory should cause snow but we don't emulate that
 *	yet.
 *
 *	In many ways it's a lot like the Nascom.
 */

static void raster_char(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp = &font[8 * c];
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + 48 * CWIDTH * y * CHEIGHT;
	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp;
		if (rows & 1)
			fp++;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x80)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 47 */
		pixp += 47 * CWIDTH;
	}
}

static void raster_char_vu(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp = &font[8 * c];
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + 48 * CWIDTH * y * CHEIGHT / 2;
	for (rows = 0; rows < CHEIGHT / 2; rows++) {
		uint8_t bits = *fp++;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x80)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 47 */
		pixp += 47 * CWIDTH;
	}
}

static void uk101_rasterize(void)
{
	unsigned int lptr = 0xD00C;
	unsigned int lines, cols;
	uint8_t *ptr;
	unsigned int nlines = video_upgrade ? 32 : 16;
	for (lines = 0; lines < nlines; lines ++) {
		ptr = mem + lptr;
		for (cols = 0; cols < 48; cols ++) {
			if(video_upgrade)
				raster_char_vu(lines, cols, *ptr++);
			else
				raster_char(lines, cols, *ptr++);
		}
		lptr += 0x40;
	}
}

static void uk101_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = 48 * CWIDTH;
	rect.h = 16 * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, 48 * CWIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

/* Most PC layouts don't have a colon key so use # */
static void keytranslate(SDL_Event *ev)
{
	SDL_Keycode c = ev->key.keysym.sym;
	switch(c) {
	case SDLK_HASH:
		c = SDLK_COLON;
		break;
	}
	ev->key.keysym.sym = c;
}

/* We do this in the 6502 loop instead. Provide a dummy for the device models */
void recalc_interrupts(void)
{
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
	fprintf(stderr, "uk101: [-f] [-2] [-b basic] [-r monitor] [-F font] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	static int tstates = 100;	/* 2MHz */
	int opt;
	char *rom_path = "uk101mon.rom";
	char *basic_path = "uk101basic.rom";
	char *font_path = "uk101font.rom";
	int romsize;

	while ((opt = getopt(argc, argv, "2b:d:fr:vF:")) != -1) {
		switch (opt) {
		case '2':
			tstates = 200;
			break;
		case 'v':
			video_upgrade = 1;
			break;
		case 'b':
			basic_path = optarg;
			break;
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

	acia = acia_create();
	acia_attach(acia, &console);
	acia_trace(acia, trace & TRACE_ACIA);

	romsize = romload(rom_path, mem + 0xF800, 0x0800);
	if (romsize != 0x0800) {
		fprintf(stderr, "uk101: invalid ROM size '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	romsize = romload(basic_path, mem + 0xA000, 0x2000);
	if (romsize != 0x2000) {
		fprintf(stderr, "uk101: invalid ROM size '%s'.\n", basic_path);
		exit(EXIT_FAILURE);
	}
	romload(font_path, font, 2048);

	ui_init();

	matrix = keymatrix_create(8, 8, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_add_events(matrix);
	keymatrix_translator(matrix, keytranslate);

	window = SDL_CreateWindow("UK101",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			48 * CWIDTH, 16 * CHEIGHT,
			SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "uk101: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "uk101: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		48 * CWIDTH, 16 * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr, "uk101: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, 48 * CWIDTH,  16 * CHEIGHT);

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
		uk101_rasterize();
		uk101_render();
		acia_timer(acia);
		/* Do 10ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
