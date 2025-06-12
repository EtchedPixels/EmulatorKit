/*
 *	Exidy Sorceror
 *	DP1000-1	- Sorceror	8K as shipped, 32K max on board
 *	DP1000-2	- Sorceror II	16K as shipped, 48K max
 *	DP1000-3	- Sorceror II	32K as shipped, 48K max
 *	DP1000-4	- Sorceror II	48K as shipped, 48K max
 *	DP1000-4	- Dynasty 'smart-ALEC' (rebrand)
 *
 *
 *	2.106MHz Z80
 *	8-56K RAM		Beyond C000 is special expansions only
 *	Keyboard
 *	Video
 *	Monitor ROM
 *	Optional 2/4/8K ROMPAC
 *	tape load
 *	ESGG  EXRAM		port 0x7F, 0-15 banks (0 - base)
 *				switches between 16 x 48K banks max (may be less, and may
 *				be 0, 4, 8, 12 if using 64K chips)
 *
 *	TODO:
 *	S100 expansion (and banked RAM etc)
 *		S100 hard disk (PPIDE @0x30-0x33), (Z100lifeline ?)
 *		S100 style banked RAM ?
 *		S100 timer tick (Mountain clock card ?)
 *	Floppy disk interface (Dreamdisk style is simplest)
 *	Centronics and other devices on the 8bit I/O port
 *	80CC (6545 at 0xEC/0xED giving 80x24 or 64x30 col support)
 *
 *	Might want to emulate video available a bit better - but does anything care ?
 *	Sparklies (will need GETIY timing)
 *	Wait states ?
 *	Disk systems
 *	- Dreamdisk
 *		Australian unit very close to MicroBee one. WD controller CPU driven plus a
 *		latch. Replacement monitor ROM (SCUAMON)
 *		Supports some kind of NMI if halted mode. NMI on intrq if in halt
 *		Latch is R/W
 *
 *	- DigiTrio
 *		D800-DFFF ROM
 *		Can work with RAM in ROMPAC C000-D7FF
 *		WD1793 + Z80DMA supports 8" to DSDD and 5.25" to DSDD
 *		I/O 30-33: WD1793, 34: flags/control 38: Z80DMA
 *	- DPS6300
 *		Seems to be an SD hard sectored low capacity floppy using 28 2A 2C
 *		Uses BC00-BFFF
 *	- DPS6400/6500 5.25 / 8" 1.2MB forms
 *
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
#include "wd17xx.h"
#include "drivewire.h"
#include "ide.h"
#include "ppide.h"

#include <SDL2/SDL.h>
#include "event.h"
#include "keymatrix.h"

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;

#define CWIDTH	8
#define CHEIGHT	8
#define COLS	64
#define ROWS	30

static uint32_t texturebits[ROWS * COLS * CWIDTH * CHEIGHT];

/* Keep all the memory in one map for now. Banking S100 stuff and the weird disk bits will
   probably need to change this */
static uint8_t ram[65536];
static uint8_t exram_ram[16][49152];
static struct keymatrix *matrix;
static uint8_t misc_out;
static unsigned romlatch;
static struct wd17xx *fdc;
static struct ppide *ppide;	/* Modern PPIDE card on S100 bus @ 0x30 */
static Z80Context cpu_z80;
static unsigned exram;		/* EXRAM present ? */
static uint8_t exram_latch;
static uint8_t fdc_latch;

static int tape = -1;		/* Tape file handle */
static unsigned rompac;		/* True of a ROMPAC is inserted */
static unsigned mem = 8;	/* First byte above RAM (defaults to 8K) */

static volatile int emulator_done;
static unsigned fast;
static unsigned int_recalc;
static unsigned live_irq;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_FDC	4
#define TRACE_IRQ	16
#define TRACE_CPU	32
#define TRACE_KEY	64

static int trace = 0;

static void reti_event(void);

static int check_chario(void)
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
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

static unsigned int next_char(void)
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

#if 0
static void recalc_interrupts(void)
{
	int_recalc = 1;
}
#endif

static uint8_t do_mem_read(uint16_t addr, unsigned debug)
{
	/* ROM map initially. Once Exxx is referenced the ROM jumps */
	if (romlatch)
		addr = 0xE000 + (addr & 0x0FFF);
	if (exram && addr < 0xC000)
		return exram_ram[exram_latch & 0x0F][addr];
	return ram[addr];
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	/* Cartridge and ROM are R/O. It's more complex with some addons but we
	   can tackle that later */
	if (rompac && addr >= 0xC000 && addr < 0xF000)
		return;
	if (addr >= mem && addr < 0xF000)
		return;
	/* Characters 0-127 are a PROM */
	if (addr >= 0xF800 && addr < 0xFC00)
		return;
	if (exram && addr < 0xC000)
		exram_ram[exram_latch & 0x0F][addr] = val;
	else
		ram[addr] = val;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate;
	uint8_t r = do_mem_read(addr, 0);

	/* Look for ED with M1, followed directly by 4D and if so trigger
	   the interrupt chain */
	if (cpu_z80.M1) {
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
	if ((addr & 0xF000) == 0xE000)
		romlatch = 0;
	return r;
}

/* Parallel emulation. This could be used for many things including sound and joysticks */
static uint8_t par_rd;
static uint8_t par_wr;

/* Platform provided callback when there is data ready for the client */
void drivewire_byte_pending(void)
{
	par_rd = 1;
}

void drivewire_byte_read(void)
{
	par_wr = 0;
}


static void parallel_out(uint8_t c)
{
	/* For centronics D0-D6 are the bits, D7 is the strobe. The handshake is not used */
	/* Can also be one bit sound or a covox */
	/* For now we've hardcoded drivewire */
	drivewire_rx(c);
}

static uint8_t parallel_in(void)
{
	return drivewire_tx();
}

static uint8_t io_read(int unused, uint16_t addr)
{
	uint8_t r = 0xFF;
	switch (addr & 0xFF) {
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
		if (ppide)
			r = ppide_read(ppide, addr & 3);
		break;
	case 0x44:	/* Dreamdisk */
		if (fdc)
			r = wd17xx_status(fdc);
		break;
	case 0x45:
		if (fdc)
			r =wd17xx_read_track(fdc);
		break;
	case 0x46:
		if (fdc)
			r = wd17xx_read_sector(fdc);
		break;
	case 0x47:
		if (fdc)
			r = wd17xx_read_data(fdc);
		break;
	case 0x48:	/* Latch */
	case 0x49:
	case 0x4A:
	case 0x4B:
		if (fdc)
			r = fdc_latch;
		break;
	case 0xFC:		/* Serial I/O */
		if (!(misc_out & 0x80)) {	/* Tapes */
			if (tape != -1) {
				if (read(tape, &r, 1) == 1)
					break;
			}
			r = 0x00;
			break;
		}
		r = next_char();
		break;
	case 0xFD:		/* Serial Status */
		if (!(misc_out & 0x80))	/* Tapes */
			r = 3;
		else
			r = check_chario();
		break;
	case 0xFE:		/* Misc */
		r = ~keymatrix_input(matrix, 1 << (misc_out & 0x0F));
		r &= 0x1F;
		r |= 0x20;	/* Screen ready */
		if (par_rd)	/* Parallel byte waiting */
			r |= 0x40;
		if (par_wr == 0) /* No parallel byte queued for output */
			r |= 0x80;
		break;
	case 0xFF:		/* Parallel I/O (not centronics as such) */
		r = parallel_in();
		par_rd = 0;
		break;
	default:
		fprintf(stderr, "unknown I/O read %02X\n", addr & 0xFF);
		return 0xFF;
	}
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x -> %02X\n", addr, r);
	return r;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	switch (addr & 0xFF) {
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
		if (ppide)
			ppide_write(ppide, addr & 3, val);
		break;
	case 0x44:
		if (fdc)
			wd17xx_command(fdc, val);
		break;
	case 0x45:
		if (fdc)
			wd17xx_write_track(fdc, val);
		break;
	case 0x46:
		if (fdc)
			wd17xx_write_sector(fdc, val);
		break;
	case 0x47:
		if (fdc)
			wd17xx_write_data(fdc, val);
		break;
	case 0x48:
	case 0x49:
	case 0x4A:
	case 0x4B:
		if (fdc == NULL)
			break;
		switch(val & 0x0F) {
		case 0:
			wd17xx_no_drive(fdc);
			return;
		case 1:
			wd17xx_set_drive(fdc, 0);
			break;
		case 2:
			wd17xx_set_drive(fdc, 1);
			break;
		case 4:
			wd17xx_set_drive(fdc, 2);
			break;
		case 8:
			wd17xx_set_drive(fdc, 3);
			break;
		default:
			fprintf(stderr, "fdc: invalid drive select %x\n", val & 0x0F);
			break;
		}
		wd17xx_set_side(fdc, !!(val & 0x10));
		wd17xx_set_density(fdc, (val & 0x20) ? DEN_DD : DEN_SD);
		/* 6 is ENMF ? */
		fdc_latch = val;
		break;
	case 0x7F:	/* EGG EXRAM */
		exram_latch = val;
		return;
	case 0xFC:
		write(1, &val, 1);
		return;
	case 0xFD:
		/* Serial setup - whatever */
		return;
	case 0xFE:
		/* Control */
		misc_out = val;
		return;
	case 0xFF:
		par_wr = 1;
		parallel_out(val);
		return;
	default:
		if (trace & TRACE_IO)
			fprintf(stderr,
				"Unknown write to port %04X of %02X\n",
				addr, val);
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
#if 0
	if (0)
		Z80INT(&cpu_z80, 0xFE);
	else
		Z80NOINT(&cpu_z80);
#endif
}

static void reti_event(void)
{
	live_irq = 0;
	poll_irq_event();
}

static void raster_char(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp = ram + 0xF800 + CHEIGHT * c;
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + COLS * CWIDTH * y * CHEIGHT;

	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp++;
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

static void sorc_rasterize(void)
{
	unsigned lines, cols;
	unsigned ptr = 0xF080;
	for (lines = 0; lines < ROWS; lines++) {
		for (cols = 0; cols < COLS; cols++) {
			raster_char(lines, cols, ram[ptr]);
			ptr++;
		}
	}
}

static void sorc_render(void)
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

/*
 *	Keyboard mapping. The Sorceror has a mux to give 16 x 5 keyboard map
 */

static SDL_Keycode keyboard[] = {
	SDLK_PAUSE /* STOP */, SDLK_LALT /* GRAPHIC */, SDLK_LCTRL, SDLK_CAPSLOCK, SDLK_LSHIFT,
	SDLK_HOME /* CLEAR */, 0 /* REPEAT */, SDLK_SPACE, SDLK_END /* SKIP */, SDLK_INSERT /* SEL */,
	SDLK_x, SDLK_z, SDLK_a, SDLK_q, SDLK_1,
	SDLK_c, SDLK_d, SDLK_s, SDLK_w, SDLK_2,
	SDLK_f, SDLK_r, SDLK_e, SDLK_4, SDLK_3,
	SDLK_b, SDLK_v, SDLK_g, SDLK_t, SDLK_5,
	SDLK_m, SDLK_n, SDLK_h, SDLK_y, SDLK_6,
	SDLK_k, SDLK_i, SDLK_j, SDLK_u, SDLK_7,
	SDLK_COMMA, SDLK_l, SDLK_o, SDLK_9, SDLK_8,
	SDLK_SLASH, SDLK_PERIOD, SDLK_SEMICOLON, SDLK_p, SDLK_0,
	SDLK_BACKSLASH, SDLK_QUOTE /* For @ */, SDLK_RIGHTBRACKET, SDLK_LEFTBRACKET, SDLK_COLON,
	SDLK_UNDERSCORE, SDLK_RETURN, 0 /* LINEFEED */, SDLK_CARET, SDLK_MINUS,
	/* Keymapd area */
	SDLK_KP_PLUS, SDLK_KP_MULTIPLY, SDLK_KP_DIVIDE, SDLK_KP_MINUS, 0 /* unused */,
	SDLK_KP_0, SDLK_KP_1, SDLK_KP_4, SDLK_KP_8, SDLK_KP_7,
	SDLK_KP_PERIOD, SDLK_KP_2, SDLK_KP_5, SDLK_KP_6, SDLK_KP_9,
	0, 0, 0 /* 3 unused */, SDLK_KP_ENTER /* Keyppad = */, SDLK_KP_3
};

/* Most PC layouts don't have a colon key so use # */
static void keytranslate(SDL_Event *ev)
{
	SDL_Keycode c = ev->key.keysym.sym;
	switch (c) {
	case SDLK_HASH:
		c = SDLK_COLON;
		break;
	case SDLK_RSHIFT:
		c = SDLK_LSHIFT;
		break;
	case SDLK_RCTRL:
		c = SDLK_LCTRL;
		break;
	case SDLK_RALT:
		c = SDLK_LALT;
		break;
	case SDLK_BACKQUOTE:
		c = SDLK_UNDERSCORE;
		break;
	}
	ev->key.keysym.sym = c;
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
	SDL_Quit();
}

static void usage(void)
{
	fprintf(stderr, "sorceror: [-f] [-r path] [-d debug]\n");
	exit(EXIT_FAILURE);
}

struct diskgeom {
	const char *name;
	unsigned int size;
	unsigned int sides;
	unsigned int tracks;
	unsigned int spt;
	unsigned int secsize;
	unsigned int sector0;
};

struct diskgeom disktypes[] = {
	/* Exidy Sorceror CP/M */
	{ "CP/M 5.25\" 77T", 315392, 1, 77, 16, 256, 0 },
	{ NULL, }
};

static struct diskgeom *guess_format(const char *path)
{
	struct diskgeom *d = disktypes;
	struct stat s;
	off_t size;
	if (stat(path, &s) == -1) {
		perror(path);
		exit(1);
	}
	size = s.st_size;
	while (d->name) {
		if (d->size == size)
			return d;
		d++;
	}
	fprintf(stderr, "max80: unknown disk format size %ld.\n", (long) size);
	exit(1);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	unsigned cycles = 421;	/* 2.106MHz */
	int opt;
	int fd;
	int l;
	int i;
	char *rompath = "sorc_mon.rom";
	char *fontpath = "sorc_font.rom";
	char *pacpath = NULL;
	char *tapepath = NULL;
	char *fdc_path[4] = { NULL, NULL, NULL, NULL };
	char *idepath = NULL;
	char *wirepath = NULL;

	while ((opt = getopt(argc, argv, "d:efp:r:t:m:A:B:C:D:4I:w:")) != -1) {
		switch (opt) {
		case 'p':
			pacpath = optarg;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'e':
			exram = 1;
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
		case 'A':
			fdc_path[0] = optarg;
			break;
		case 'B':
			fdc_path[1] = optarg;
			break;
		case 'C':
			fdc_path[2] = optarg;
			break;
		case 'D':
			fdc_path[3] = optarg;
			break;
		case '4':
			/* 4MHz: late machines and mods */
			cycles = 800;
			break;
		case 'I':
			idepath = optarg;
			break;
		case 'w':
			wirepath = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (mem < 4 || mem > 56) {
		fprintf(stderr, "sorceror: base memory %dK is out of range.\n", mem);
		exit(1);
	}
	mem *= 1024;

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	l = read(fd, ram + 0xE000, 4096);
	if (l < 4096) {
		fprintf(stderr, "sorceror: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	fd = open(fontpath, O_RDONLY);
	if (fd == -1) {
		perror(fontpath);
		exit(EXIT_FAILURE);
	}
	l = read(fd, ram + 0xF800, 1024);
	if (l < 1024) {
		fprintf(stderr, "sorceror: short rom '%s'.\n", fontpath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (pacpath) {
		fd = open(pacpath, O_RDONLY);
		if (fd == -1) {
			perror(fontpath);
			exit(EXIT_FAILURE);
		}
		l = read(fd, ram + 0xC000, 8192);
		if (l < 2048) {
			fprintf(stderr, "sorceror: short rom '%s'.\n", pacpath);
			exit(EXIT_FAILURE);
		}
		close(fd);
		/* Fake cartridge decode (it's ROM so this is fine) */
		if (l == 2048)
			memcpy(ram + 0xC800, ram + 0xC000, 2048);
		if (l == 2048 || l == 4096)
			memcpy(ram + 0xD000, ram + 0xC000, 4096);
		rompac = 1;
	}

	if (tapepath) {
		tape = open(tapepath, O_RDONLY);	/* No writes for now just minimal stuff */
		if (tape == -1)
			perror(tapepath);
	}

	for (i = 0; i < 4; i++) {
		if (fdc_path[i]) {
			struct diskgeom *d = guess_format(fdc_path[i]);
			if (fdc == NULL)
				fdc = wd17xx_create(2793);
			printf("[Drive %c, %s.]\n", 'A' + i, d->name);
			wd17xx_attach(fdc, i, fdc_path[i], d->sides, d->tracks, d->spt, d->secsize);
			wd17xx_set_sector0(fdc, i, d->sector0);
			/* Double density; required for now TODO */
			wd17xx_set_media_density(fdc, i, DEN_DD);
		}
	}
	if (fdc)
		wd17xx_trace(fdc, trace & TRACE_FDC);

	if (idepath) {
		ppide = ppide_create("ppi0");
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		}
		ppide_reset(ppide);
		ppide_attach(ppide, 0, fd);
	}

	drivewire_init();
	if (wirepath)
		drivewire_attach(0, wirepath, 0);

	ui_init();

	window = SDL_CreateWindow("Exidy Sorceror",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  COLS * CWIDTH,
				  ROWS * CHEIGHT, SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr,
			"sorceror: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr,
			"sorceror: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture =
		SDL_CreateTexture(render,
				  SDL_PIXELFORMAT_ARGB8888,
				  SDL_TEXTUREACCESS_STREAMING,
				  COLS * CWIDTH, ROWS * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr,
			"sorceror: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, COLS * CWIDTH, ROWS * CHEIGHT);

	matrix = keymatrix_create(16, 5, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_add_events(matrix);
	keymatrix_translator(matrix, keytranslate);

	/* TODO */
	tc.tv_sec = 0;
	tc.tv_nsec = 2000000L;	/* 2ms */

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
	cpu_z80.trace = z80_trace;

	romlatch = 1;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* 2MHz processor */
	while (!emulator_done) {
		int l;
		for (l = 0; l < 10; l++) {
			int i;
			for (i = 0; i < 10; i++)
				Z80ExecuteTStates(&cpu_z80, cycles);
			ui_event();
			/* Do a small block of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
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
		/* 50 Hz refresh */
		if (fdc)
			wd17xx_tick(fdc, 20);
		sorc_rasterize();
		sorc_render();
		poll_irq_event();
	}
	exit(0);
}
