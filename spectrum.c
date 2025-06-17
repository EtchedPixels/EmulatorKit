/*
 *	Very basic ZX Spectrum set up for debugging stuff. This does not
 *	do all the timing related magic required to run games correctly
 *	with effects and stuff.
 *
 *	TODO: ZXCF, Simple CF cards
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "libz80/z80.h"
#include "z80dis.h"
#include "ide.h"
#include "lib765/include/765.h"

#include <SDL2/SDL.h>
#include "event.h"
#include "keymatrix.h"

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;

#define BORDER	32
#define WIDTH	(256 + 2 * BORDER)
#define HEIGHT	(192 + 2 * BORDER)

static uint32_t texturebits[WIDTH * HEIGHT];

static uint32_t palette[16] = {
	0xFF000000,
	0xFF0000A0,
	0xFF00A000,
	0xFF00A0A0,
	0xFFA00000,
	0xFFA000A0,
	0xFFA0A000,
	0xFFA0A0A0,
	0xFF000000,
	0xFF0000E0,
	0xFF00E000,
	0xFF00E0E0,
	0xFFE00000,
	0xFFE000E0,
	0xFFE0E000,
	0xFFE0E0E0
};

/* Keep all the memory in one map for now. 128K will need to do a bit
   more work later */
static uint8_t ram[16][16384];
#define ROM(x)	(x)
#define RAM(x)	((x) + 8)

static struct keymatrix *matrix;
static Z80Context cpu_z80;
static FDC_PTR fdc;
static FDRV_PTR drive_a, drive_b;
static struct ide_controller *ide;

static int tape = -1;		/* Tape file handle */
static unsigned mem = 48;	/* First byte above RAM (defaults to 48K) */
static uint8_t ula;		/* ULA state */
static uint8_t frames;		/* Flash counter */
static uint8_t mlatch;
static uint8_t p3latch;
static unsigned map[4] = { ROM(0), RAM(5), RAM(2), RAM(0) };
static unsigned vram = RAM(5);

static unsigned drawline;	/* If rasterising */
static unsigned blanked;	/* True if blanked */

static uint8_t divmem[524288];/* Full 512K emulated */
static uint8_t divrom[524288];
static uint8_t divide_latch;
static unsigned divide_mapped;	/* 0 no, 1 yes */
static unsigned divide_oe;
static unsigned divide_pair;	/* Latches other half of wordstream for IDE */
static unsigned divide;

static uint8_t divplus_latch;
static unsigned divplus_128k = 1;
static unsigned divplus_7ffd;

#define ZX_48K_2	0
#define ZX_48K_3	1
#define ZX_128K		2
#define ZX_PLUS3	3
static unsigned model = ZX_48K_3;

static volatile int emulator_done;
static unsigned fast;
static unsigned int_recalc;
/* static unsigned live_irq; */

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_IRQ	4
#define TRACE_KEY	8
#define TRACE_CPU	16
#define TRACE_FDC	32

static int trace = 0;

static void reti_event(void);

static uint8_t *divbank(unsigned bank, unsigned page, unsigned off)
{
	bank <<= 2;
	bank |= page;
	return &divmem[bank * 0x2000 + (off & 0x1FFF)];
}

static uint8_t *divide_getmap(unsigned addr, unsigned w)
{
	unsigned bank = 0;
	if (divide == 2) {
		switch(divplus_latch & 0xC0) {
		case 0x00:	/* DivIDE mode */
			bank = (divplus_latch >> 1) & 0x0F;
			break;
		case 0x40:
			if (w & (divplus_latch & 0x20))
				return NULL;
			return &divmem[((divplus_latch & 0x1F) << 14) +
				(addr & 0x3FFF)];
		case 0x80:
			if (w)
				return NULL;
			return &divrom[((divplus_latch & 0x1F) << 14) +
				(addr & 0x3FFF)];
		}
	}
	/* TODO mapmem should probably stop RAM 3 writes without CONMEM
	   even if 2000-3FFF */
	if (addr & 0x2000)
		return divbank(bank, divide_latch & 3, addr);
	/* CONMEM */
	if (divide_latch & 0x80) {
		if (w)
			return NULL;
		if (divide == 2)
			return divrom + bank * 0x8000 + 0x6000 + (addr & 0x1FFF);
		else
			return divrom + (addr & 0x1FFF);
	}
	/* MAPMEM */
	if (divide_latch & 0x40) {
		if (w)
			return NULL;
		return divbank(bank, 3, addr);
	}
	if (divide == 2)
		return divrom + bank * 0x8000 + 0x6000 + (addr & 0x1FFF);
	else
		return divrom + (addr & 0x1FFF);
}

static void divide_write(uint16_t addr, uint8_t val)
{
	uint8_t *p = divide_getmap(addr, 1);
	if (p)
		*p = val;
}

static uint8_t divide_read(uint16_t addr)
{
	return *divide_getmap(addr, 0);
}

/* TODO: memory contention */
static uint8_t do_mem_read(uint16_t addr, unsigned debug)
{
	unsigned bank = map[addr >> 14];
	/* For now until banked stuff */
	if (addr >= mem)
		return 0xFF;
	if (addr < 0x4000 && divide_mapped == 1)
		return divide_read(addr);
	return ram[bank][addr & 0x3FFF];
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	unsigned bank = map[addr >> 14];
	if (addr >= mem)
		return;
	if (addr < 0x4000 && divide_mapped == 1) {
		divide_write(addr, val);
		return;
	}
	/* ROM is read only */
	if (bank >= RAM(0))
		ram[bank][addr & 0x3FFF] = val;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate;
	uint8_t r;

	/* DivIDE+ modes other than 00 don't autopage */
	if (cpu_z80.M1 && !(divplus_latch & 0xC0)) {
		/* Immediate map */
		if (divide == 1 && addr >= 0x3D00 && addr <= 0x3DFF)
			divide_mapped = 1;
		/* TODO: correct this based on the B4 latch and 128K flag */
		if (divide == 2 && (model <= ZX_48K_3 || !divplus_128k || (mlatch & 0x10)) && addr >= 0x3D00 && addr <= 0x3DFF)
			divide_mapped = 1;
	}

	r = do_mem_read(addr, 0);

	/* Look for ED with M1, followed directly by 4D and if so trigger
	   the interrupt chain */
	if (cpu_z80.M1) {
		if (!(divplus_latch & 0xC0)) {
			/* ROM paging logic */
			if (divide && addr >= 0x1FF8 && addr <= 0x1FFF)
				divide_mapped = 0;
			if (divide && (addr == 0x0000 || addr == 0x0008 || addr == 0x0038 ||
				addr == 0x0066 || addr == 0x04C6 || addr == 0x0562))
				divide_mapped = 1;
		}
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r == 0xCB) {
			rstate = 2;
			return r;
		}
		/* Look for ED with M1, followed directly by 4D and if so trigger
		   the interrupt chain */
		if (r == 0xED && rstate == 0) {
			rstate = 1;
			return r;
		}
	}
	if (r == 0x4D && rstate == 1)
		reti_event();
	rstate = 0;
	return r;
}

static void recalc_mmu(void)
{
	map[3] = RAM(mlatch & 7);
	if (mlatch & 0x08)
		vram = RAM(7);
	else
		vram = RAM(5);
	if (model == ZX_128K) {
		if (mlatch & 0x10)
			map[0] = ROM(1);
		else
			map[0] = ROM(0);
	}
	if (model == ZX_PLUS3) {
		unsigned rom = (mlatch & 0x10) ? 1 : 0;
		if (p3latch & 0x04)
			rom |= 2;
		map[0] = ROM(rom);
		switch(p3latch & 0x07) {
		case 1:
			map[0] = RAM(0);
			map[1] = RAM(1);
			map[2] = RAM(2);
			map[3] = RAM(3);
			break;
		case 3:
			map[0] = RAM(4);
			map[1] = RAM(5);
			map[2] = RAM(6);
			map[3] = RAM(7);
			break;
		case 5:
			map[0] = RAM(4);
			map[1] = RAM(5);
			map[2] = RAM(6);
			map[3] = RAM(3);
			break;
		case 7:
			map[0] = RAM(4);
			map[1] = RAM(7);
			map[2] = RAM(6);
			map[3] = RAM(3);
			break;
		}
	}
}

static void repaint_border(unsigned colour)
{
	uint32_t *p = texturebits;
	unsigned x,y;
	uint32_t border = palette[colour];

	for(y = 0; y < BORDER; y++)
		for(x = 0; x < WIDTH; x++)
			*p++ = border;
	for(y = BORDER; y < BORDER + 192; y++) {
		for(x = 0; x < BORDER; x++)
			*p++ = border;
		p += 256;
		for(x = 0; x < BORDER; x++)
			*p++ = border;
	}
	for(y = 0; y < BORDER; y++)
		for (x = 0; x < WIDTH; x++)
			*p++ = border;
}

static void fdc_log(int debuglevel, char *fmt, va_list ap)
{
	if ((trace & TRACE_FDC) || debuglevel == 0) {
		fprintf(stderr, "fdc: ");
		vfprintf(stderr, fmt, ap);
	}
}

static void ula_write(uint8_t v)
{
	/* ear is bit 4 mic is bit 3, border low bits */
	ula = v;
	repaint_border(v & 7);
}

static uint8_t ula_read(uint16_t addr)
{
	uint8_t r = 0xA0;	/* Fixed bits */

	if (model != ZX_PLUS3) {
		if (ula & 0x10)		/* Issue 3 and later */
			r |= 0x40;
		if (model == ZX_48K_2 && (ula & 0x08))
			r |= 0x40;
	}
	/* Low 5 bits are keyboard matrix map */
	r |= ~keymatrix_input(matrix, ~(addr >> 8)) & 0x1F;
	return r;
}

static uint8_t floating(void)
{
	unsigned n;
	if (blanked || model == ZX_PLUS3)
		return 0xFF;
	n = cpu_z80.tstates;
	n /= 4;
	if (n < 32)
		return ram[vram][0x1800 + 32 * drawline + n];
	return 0xFF;
}

static void divplus_ctrl(uint8_t val)
{
	divplus_latch = val;
	switch(val & 0xE0) {
	case 0xC0:
	case 0xE0:
		divide_latch = 0;
		divplus_latch = 0;
		divplus_7ffd = 0;
		divide_mapped = 0;
		break;
	case 0x00:
		/* DivIDE mode */
		/* bits 4-1 selects the extended banking */
		break;
	case 0x20:
		/* Enable 128K mode */
		divplus_128k = 1;
		break;
		/* RAM mode: 10WAAAAA */
		/* 32K pages x 16K replace Spectrum ROM, DivIDE traps off */
	case 0x40:
	case 0x60:
		break;
	case 0x80:
		/* ROM mode: as above for 16K ROM banks */
	case 0xA0:
		break;
	}
}

static uint8_t io_read(int unused, uint16_t addr)
{
	unsigned r;
	/* Timex checks XXFE, Sinclair just the low bit */
	if ((addr & 0x01) == 0)	/* ULA */
		return ula_read(addr);
	if (model == ZX_PLUS3) {
		if ((addr & 0xF002) == 0x2000)
			return fdc_read_ctrl(fdc);
		if ((addr & 0xF002) == 0x3000)
			return fdc_read_data(fdc);
	}
	if (divide) {
		if ((addr & 0xE3) == 0xA3) {
			r = (addr >> 2) & 0x07;
			if (r) {
				divide_oe = 1;	/* Odd mode */
				return ide_read16(ide, r);
			}
			if (divide_oe == 0) {
				divide_oe = 1;
				return divide_pair;
			}
			r = ide_read16(ide, 0);
			divide_pair = r >> 8;
			divide_oe = 0;
			return r & 0xFF;
		}
	}
	return floating();
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr & 1) == 0)
		ula_write(val);
	if (model == ZX_128K && (addr & 0x8002) == 0) {
		if ((mlatch & 0x20) == 0) {
			mlatch = val;
			recalc_mmu();
		}
	}
	if (model == ZX_PLUS3 && (addr & 0xF002) == 0x3000) {
		fdc_write_data(fdc, val);
	}
	if (model == ZX_PLUS3 && (addr & 0xC002) == 0x4000) {
		if ((mlatch & 0x20) == 0) {
			mlatch = val;
			recalc_mmu();
		}
	}
	if (model == ZX_PLUS3 && (addr & 0xF002) == 0x1000) {
		/* Does the memory latch lock this too ? TODO */
		p3latch = val;
		if (p3latch & 0x08)
			fdc_set_motor(fdc, 3);
		else
			fdc_set_motor(fdc, 0);
		recalc_mmu();
	}
	if (divide) {
		if ((addr & 0xE3) == 0xA3) {
			uint8_t r = (addr >> 2) & 0x07;
			if (r) {
				divide_oe = 1;	/* Odd mode */
				ide_write16(ide, r, val);
			} else if (divide_oe == 1) {
				divide_oe = 0;
				divide_pair = val;
			} else {
				ide_write16(ide, 0, divide_pair | (val << 8));
				divide_oe = 0;
			}
		}
		if ((addr & 0xE3) == 0xE3) {
			/* MAPRAM cannot be cleared */
			val |= divide_latch & 0x40;
			divide_latch = val;
			if (val & 0x80)
				divide_mapped = 1;
		}
		if (divide ==2 && (addr & 0xFF) == 0x17)
			divplus_ctrl(val);
	}
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = do_mem_read(addr, 1);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return do_mem_read(addr, 1);
}

static void z80_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc
	    && z80dis_byte_quiet(lastpc) == 0xED
	    && (z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while (nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr,
		"[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z80.R1.br.A, cpu_z80.R1.br.F, cpu_z80.R1.wr.BC,
		cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL,
		cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}


static void poll_irq_event(void)
{
}

static void reti_event(void)
{
}

static void raster_byte(unsigned lines, unsigned cols, uint8_t byte, uint8_t attr)
{
	uint32_t *pixp;
	unsigned x;
	unsigned paper = (attr >> 3) & 0x0F;
	unsigned ink = attr & 7;
	if (attr & 0x40)
		paper |= 0x08;

	/* Flash swaps every 16 frames */
	if ((attr & 0x80) && (frames & 0x10)) {
		x = ink;
		ink = paper;
		paper = x;
	}

	pixp = texturebits + (lines + BORDER) * WIDTH + cols * 8 + BORDER;

	for (x = 0; x < 8; x++) {
		if (byte & 0x80)
			*pixp++ = palette[ink];
		else
			*pixp++ = palette[paper];
		byte <<= 1;
	}
}

static void raster_block(unsigned ybase, unsigned off, unsigned aoff)
{
	unsigned c,l,w;
	uint8_t *ptr = ram[vram] + off;
	uint8_t *aptr = ram[vram] + aoff;
	for (l = 0; l < 8; l++) {
		for (c = 0; c < 8; c++)
			for (w = 0; w < 32; w++)
				raster_byte(ybase + c * 8 + l, w, *ptr++, *aptr++);
		aptr -= 0x100;
	}
}

static void spectrum_rasterize(void)
{
	raster_block(0, 0x0000, 0x1800);
	raster_block(64, 0x0800, 0x1900);
	raster_block(128, 0x1000, 0x1A00);
}

static void spectrum_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = WIDTH;
	rect.h = HEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, WIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

/*
 *	Keyboard mapping.
 *	TODO:
 */

static SDL_Keycode keyboard[] = {
	SDLK_LSHIFT, SDLK_z, SDLK_x, SDLK_c, SDLK_v,
	SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g,
	SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t,
	SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5,
	SDLK_0, SDLK_9, SDLK_8, SDLK_7, SDLK_6,
	SDLK_p, SDLK_o, SDLK_i, SDLK_u, SDLK_y,
	SDLK_RETURN, SDLK_l, SDLK_k, SDLK_j, SDLK_h,
	SDLK_SPACE, SDLK_RSHIFT, SDLK_m, SDLK_n, SDLK_b
};

static void run_scanlines(unsigned lines, unsigned blank)
{
	unsigned i;
	unsigned n = 224;	/* T States per op */

	blanked = blank;

	if (!blanked)
		drawline = 0;
	/* Run scanlines */
	for (i = 0; i < lines; i++) {
		n = 224 + 224 - Z80ExecuteTStates(&cpu_z80, n);
		if (!blanked)
			drawline++;
	}
	if (ui_event())
		emulator_done = 1;

	if (int_recalc) {
		/* If there is no pending Z80 vector IRQ but we think
		   there now might be one we use the same logic as for
		   reti */
		poll_irq_event();
		/* Clear this after because reti_event may set the
		   flags to indicate there is more happening. We will
		   pick up the next state changes on the reti if so */
		if (!(cpu_z80.IFF1 | cpu_z80.IFF2))
			int_recalc = 0;
	}
}

static void usage(void)
{
	fprintf(stderr, "spectrum: [-f] [-r path] [-d debug] [-A disk] [-B disk]\n"
			"          [-i idedisk] [-I dividerom]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "spectrum.rom";
	char *divpath = "divide.rom";
	char *idepath = NULL;
	char *tapepath = NULL;
	char *patha = NULL;
	char *pathb = NULL;

	while ((opt = getopt(argc, argv, "d:f:r:m:i:I:A:B:")) != -1) {
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
		case 't':
			tapepath = optarg;
			break;
		case 'm':
			mem = atoi(optarg);
			break;
		case 'i':
			idepath = optarg;
			break;
		case 'I':
			divpath = optarg;
			break;
		case 'A':
			patha = optarg;
			break;
		case 'B':
			pathb = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (mem < 16 || mem > 48) {
		fprintf(stderr, "spectrum: base memory %dK is out of range.\n", mem);
		exit(1);
	}

	mem *= 1024;
	mem += 16384;

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	l = read(fd, ram, 0x10000);
	switch(l) {
	case 0x4000:
		break;
	case 0x8000:
		model = ZX_128K;
		break;
	case 0x10000:
		model = ZX_PLUS3;
		break;
	default:
		fprintf(stderr, "spectrum: invalid rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (model == ZX_PLUS3) {
		fdc = fdc_new();

		lib765_register_error_function(fdc_log);

		if (patha) {
			drive_a = fd_newdsk();
			fd_settype(drive_a, FD_30);
			fd_setheads(drive_a, 1);
			fd_setcyls(drive_a, 40);
			fdd_setfilename(drive_a, patha);
			printf("Attached disk '%s' as A\n", patha);
		} else
			drive_a = fd_new();

		if (pathb) {
			drive_b = fd_newdsk();
			fd_settype(drive_a, FD_35);
			fd_setheads(drive_a, 2);
			fd_setcyls(drive_a, 80);
			fdd_setfilename(drive_a, pathb);
		} else
			drive_b = fd_new();

		fdc_reset(fdc);
		fdc_setisr(fdc, NULL);

		fdc_setdrive(fdc, 0, drive_a);
		fdc_setdrive(fdc, 1, drive_b);
	}

	if (tapepath) {
		tape = open(tapepath, O_RDONLY);	/* No writes for now just minimal stuff */
		if (tape == -1)
			perror(tapepath);
	}

	if (idepath) {
		ide = ide_allocate("divide0");
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		}
		if (ide_attach(ide, 0, fd) == 0)
			ide_reset_begin(ide);
		else {
			fprintf(stderr, "ide: attach failed.\n");
			exit(1);
		}
		fd = open(divpath, O_RDONLY);
		if (fd == -1) {
			perror(divpath);
			exit(1);
		}
		l = read(fd, divrom, 524288);
		if (l == 8192)
			divide = 1;
		else if (l == 524288)
			divide = 2;
		else {
			fprintf(stderr, "spectrum: divide.rom invalid.\n");
			exit(1);
		}
	}

	ui_init();

	window = SDL_CreateWindow("ZX Spectrum",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  WIDTH,
				  HEIGHT, SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr,
			"spectrum: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr,
			"spectrum: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture =
		SDL_CreateTexture(render,
				  SDL_PIXELFORMAT_ARGB8888,
				  SDL_TEXTUREACCESS_STREAMING,
				  WIDTH, HEIGHT);
	if (texture == NULL) {
		fprintf(stderr,
			"spectrum: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, WIDTH, HEIGHT);

	matrix = keymatrix_create(8, 5, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_add_events(matrix);

	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;	/* 20ms (50Hz frame rate) */

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;
	cpu_z80.trace = z80_trace;

	while (!emulator_done) {
		/* TODO: later machines are 228 bytes a line and slightly
		   different numbers of lines */
		run_scanlines(192, 1);
		/* and border */
		run_scanlines(56, 0);
		spectrum_rasterize();
		spectrum_render();
		Z80INT(&cpu_z80, 0xFF);
		/* 64 scan lines of 224 T states for vblank etc */
		run_scanlines(64, 0);
		poll_irq_event();
		frames++;
		/* Do a small block of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (fdc)
			fdc_tick(fdc);
	}
	exit(0);
}
