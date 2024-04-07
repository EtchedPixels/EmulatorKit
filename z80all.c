/*
 *	Bill Shen's Z80 machine with VGA video and PS/2 interface implemented using
 *	a dual port RAM and CPLD
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

#include <SDL2/SDL.h>

#include "system.h"
#include "libz80/z80.h"
#include "z80dis.h"

#include "ide.h"
#include "ps2keymap.h"
#include "serialdevice.h"
#include "16x50.h"
#include "ttycon.h"

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;

#define CWIDTH	8
#define CHEIGHT	8
#define COLS	64
#define ROWS	48

static uint32_t texturebits[ROWS * COLS * CWIDTH * CHEIGHT];

static uint8_t ram[131072];
static uint8_t vram[4096];
static uint8_t banklatch = 3;

static struct ide_controller *ide;
struct uart16x50 *uart;

static uint8_t fast = 0;
static unsigned rom_mapped = 1;

static uint8_t ps2stat;
static uint8_t ps2queue[16];
static unsigned ps2qlen;

static Z80Context cpu_z80;
volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_CPU	0x000004
#define TRACE_IDE	0x000008
#define TRACE_PS2	00000010

unsigned trace;

static void poll_irq(void)
{
	if ((ps2stat & 0x80) || uart16x50_irq_pending(uart))
		Z80INT(&cpu_z80, 0x78);
	else
		Z80NOINT(&cpu_z80);
}

static void ps2_queue_byte(uint8_t c)
{
	if (ps2qlen < 16)
		ps2queue[ps2qlen++] = c;
}

static uint8_t ps2_dequeue(void)
{
	uint8_t r;
	if (ps2qlen == 0)
		return 0xFF;
	r = ps2queue[0];
	memmove(ps2queue, ps2queue + 1, 15);
	ps2qlen--;
	return r;
}

/* Nothing to do */
void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
}

/* FIXME: use the smaller correct IROM */
static uint8_t irom[64] = {
	0x3C,
	0x3C,
	0x3C,
	0x3C,

	0x21,
	0x00,
	0xB0,

	0xDB,
	0xF8,

	0xE6,
	0x01,

	0x20,
	0x24,

	0xDB,
	0x17,

	0xE6,
	0x80,

	0x20,
	0xF4,

	0x47,

	0xD3,
	0x15,

	0xD3,
	0x14,

	0x3C,

	0xD3,
	0x13,

	0x0E,
	0x10,

	0xD3,
	0x12,

	0x3E,
	0x20,

	0xD3,
	0x17,

	0xDB,
	0x17,

	0xE6,
	0x08,

	0x28,
	0xFA,

	0xED,
	0xB2,

	0xC3,
	0x00,
	0xB0,

	0x3C,
	0x3C,
	0x3C,

	0xDB,
	0xF9,

	0xDB,
	0xF8,

	0xE6,
	0x01,

	0x28,
	0xFA,

	0xDB,
	0xF9,

	0x77,

	0x2C,

	0x20,
	0xF4,
	0xE9
};

static uint8_t *memptr(uint16_t addr)
{
	if (addr >= 0x8000)
		return ram + 65536 + addr;
	return ram + (0x8000 * banklatch) + (addr & 0x7FFF);
}

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X <- %02X\n", addr, val);
	*memptr(addr) = val;
}

uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t r;
	if (rom_mapped && addr < sizeof(irom))
		r = irom[addr];
	else
		r = *memptr(addr);
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X -> %02X\n", addr, r);
	return r;
}

static uint8_t *vram_permute(uint16_t addr)
{
	uint16_t off = addr >> 8;
	off |= (addr & 0x0F) << 8;
	return vram + off;
}

static void vram_write(uint16_t addr, uint8_t val)
{
	*vram_permute(addr) = val;
}

static uint8_t vram_read(uint16_t addr)
{
	return *vram_permute(addr);
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "W IO %04X <- %02X\n", addr, val);
	if ((addr & 0xF0) == 0x00) {
		vram_write(addr, val);
		return;
	}
	if ((addr & 0xF8) == 0x10) {
		ide_write8(ide, addr & 0x07, val);
		return;
	}
	if ((addr & 0xE0) == 0xC0) {
		/* Should be 4 uarts really TODO */
		uart16x50_write(uart, addr & 7, val);
		return;
	}

	switch (addr & 0xFF) {
	case 0x1F:		/* Memory control */
		if (val & 0x80)
			rom_mapped = 0;
		banklatch = val & 3;
		return;
	case 0xF5:		/* PS/2 status/command */
		/* TODO: confusing description */
		ps2stat = val;
		if (val & 0x80)
			ps2stat &= 0x7F;
		return;
	}
}

uint8_t io_read(int unused, uint16_t addr)
{
	uint8_t r = 0x78;
	if ((addr & 0xF0) == 0x00)
		r = vram_read(addr);
	else if ((addr & 0xF8) == 0x10)
		r = ide_read8(ide, addr & 0x07);
	else if ((addr & 0xE0) == 0xC0) {
		/* Should be 4 uarts really TODO */
		r = uart16x50_read(uart, addr & 7);
	} else {
		switch (addr) {
		case 0xF4:
			return ps2_dequeue();
		case 0xF5:
			r = ps2stat & 0xFE;
			if (ps2qlen)
				r |= 1;
			break;
		}
	}
	if (trace & TRACE_IO)
		fprintf(stderr, "R IO %04X -> %02X\n", addr, r);
	return r;
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = mem_read(0, addr);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return mem_read(0, addr);
}

static void z80_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED && (z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while (nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n", cpu_z80.R1.br.A, cpu_z80.R1.br.F, cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL, cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}

static void raster_char(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp;
	uint8_t rev = 0;
	uint32_t *pixp;
	unsigned int rows, pixels;

	if (c & 0x80) {
		rev = 0xFF;
		c &= 0x7F;
	}
	fp = vram + 3072 + c * 8;
	pixp = texturebits + x * CWIDTH + COLS * CWIDTH * y * CHEIGHT;

	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp++ ^ rev;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x80)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 63 */
		pixp += 63 * CWIDTH;
	}
}

static void vga_rasterize(void)
{
	unsigned lines, cols;
	uint8_t *ptr = vram;
	for (lines = 0; lines < ROWS; lines++)
		for (cols = 0; cols < COLS; cols++)
			raster_char(lines, cols, *ptr++);
}

static void vga_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = COLS * CWIDTH;
	rect.h = ROWS * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, COLS * CWIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

static uint16_t ps2map(SDL_Scancode code)
{
	struct keymapping *k = keytable;
	while (k->code != SDL_SCANCODE_UNKNOWN) {
		if (k->code == code)
			return k->ps2;
		k++;
	}
	return 0;
}

static void make_ps2_code(SDL_Event *ev)
{
	uint16_t code = ev->key.keysym.scancode;
	if (code > 255)
		return;
	code = ps2map(code);
	if (ev->type == SDL_KEYUP)
		ps2_queue_byte(0xF0);
	if (code >> 8)
		ps2_queue_byte(code >> 8);
	ps2_queue_byte(code);
}

void ui_event(void)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			emulator_done = 1;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			make_ps2_code(&ev);
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
	SDL_Quit();
}

static void usage(void)
{
	fprintf(stderr, "z80all: [-f] [-i idepath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	char *idepath = NULL;

	uint8_t *p = ram;
	while (p < ram + sizeof(ram))
		*p++ = rand();

	while ((opt = getopt(argc, argv, "d:fi:")) != -1) {
		switch (opt) {
		case 'i':
			idepath = optarg;
			break;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	ide = ide_allocate("cf");
	if (ide && idepath) {
		int ide_fd = open(idepath, O_RDWR);
		if (ide_fd == -1) {
			perror(idepath);
			exit(1);
		}
		if (ide_attach(ide, 0, ide_fd) == 0)
			ide_reset_begin(ide);
	}

	/* One for now */
	uart = uart16x50_create();
	uart16x50_attach(uart, &console);

	tc.tv_sec = 0;
	tc.tv_nsec = 16666667;

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

	atexit(SDL_Quit);
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "sorceror: unable to initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}
	window = SDL_CreateWindow("Z80ALL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, COLS * CWIDTH, ROWS * CHEIGHT, SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "sorceror: unable to open window: %s\n", SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "sorceror: unable to create renderer: %s\n", SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, COLS * CWIDTH, ROWS * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr, "sorceror: unable to create texture: %s\n", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, COLS * CWIDTH, ROWS * CHEIGHT);
	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;
	cpu_z80.trace = z80_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* 25.175MHz -> 419583 T states a frame. We do 419584 as it's rather easier
	   to factorise down */
	while (!emulator_done) {
		int i;
		if (cpu_z80.halted && !cpu_z80.IFF1) {
			/* HALT with interrupts disabled, so nothing left
			   to do, so exit simulation. If NMI was supported,
			   this might have to change. */
			emulator_done = 1;
			break;
		}
		for (i = 0; i < 149; i++) {
			int j;
			for (j = 0; j < 11; j++) {
				Z80ExecuteTStates(&cpu_z80, 256);
				poll_irq();
			}
			uart16x50_event(uart);
			/* We want to run UI events regularly it seems */
			ui_event();
		}
		vga_rasterize();
		vga_render();
		/* Do 16.6667ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (ps2stat & 0x40)
			ps2stat |= 0x80;
		poll_irq();
	}
	exit(0);
}
