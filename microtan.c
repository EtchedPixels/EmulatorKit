/*
 *	Microtan 65
 *	6502 CPU at 750KHz (or 1.5MHz with mods)
 *	1K of RAM at 0000-03FF
 *	1K of ROM at FC00-FFFF (F800-FFFF later)
 *	32x16 video using 0200-03FF
 *	Uppercase - lowercase option covering 00-1F/60-7F
 *
 *	Related boards existed without the video and at higher speeds.
 *
 *	TANEX
 *	External buffers for Microtan 65
 *	ROM C000-F7FF	(F000-F7FF is Xbug, C000-E7FFF is usually BASIC
 *			 E800-EFFF extensions ROMs)
 *	RAM 0400-1FFF
 *	2 x VIA		Printing/User (CF adapter)
 *	1 x 6551	Serial console
 *
 *	TANRAM
 *	RAM 2000-BBFF	Pageable RAM banks (64K card equivalents can handle
 *			upper space without ROM if set up right)
 *
 *	TANDOS A800-BBFF RAM/ROM/FDC
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
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "6502.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "6522.h"
#include "6551.h"
#include "event.h"
#include "asciikbd.h"
#include "wd17xx.h"
#include "ide.h"
#include "58174.h"

#define CWIDTH 8
#define CHEIGHT 16

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[32 * CWIDTH * 16 * CHEIGHT];

#define MACH_MICROTAN	1
#define MACH_MICRON	2

static unsigned machine = MACH_MICROTAN;

static uint8_t mem[65536];
static uint8_t paged[16][65536];/* Paged space by board slot */
static uint8_t vgfx[512];	/* Extra bit for video */
static uint8_t font[8192];	/* Chars in 8x16 (repeated twice) then gfx */
static uint8_t dosrom[4096];	/* TANDOS ROM */
static uint8_t dosram[1024];	/* TANDOS RAM */
static uint8_t dosctrl;

static unsigned tanex;		/* Set if we have a tanex */
static unsigned romsize;	/* System ROM size */
static unsigned basic;		/* BASIC (etc) ROM present at C000-E7FF */
static unsigned numpages;	/* Pages of high memory */
static unsigned tandos;		/* TANDOS card present */
static unsigned basic_top;	/* BASIC top of ROM space */

static struct asciikbd *kbd;
static struct via6522 *via1, *via2;
static struct m6551 *uart;
static struct wd17xx *fdc;
static struct ide_controller *ide;
static struct mm58174 *rtc;

static uint8_t fast;
volatile int emulator_done;

static uint8_t mem_be;
static unsigned gfx_bit;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_6551	0x000010
#define TRACE_RTC	0x000020
#define TRACE_FDC	0x000040

static int trace = 0;

static uint8_t tandos_read(uint16_t addr)
{
	unsigned r;
	uint8_t res;
	switch(addr & 0x0F) {
	case 0x00:
		return wd17xx_status(fdc);
	case 0x01:
		return wd17xx_read_track(fdc);
	case 0x02:
		return wd17xx_read_sector(fdc);
	case 0x03:
		return wd17xx_read_data(fdc);
	case 0x04:
		r = wd17xx_status_noclear(fdc);
		res = dosctrl & 0x3E;
		if (r & 0x02)	/* DRQ */
			res |= 0x80;
		if (r & 0x20)	/* HEADLOAD */
			res |= 0x40;
		if (wd17xx_intrq(fdc))
			res |= 0x01;
		return res;
	case 0x05:
	case 0x06:
	case 0x07:
		/* Switches - TODO */
		return 0xFF;
	default:
		/* TMS9914 - model not fitted for now */
		return 0xFF;
	}
	/* Can't get here */
	return 0xFF;
}

static void tandos_write(uint16_t addr, uint8_t val)
{
	switch(addr & 0x0F) {
	case 0x00:
		wd17xx_command(fdc, val);
		break;
	case 0x01:
		wd17xx_write_track(fdc, val);
		break;
	case 0x02:
		wd17xx_write_sector(fdc, val);
		break;
	case 0x03:
		wd17xx_write_data(fdc, val);
	case 0x04:
		/* Control TODO */
		dosctrl = val;
		if (trace & TRACE_FDC)
			fprintf(stderr, "fdc: dosctrl set to %02X\n", dosctrl);
		wd17xx_set_density(fdc, (val & 0x20) ? DEN_SD : DEN_DD);
		wd17xx_set_drive(fdc, (val >> 2) & 0x03);
		/* Not clear how motor timing works ? */
		wd17xx_motor(fdc, 1);
		break;
	case 0x05:
	case 0x06:
	case 0x07:
		/* Switches - RO */
		break;
	default:
		/* TMS9914 */
		break;
	}
}

uint8_t do_read_6502(uint16_t addr, unsigned debug)
{
	/* Everything is mirrored everywhere on the base unit */
	if (!tanex) {
		if (addr <= 0x8000)
			addr &= 0x03FF;
		else if (addr <= 0xC000)
			addr = 0xBFF0 | (addr & 0x0F);
		else {
			if (romsize == 0x0400)
				addr = 0xFC00 | (addr & 0x3FF);
			else
				addr = 0xF800 | (addr & 0x7FF);
		}
	}
	/* Tanex RAM : unpaged */
	if (addr <= 0x2000)
		return mem[addr];
	if (basic && addr >= 0xC000 && addr < basic_top)
		return mem[addr];
	if (tandos) {
		if (addr >= 0xA800 && addr < 0xB800)
			return dosrom[(addr - 0xA800) ^ 0x0800];
		if (addr >= 0xB800 && addr <= 0xBC00)
			return dosram[addr - 0xB800];
	}
	/* TODO Toolkit ROM space */
	/* TODO: might have tanex but no ROM F000-F7FF - need a way to config this */
	if (addr >= 0x10000 - romsize)
		return mem[addr];
	if (addr >= 0xBC00 && addr <= 0xBFFF) {
		if (debug)
			return 0xFF;
		if (tandos && (addr & 0xFFF0) == 0xBF90)
			return tandos_read(addr);
		if ((addr & 0xFFF0) == 0xBFC0)
			return via_read(via1, addr & 0x0F);
		if (uart && (addr & 0xFFF0) == 0xBFD0)
			return m6551_read(uart, addr & 0x03);
		if ((addr & 0xFFF0) == 0xBFE0)
			return via_read(via2, addr & 0x0F);
		if ((addr & 0xFFF0) == 0xBC40)
			return mm58174_read(rtc, addr & 0x0F);
		/* I/O space */
		switch(addr) {
		case 0xBFF0:
			gfx_bit = 1;
			break;
		case 0xBFF3:
			if (machine == MACH_MICROTAN)
				return (asciikbd_read(kbd) & 0x7F) | (asciikbd_ready(kbd) ? 0x80 : 0x00);
			return 0x00;
		}
		return 0xFF;
	}
	/* Paged space */
	if ((mem_be >> 4) < numpages)
		return paged[mem_be >> 4][addr];
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
	if (!tanex) {
		if (addr < 0xBC00)
			addr &= 0x3FF;
		else if (addr <= 0xC000)
			addr = 0xBFF0 | (addr & 0x0F);
		else
			return;
	}
	if (addr <= 0x2000) {
		mem[addr] = val;
		if (addr >= 0x200 && addr <= 0x03FF)
			vgfx[addr & 0x01FF] = gfx_bit;
		return;
	}
	if (basic && addr >= 0xC000 && addr < basic_top)
		return;
	if (addr >= 0xE800) {
		if (addr >= 0xFFF0)
			mem_be = val;
		return;
	}
	if (tandos) {
		if (addr >= 0xA800 && addr < 0xB800)
			return;
		if (addr >= 0xB800 && addr < 0xBC00) {
			dosram[addr - 0xB800] = val;
			return;
		}
	}
	if (tandos && (addr & 0xFFF0) == 0xBF90) {
		tandos_write(addr, val);
		return;
	}
	if ((addr & 0xFFF0) == 0xBFC0) {
		via_write(via1, addr & 0x0F, val);
		return;
	}
	if ((addr & 0xFFF0) == 0xBFD0) {
		if (uart)
			m6551_write(uart, addr & 0x03, val);
		return;
	}
	if ((addr & 0xFFF0) == 0xBC40) {
		mm58174_write(rtc, addr & 0x0F, val);
		return;
	}
	if ((addr & 0xFFF0) == 0xBFE0) {
		via_write(via2, addr & 0x0F, val);
		return;
	}
	switch(addr) {
	case 0xBFF0:
		if (machine == MACH_MICROTAN)
			asciikbd_ack(kbd);
		break;
	case 0xBFF1:
		/* Delayed NMI - not implemented yet */
		break;
	case 0xBFF2:
		/* Set kbd pattern */
	case 0xBFF3:
		gfx_bit = 0;
		break;
	}
	if ((addr < 0xBC00 || addr >= 0xC000) && (mem_be & 0x0F) < numpages) {
		paged[mem_be & 0x0F][addr] = val;
		return;
	}
}

/*
 *	VIA glue
 *
 *	Emulate CF directly attached to the second VIA, (resistors are a good
 *	idea on port B in case of screwups with DDRB)
 *
 *	Emulation doesn't consider DDR and the like properly. TODO
 */

#define IDE_ADDR	0x07
#define	IDE_CS0		0x08
#define	IDE_CS1		0x10	/* Not emulated at this point */
#define IDE_R		0x20
#define IDE_W		0x40
#define IDE_RESET	0x80

void via_recalc_outputs(struct via6522 *via)
{
	static unsigned old_pa;
	unsigned pa, pb, delta;

	if (via == via2 && ide) {
		/* See what is cooking */
		pa = via_get_port_a(via2);
		pb = via_get_port_b(via2);

		delta = pa ^ old_pa;
		if (delta & IDE_RESET) {
			if (!(pa & IDE_RESET))
				ide_reset_begin(ide);
		}
		if (!(pa & IDE_RESET) || (pa & IDE_CS0))
			return;
		if (delta & pa & IDE_W) {
			/* Write rising edge */
			ide_write8(ide, pa & IDE_ADDR, pb);
		}
		if (delta & IDE_R) {
			if (!(pa & IDE_R))
				via_set_port_b(via2, ide_read8(ide, pa & IDE_ADDR));
		}
	}
}

void via_handshake_a(struct via6522 *via)
{
}

void via_handshake_b(struct via6522 *via)
{
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
	if (machine == MACH_MICROTAN && asciikbd_ready(kbd))
		irq6502();
	else if (via1 && via_irq_pending(via1))
		irq6502();
	else if (via2 && via_irq_pending(via2))
		irq6502();
	else if (uart && m6551_irq_pending(uart))
		irq6502();
	else if (tandos && (dosctrl & 0x01) && wd17xx_intrq(fdc))
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

/* Expand the bitmap in the bottom of vgfx into the full table. Do it end
   to start so we don't overwrite anything */
static void bits2chunky(void)
{
	uint8_t *p = vgfx + 31;
	uint8_t *t = vgfx + 511;
	uint8_t c;
	unsigned n;

	while(p > vgfx) {
		c = *p--;
		for (n = 0; n < 8; n++) {
			if (c & 0x80)
				*t-- = 1;
			else
				*t-- = 0;
		}
	}
}

static void m65_block(int fd, uint8_t *p, unsigned size)
{
	if (read(fd, mem, 0x2000) != 0x2000 || read(fd, vgfx, 0x40) != 0x40) {
		fprintf(stderr, "microtan: m65 file too short.\n");
		exit(EXIT_FAILURE);
	}
}

static uint8_t m65_byte(int fd)
{
	uint8_t c;
	m65_block(fd, &c, 1);
	return c;
}

static uint16_t m65_word(int fd)
{
	uint16_t r =  m65_byte(fd);
	r |= m65_byte(fd) << 8;
	return r;
}

static void m65_regs(int fd)
{
	uint8_t buf[SAVE_SIZE];
	m65_block(fd, buf, SAVE_SIZE);
	load6502(buf);
}

static void load_m65(const char *path)
{
	int fd = open(path, O_RDONLY);
	off_t size;
	unsigned n;
	unsigned version;
	unsigned memory;

	if (fd == -1) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	size = lseek(fd, 0, SEEK_END);
	if (size == -1) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	if (size == 8263) {
		/* Legacy snapshot format */
		lseek(fd, 0, SEEK_SET);
		m65_block(fd, mem, 0x2000);
		m65_block(fd, vgfx, 0x40);
		m65_regs(fd);
		close(fd);
		bits2chunky();
		return;
	}
	/* M65 is little endian */
	version = m65_word(fd);
	memory = m65_word(fd);
	/* Load main memory (.m65 has no proper bank support) */
	if (memory <= 0x2000)
		m65_block(fd, mem, memory);
	else {
		m65_block(fd, mem, 0x2000);
		m65_block(fd, &paged[0][0], memory - 0x2000);
	}
	/* Load I/O */
	m65_block(fd, mem + 0xBFC0, 0x30);
	m65_block(fd, mem + 0xBC04, 1);
	gfx_bit = m65_byte(fd);
	/* Dummy for AY-3-8910. We will just overwrit vgfx again in a minute */
	m65_block(fd, vgfx, 32);

	/* V2 suports 4 banks of high res pages but we don't */
	if (version > 1) {
		/* Dummy load 32K */
		m65_block(fd, &paged[1][0x8000], 0x2000);
		m65_block(fd, &paged[2][0x8000], 0x2000);
		m65_block(fd, &paged[3][0x8000], 0x2000);
		m65_block(fd, &paged[4][0x8000], 0x2000);
	}
	m65_block(fd, vgfx, 0x40);
	m65_regs(fd);
	close(fd);

	bits2chunky();

	/* Now load up the devices as best we can */
	for (n = 0xBFC0; n > 0xBFCF; n++)
		write6502(n, mem[n]);
	for (n = 0xBFD0; n > 0xBFD3; n++)
		write6502(n, mem[n]);
	for (n = 0xBFE0; n > 0xBFEF; n++)
		write6502(n, mem[n]);
}

static void usage(void)
{
	fprintf(stderr, "microtan: [-f] [-a] [-m] [-A disk] [-B disk] [-i ide] [-r monitor] [-b basic] [-F font] [-d debug] [snapshot.m65]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	static int tstates = 750;	/* 750KHz */
	int opt;
	char *rom_path = "microtan.rom";
	char *font_path = "microtan.font";
	char *drive_a = NULL;
	char *drive_b = NULL;
	char *basic_path = NULL;
	char *ide_path = NULL;
	char *m65_path = NULL;
	unsigned has_uart = 0;

	while ((opt = getopt(argc, argv, "ab:d:fi:mp:r:A:B:F:")) != -1) {
		switch (opt) {
		case 'a':
			has_uart = 1;
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
		case 'i':
			ide_path = optarg;
			break;
		case 'm':
			/* Faster board without onboard video */
			machine = MACH_MICRON;
			tstates = 1000;	/* 1MHz - could be various options */
			break;
		case 'r':
			rom_path = optarg;
			break;
		case 'p':
			numpages = atoi(optarg);
			if (numpages > 16) {
				fprintf(stderr, "microtan: maximum of 16 banks supported.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'A':
			drive_a = optarg;
			break;
		case 'B':
			drive_b = optarg;
			break;
		case 'F':
			font_path = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind == argc - 1)
		m65_path = argv[optind];
	else if (optind < argc)
		usage();

	romsize = romload(rom_path, mem + 0xF000, 0x1000);
	if (romsize == 0x1000)
		tanex = 1;
	else if (romsize == 0x0800)
		/* Move it up */
		memcpy(mem + 0xF800, mem + 0xF000, 0x0800);
	else if (romsize == 0x0400)
		/* Move it up */
		memcpy(mem + 0xFC00, mem + 0xF000, 0x0400);
	else {
		fprintf(stderr, "microtan: invalid ROM size '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	if (romload(font_path, font, 4096) != 4096) {
		fprintf(stderr, "microtan: invalid font ROM\n");
		exit(EXIT_FAILURE);
	}
	if (basic_path) {
		basic_top = romload(basic_path, mem + 0xC000, 0x2800);
		if (basic_top < 0x1000) {
			fprintf(stderr, "microtan: invalid BASIC ROM\n");
			exit(EXIT_FAILURE);
		}
		basic = 1;
		basic_top = (basic_top + 0x7FF) & 0xF800;
		basic_top += 0xC000;
	}
	make_blockfont();

	/* TODO: the TANDOS boot disk if present should
	   tell us the disk config and memory sizes at least for
	   track count */
	if (drive_a || drive_b) {
		tandos = 1;
		/* TODO: make path settable */
		if (romload("tandos.rom", dosrom, 4096) != 4096) {
			fprintf(stderr, "microtan: invalid TANDOS ROM\n");
			exit(EXIT_FAILURE);
		}
		fdc = wd17xx_create(1791);
		wd17xx_trace(fdc, trace & TRACE_FDC);
		if (drive_a) {
			wd17xx_attach(fdc, 0, drive_a, 1, 40, 10, 256);
			wd17xx_set_media_density(fdc, 0, DEN_SD);
		}
		if (drive_b) {
			wd17xx_attach(fdc, 1, drive_b, 1, 40, 10, 256);
			wd17xx_set_media_density(fdc, 1, DEN_SD);
		}
	}

	if (ide_path) {
		int ide_fd;
		ide = ide_allocate("via2");
		ide_fd = open(ide_path, O_RDWR);
		if (ide_fd == -1) {
			perror(ide_path);
			exit(1);
		}
		ide_attach(ide, 0, ide_fd);
		ide_reset_begin(ide);
	}

	if (machine == MACH_MICROTAN)
		kbd = asciikbd_create();
	via1 = via_create();
	via2 = via_create();
	if (has_uart) {
		uart = m6551_create();
		m6551_attach(uart, &console);
		m6551_trace(uart, trace & TRACE_6551);
	}
	rtc = mm58174_create();
	mm58174_trace(rtc, trace & TRACE_RTC);

	if (machine == MACH_MICROTAN) {
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
	hookexternal(irqnotify);
	reset6502();

	/* Has to be done after CPU init so we can set the registers */
	if (m65_path)
		load_m65(m65_path);
	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int i;
		for (i = 0; i < 10; i++) {
			exec6502(tstates);
			if (tanex) {
				via_tick(via1, tstates);
				via_tick(via2, tstates);
			}
		}
		if (machine == MACH_MICROTAN) {
			/* We want to run UI events before we rasterize */
			if (ui_event())
				break;
			utan_rasterize();
			utan_render();
		}
		mm58174_tick(rtc);
		if (uart)
			m6551_timer(uart);
		if (tandos)
			wd17xx_tick(fdc, 10);
		/* Do 10ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
