/*
 *	Ohio Scientific 500 seriels machine with 6502 CPU
 *
 *	Currently simulates a very 500 series board with ACIA
 *	and 6820 as follows
 *
 *	FD00-FFXX is ROM
 *	FC00-FCFF is the system ACIA (partial decode). No IRQ routing.
 *	F700-F7FF is the 6820 PIA (should have IRQ routing but not yet done)
 *	0000-BFFF is RAM (optionally paged -B option)
 *	A000-BFFF can be basic (-b option)
 *	CXXX	  is not yet done
 *	D000-D7FF is a 440 2K video (540 TBD)
 *	D800-EFFF are unpaged RAM (555 or similar)
 *
 *	TODO:
 *	-	430 style cassette port
 *	-	Make video selectable between serial/440/540 style
 *	-	video config and 60Hz timer latches
 *	-	PIA interrupt
 *	-	470/505 floppy controller option
 *	-	540 style keyboard
 *	-	Hard disk maybe
 *
 *	BASIC if present is A000-BFFF
 *
 *	Full memory map for 500 series systems is something like
 *	0000-0FFF	RAM (max 4K on the mainboard)
 *	1000-9FFF	RAM
 *	A000-BFFF	RAM or ROM BASIC
 *	C000		6821 for FDC on disk machine
 *	C002		ACIA for FDC on disk machine
 *	C70X		A15 head end card on 505B
 *	CEXX-CEFF	Multi I/O serial ports
 *	CF00-CF1F	Multi-serial | 555 node port
 *	D000-EFFF	RAM on multi I/O if no video fitted
 *	D000-D7FF	Video RAM
 *	DExx		Video config latch
 *	DFxx		Matrix keyboard
 *	E000-E7FF	Optional colour RAM for video
 *	F4XX		6821 for printer
 *	F5XX		Non centronics printer
 *	F7XX		PIA (banking, 510 CPU control)
 *	FBXX		430 serial
 *	FCXX		Motherboard ACIA
 *	FD00-FFFF	System ROM
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
#include "6821.h"

static uint8_t rom[2048];	/* Pages selected by decoder in 502/5 */
static uint8_t mem[65536]; 	/* Base RAM/ROM */
static uint8_t bankmem[4][49152]; /* Allow for banked memory 0000-BFFF */

#define CWIDTH 8
#define CHEIGHT 8

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
/* Space for the 540, only half used on a 440 */
static uint32_t texturebits[64 * CWIDTH * 32 * CHEIGHT];
static unsigned vwidth;	/* Characters per line */

static unsigned fast;
volatile int emulator_done;
struct acia *acia;
struct m6821 *pia;
static unsigned basic;
static unsigned video;
static uint8_t last_key = 0x80;
static uint8_t pia_a, pia_b;
static unsigned ram_banks = 0;
static unsigned bank_mask;
static uint8_t font[2048];
static unsigned fontsize;
/* ROM page mapping */
static unsigned page_fd;
static unsigned page_fe;
static unsigned page_ff;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_ACIA	0x000010

static int trace = 0;

/* PIA glue */

void m6821_ctrl_change(struct m6821 *pia, uint8_t ctrl)
{
}

uint8_t m6821_input(struct m6821 *pia, int port)
{
	return 0xFF;
}

void m6821_output(struct m6821 *pia, int port, uint8_t data)
{
	/* Capture the bits for our own use. The base machine assigns
	   pia A:1 to A17 and A0: to A16 on the bus. Some setups use
	   A:3 and A:2 as A19 and A18. */
	if (port == 0)
		pia_a = data;
	else
		pia_b = data;
}

void m6821_strobe(struct m6821 *pia, int pin)
{
}

uint8_t do_read_6502(uint16_t addr, unsigned debug)
{
	unsigned page = addr >> 8;
	switch(page) {
	case 0xF7:
		return m6821_read(pia, addr & 3);
	case 0xFC:
		return acia_read(acia, addr & 1);
	/* Hardcoded for now for 440. The pages are
	   440 / ASCII	5 0 1
	   540 / Polled 4 3 2
	   Serial  5 6 6
	   Disk boot (no basic) 5 6 7 */
	case 0xFD:
		return rom[page_fd + (addr & 0xFF)];
	case 0xFE:
		return rom[page_fe + (addr & 0xFF)];
	case 0xFF:
		return rom[page_ff + (addr & 0xFF)];
	}
	/* 440 ASCII keyboard */
	if (video == 440 && page == 0xDF)
		return last_key;
	/* Banked memory option */
	if (ram_banks && addr < 0xC000) {
		if ((pia_a & bank_mask) >= ram_banks)
			return 0xFF;
		return bankmem[pia_a & bank_mask][addr];
	}
	/* We model a full load of memory space for now */
	return mem[addr];
}

uint8_t read6502(uint16_t addr)
{
	uint8_t r = do_read_6502(addr, 0);
	if (trace & TRACE_MEM) {
		if (addr <= 0xC000 && ram_banks)
			fprintf(stderr, "%01X", pia_a & bank_mask);
		fprintf(stderr, "%04X -> %02X\n", addr, r);
	}
	return r;
}

uint8_t read6502_debug(uint16_t addr)
{
	return do_read_6502(addr, 1);
}

static void rom_write(unsigned addr, unsigned val)
{
	if (trace & (TRACE_MEM|TRACE_CPU))
		fprintf(stderr, "%04X ROM (write %02X fail)\n", addr, val);
}

void write6502(uint16_t addr, uint8_t val)
{
	unsigned page = addr >> 8;
	switch(page) {
	case 0xF7:
		m6821_write(pia, addr & 3, val);
		return;
	case 0xFC:
		acia_write(acia, addr & 1, val);
		return;
	case 0xFD:
	case 0xFE:
	case 0xFF:
		rom_write(addr, val);
		return;
	}
	if (basic && page >= 0xA0 && page <= 0xBF)  {
		rom_write(addr, val);
		return;
	}
	if (page >= 0xF0) {
		rom_write(addr, val);
		return;
	}
	if (ram_banks && addr < 0xC000) {
		if ((pia_a & bank_mask) >= ram_banks)
			return;
		bankmem[pia_a & bank_mask][addr] = val;
		if (trace & TRACE_MEM)
			fprintf(stderr, "%01X%04X <- %02X\n", pia_a & bank_mask, addr, val);
	}
	/* Model a full RAM load for now */
	if (trace & TRACE_MEM)
		fprintf(stderr, "%04X <- %02X\n", addr, val);
	mem[addr] = val;
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

	pixp = texturebits + x * CWIDTH + vwidth * CWIDTH * y * CHEIGHT;
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
		pixp += (vwidth - 1) * CWIDTH;
	}
}

static void osi440_rasterize(void)
{
	unsigned int lines, cols;
	uint8_t *ptr = mem + 0xD000;
	uint_fast8_t c;
	for (lines = 0; lines < 32; lines ++) {
		for (cols = 0; cols < vwidth; cols ++) {
			c = *ptr++;
			if (fontsize < 2048)
				c &= 63;
			raster_char(lines, cols, c);
		}
	}
}

static void osi440_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = vwidth * CWIDTH;
	rect.h = 32 * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, vwidth * CWIDTH * 4);
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
	if (s < 0x80) {
		if (mod & (KMOD_LCTRL|KMOD_RCTRL))
			s &= 31;
		return s;
	}
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
	if (video)
		SDL_Quit();
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
	fprintf(stderr, "osi500: [-f] [-r monitor] [-b basic] [-F font] [-d debug] [-B banks]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	static int tstates = 100;	/* 1MHz */
	int opt;
	unsigned romsize;
	char *rom_path = "osi500.rom";
	char *font_path = "osi440.font";
	char *basic_path = NULL;

	while ((opt = getopt(argc, argv, "d:fr:b:B:v")) != -1) {
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
		case 'b':
			basic_path = optarg;
			break;
		case 'B':
			ram_banks = atoi(optarg);
			if (ram_banks > 15) {
				fprintf(stderr, "osi500: maximum of 16 banks\n");
				exit(1);
			}
			break;
		case 'v':
			/* Will need to be selectable eventually */
			video = 440;
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

	if (ram_banks > 4)
		bank_mask = 0x0F;
	else
		bank_mask = 0x03;

	romsize = romload(rom_path, rom, 0x0800);
	if (romsize != 0x0800) {
		fprintf(stderr, "osi500: invalid ROM size '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	fontsize = romload(font_path, font, 2048);	/* 64 or 256 chars 8x8 */
	if (basic_path) {
		basic = 1;
		romload(basic_path, mem + 0xA000, 8192);
	}

	/* Serial boot monitor */
	page_fd = 0x0500;
	page_fe = 0x0600;
	page_ff = 0x0700;

	/* 440 style ASCII keyboard, 2K 440 and BASIC */
	if (video == 440 && basic) {
		page_fd = 0x0100;
		page_fe = 0x0000;
		page_ff = 0x0100;
		rom[0x01E0] = 0x64;
		rom[0x01E1] = 0x18;
		rom[0x01E2] = 0x00;
	}
	/* 540 style video, matrix keyboard, BASIC */
	if (video == 540) {
		if (basic) {
			page_fd = 0x0200;
			page_fe = 0x0300;
			page_ff = 0x0400;
		} else {
			page_fd = 0x0200;
			page_fe = 0x0300;
			page_ff = 0x0700;
		}
	}

	if (video) {
		if (video == 440)
			vwidth = 32;
		else
			vwidth = 64;

		if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
			fprintf(stderr, "osi500: unable to initialize SDL: %s\n",
				SDL_GetError());
			exit(1);
		}
		window = SDL_CreateWindow("OSI 500",
				SDL_WINDOWPOS_UNDEFINED,
				SDL_WINDOWPOS_UNDEFINED,
				vwidth * CWIDTH, 32 * CHEIGHT,
				SDL_WINDOW_RESIZABLE);
		if (window == NULL) {
			fprintf(stderr, "osi500: unable to open window: %s\n",
				SDL_GetError());
			exit(1);
		}
		render = SDL_CreateRenderer(window, -1, 0);
		if (render == NULL) {
			fprintf(stderr, "osi500: unable to create renderer: %s\n",
				SDL_GetError());
			exit(1);
		}
		texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			vwidth * CWIDTH, 32 * CHEIGHT);
		if (texture == NULL) {
			fprintf(stderr, "osi500: unable to create texture: %s\n",
				SDL_GetError());
			exit(1);
		}
		SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
		SDL_RenderClear(render);
		SDL_RenderPresent(render);
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
		SDL_RenderSetLogicalSize(render, vwidth * CWIDTH,  32 * CHEIGHT);
	}

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
		if (video) {
			ui_event();
			osi440_rasterize();
			osi440_render();
		}
		acia_timer(acia);
		/* Do 10ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
