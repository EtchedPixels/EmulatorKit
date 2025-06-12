/*
 *	Very approximate VZ300
 *
 *	CMOS Z80 at 3.58MHz (1 wait state)
 *	6847 video but limited to 2K RAM and modes limtied
 *	Matrix keyboard
 *	RAM and ROM
 *	Tape (not emulated)
 *	Optional SDLoader add on
 *	Optional 8K Australian style video mod
 *
 *	The video timing is something like (NTSC)
 *
 *	\FS edge
 *	26 lines of border
 *	6 lines of vertical retrace
 *	13 lines of blanking
 *	25 lines of top border
 *	192 lines of video
 *				total: 262 lines (NTSC). Need PAL info
 *	227 clocks per line.
 *	or about 15890 clocks from interrupt top until you get into video
 *	contention space
 *
 *	With SDLoader the memory map in total is
 *
 *	0000-3FFF	ROM
 *	4000-67FF	DOS ROM, RAM 0, RAM 1
 *	6800-6FFF	Keyboard in, output latch
 *	7000-77FF	Video memory (2K)
 *	7800-8FFF	Expansion memory
 *	9000-FFFF	Expansion memory (banked on SDLoader)
 *
 *	There are other expansion carts which window C000-FFFF and
 *	video expansions that window 7000-77FF with an 8K RAM for extended
 *	video.
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

#include "6847.h"
#include "6847_render.h"
#include "event.h"
#include "keymatrix.h"
#include "sdcard.h"

#include "event.h"

#include "libz80/z80.h"
#include "z80dis.h"

struct keymatrix *matrix;
struct m6847 *video;
struct m6847_renderer *render;
struct sdcard *sd;

/* We map into the 128K as bank 0 and 1 as might be expected and then
   use the extra 6K above that for video 1,2,3 with the video mods */
static uint8_t mem[131072 + 6144];
static uint8_t spicfg;
static uint8_t spidat;
static uint8_t bank;
static uint8_t latch;
static uint8_t vzcompat;
static uint8_t gmbits = M6847_GM1;
static unsigned machine = 3;		/* Default to VZ300 */
static unsigned vdcbank = 0;
static unsigned hires = 0;		/* Australian hires mode present */

static Z80Context cpu_z80;

static uint8_t fast;
volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_CPU	0x000004
#define TRACE_KEY	0x000008
#define TRACE_SD	0x000010
#define TRACE_HIRES	0x000020

static int trace = 0;

static unsigned video_line;		/* Scan line of the 262 NTSC we have */

static uint8_t *mmu(uint16_t addr, bool write)
{
	/* Low ROM : fixed */
	if (addr < 0x4000) {
		if (write)
			return NULL;
		return mem + addr;
	}
	if (addr < 0x6800) {
		if (sd == NULL || !(bank & 1)) {
			if (!write)
				return mem + 65536 + (addr & 0x3FFF);	/* Borrow the hole */
			/* Write writes through to the RAM selected */
		}
		if (!(bank & 2))
			return mem + addr;
		return mem + 65536 + addr;

	}
	/* I/O should never get here */
	if (addr < 0x6FFF) {
		fprintf(stderr, "vz300: internal.\n");
		exit(1);
	}
	/* For 7000 to 77FF we should generate noise based upon the cycle
	   position relative to screen if we are outside blanking TODO */
	if (addr < 0x7800) {
		m6847_sparkle(video, video_line, cpu_z80.tstates);
		if (hires == 0 || vdcbank == 0)
			return mem + addr;
		/* Graphics expander mods : we put the extra above 128K in our
		   array. In reality it's a banked 8K RAM where the 2K RAM was */
		return mem + 131072 + (addr & 2047) + 2048 * (vdcbank - 1);
	}
	if (sd) {
		if (addr < ((vzcompat & 1) ? 0xB800 : 0x9000))
			return mem + addr;
		if (bank & 4)
			return mem + 65536 + addr;
		else
			return mem + addr;
	}
	/* Not expanded */
	if (machine == 2 && addr < 0x9000)
		return mem + addr;
	if (machine == 3 && addr < 0xB800)
		return mem + addr;
	return NULL;
}

/*
 *	Keyboard mapping
 *	68FE/FD/FB/F7 etc for the keyboard matrix RAM
 *	6bit wide result with low meaning down
 *
 *      R Q   E **** W T
 *      F A   D CTRL S G
 *      V Z   C SHFT X B
 *      4 1   3 **** 2 5
 *      M SPC , **** . N
 *      7 0   8 -    9 6
 *      U P   I RETN O Y
 *      J ;   K :    L H
 */

static SDL_Keycode keyboard[] = {
	SDLK_t, SDLK_w, 0, SDLK_e, SDLK_q, SDLK_r,
	SDLK_g, SDLK_s, SDLK_LCTRL, SDLK_d, SDLK_a, SDLK_f,
	SDLK_b, SDLK_x, SDLK_LSHIFT, SDLK_c, SDLK_z, SDLK_v,
	SDLK_5, SDLK_2, 0, SDLK_3, SDLK_1, SDLK_4,
	SDLK_n, SDLK_PERIOD, 0, SDLK_COMMA, SDLK_SPACE, SDLK_m,
	SDLK_6, SDLK_9, SDLK_MINUS, SDLK_8, SDLK_0, SDLK_7,
	SDLK_y, SDLK_o, SDLK_RETURN, SDLK_i, SDLK_p, SDLK_u,
	SDLK_h, SDLK_l, SDLK_COLON, SDLK_k, SDLK_AT, SDLK_j
};

/*
 *	Keyboard scanning is handled by the matrix keyboard module
 */
static uint8_t keymatrix(uint8_t addr)
{
	return ~keymatrix_input(matrix, ~(addr & 0xFF));
}

uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t *p;

	/* 1 wait state */
	cpu_z80.tstates++;
	if (addr >= 0x6800 && addr <= 0x6FFF)
		return keymatrix(addr);

	p = mmu(addr, false);
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
	uint8_t *p;

	/* 1 wait state */
	cpu_z80.tstates++;

	if (addr >= 0x6800 && addr <= 0x6FFF) {
		latch = val;
		return;
	}
	p = mmu(addr, true);
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
	uint8_t *p;

	if (addr >= 0x6800 && addr <= 0x6FFF)
		return 0xFF;
	p = mmu(addr, 0);
	if (p == NULL) {
		fprintf(stderr, "??");
		return 0xFF;
	}
	fprintf(stderr, "%02X ", *p);
	nbytes++;
	return *p;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	uint8_t *p;

	if (addr >= 0x6800 && addr <= 0x6FFF)
		return 0xFF;
	p = mmu(addr, 0);
	if (p == NULL)
		return 0xFF;
	return *p;
}

static void vz300_trace(unsigned unused)
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

void io_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t dev = addr & 0xFF;
	if (trace & TRACE_IO)
		fprintf(stderr, "=== OUT %02X, %02X\n", addr & 0xFF, val);
	if (sd != NULL) {
		switch(dev) {
		case 55:
			bank = val;
			break;
		case 56:
			spicfg = val;
			if (spicfg & 2)
				sd_spi_lower_cs(sd);
			else
				sd_spi_raise_cs(sd);
			break;
		case 57:
			/* Really this has timing rules */
			if (trace & TRACE_SD)
				fprintf(stderr, "sd: W %02X R ", val);
			spidat = sd_spi_in(sd, val);
			if (trace & TRACE_SD)
				fprintf(stderr, "%02X\n", spidat);
			break;
		case 58:
			vzcompat = val;
			break;
		}
	}
	/* Australian style high resolution */
	if (dev == 32 && hires == 1) {
		vdcbank = val & 3;
		gmbits = 0;
		if (val & 4)
			gmbits |= M6847_GM0;
		if (val & 8)
			gmbits |= M6847_GM1;
		if (val & 16)
			gmbits |= M6847_GM2;
		if (trace & TRACE_HIRES)
			fprintf(stderr, "6847 GMBITS set to %d, bank to %d.\n",
				(val >> 2) & 7, vdcbank);
	}
	/* German hi-res mod - mono 256x192 only. Unfinished */
	if (dev == 222 && hires == 2)
		vdcbank = val & 3;
}

static uint8_t do_io_read(int unused, uint16_t addr)
{
	uint8_t dev = addr & 0xFF;

	if (sd != NULL) {
		switch(dev) {
		case 57:
			return spidat;
		}
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

/* This is wired the other way to the Tandy MC10. Bit 7 is clear for alpha
   Bit 6 is invert. Takes all sorts I guess. The display in text mode
   is using SG4 for the graphic blocks. In graphics mode it is 128 x 64
   packed pixel mode (CG2) */

uint8_t m6847_video_read(struct m6847 *video, uint16_t addr, uint8_t *cfg)
{
	uint8_t c = mem[0x7000 + (addr & 0x07FF)];
	/* 8K hires mod */
	if (hires && addr > 0x7FF)
		c = mem[131072 + addr - 0x0800];

	if (latch & 0x08)
		return c;

	if (c & 0x80)
		*cfg |= M6847_AS;
	else
		*cfg &= ~M6847_AS;

	if (c & 0x40)
		*cfg |= M6847_INV;
	else
		*cfg &= ~M6847_INV;
	return c;
}

uint8_t m6847_get_config(struct m6847 *video)
{
	uint8_t c = M6847_INV;
	if (latch & 0x10)
		c |= M6847_CSS;
	if (latch & 0x08)
		return c | gmbits | M6847_AG;
	else
		return c | gmbits;
}

uint8_t m6847_font_rom(struct m6847 *video, uint8_t ch, unsigned row)
{
	return 0xFF;
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
	fprintf(stderr, "vz300: [-2] [-3] [-a] [-f] [-r rompath] [-R sdrompath] [-s sdcard] [-d debug]\n");
	exit(EXIT_FAILURE);
}

static void load_rom(const char *rom_path, uint8_t *addr, int len)
{
	int fd = open(rom_path, O_RDONLY);
	if (fd == -1) {
		perror(rom_path);
		exit(EXIT_FAILURE);
	}
	if (read(fd, addr, len) < len) {
		fprintf(stderr, "vz300: bad rom '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	close(fd);
}

static void sd_init(const char *rompath, const char *path)
{
	int fd;
	load_rom(rompath, mem + 65536, 6034);
	sd = sd_create("sd0");
	fd = open(path, O_RDWR);
	if (fd == -1) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	sd_attach(sd, fd);
	sd_trace(sd, !!(trace & TRACE_SD));
	sd_reset(sd);
}

/* VZ files are loaded into the main memory bank always. There are no
   provisions for anything clever here */

static int load_vzfile(const char *path)
{
	int fd = open(path, O_RDONLY);
	static uint8_t hdr[24];
	uint16_t addr;
	if (fd == -1) {
		perror(path);
		return -1;
	}
	if (read(fd, hdr, 24) != 24 || memcmp(hdr, "VZF0", 4) || (hdr[21] & 0xFE) != 0xF0) {
		fprintf(stderr,"%s: not a valid VZ file.\n", path);
		return -1;
	}
	addr = hdr[22] | (hdr[23] << 8);
	printf("Loading \"%.17s\" to 0x%X (type %02X).\n",
		hdr + 4, addr, hdr[21]);
	addr += read(fd, mem + addr, 65536 - addr);
	close(fd);
	if (hdr[21] == 0xF0) {
		mem[0x78A4] = hdr[22];	/* Start of BASIC program */
		mem[0x78A5] = hdr[23];
		mem[0x78F9] = addr;	/* End of BASIC program */
		mem[0x78FA] = addr >> 8;
		mem[0x78FB] = addr;	/* Start of DIMensioned variables */
		mem[0x78FC] = addr >> 8;
		mem[0x78FD] = addr;	/* Interrupt hook pointer */
		mem[0x78FE] = addr >> 8;
	} else {
		mem[0x788E] = hdr[22];	/* Set USR vector */
		mem[0x788F] = hdr[23];
	}
	return 0;
}

static void select_vzfile(void)
{
	char buf[512];
	printf("VZ > ");
	fflush(stdout);
	if (fgets(buf, 512, stdin) == NULL)
		return;
	buf[strlen(buf) - 1] = 0;
	load_vzfile(buf);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	char *rom_path = "vz300.rom";
	char *sdrom_path = "vz300sdload.rom";
	char *sd_path = NULL;
	int tstates_per_line = 227;
	int tstates = 227;

	while ((opt = getopt(argc, argv, "ar:R:d:fs:23")) != -1) {
		switch (opt) {
		case '2':
			machine = 2;
			break;
		case '3':
			machine = 3;
			break;
		case 'a':	/* Australian style hires */
			hires = 1;
			break;
		case 'r':
			rom_path = optarg;
			break;
		case 'R':
			sdrom_path = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 's':
			sd_path = optarg;
			break;
		case 'f':
			fast = 1;
			break;
			break;
		default:
			usage();
		}
	}
	while (optind < argc)
		if (load_vzfile(argv[optind++]) < 0)
			exit(EXIT_FAILURE);

	load_rom(rom_path, mem, 16384);
	memset(mem + 65536, 0xFF, 8192);
	if (sd_path)
		sd_init(sdrom_path, sd_path);

	ui_init();

	matrix = keymatrix_create(8, 6, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_add_events(matrix);

	video = m6847_create(M6847);
	m6847_reset(video);
	render = m6847_renderer_create(video);

	/* 10ms - it's a balance between nice behaviour and simulation
	   smoothness */

	tc.tv_sec = 0;
	tc.tv_nsec = 16666667L;		/* 20ms - 50Hz, 16.67ms - 60Hz */

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
	cpu_z80.trace = vz300_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* For the moment these are NTSC timings. Need to add PAL machines. We
	   don't do line by line rastering at this point. We do need to do sparkle
	   computation eventually */
	while (!emulator_done) {
		int i;
		/* Roughly right - need to tweak this to get 50Hz and the
		   right speed plus 1 wait state */
		for (i = 0; i < 262; i++) {
			if (i == 0) {
				m6847_rasterize(video);
				Z80INT(&cpu_z80, 0xFF);
			}
			video_line = i;
			/* Keep track of the odd cycles from instructions going
			   over the 227 */
			tstates += tstates_per_line;
			tstates -= Z80ExecuteTStates(&cpu_z80, tstates);
		}
		/* We are just about to go back into blank which means we've
		   sparklified the raster image nicely ready to draw */
		/* We want to run UI events before we rasterize */
		if (ui_event())
			Z80NMI(&cpu_z80);

		m6847_render(render);
		/* Do 16.66ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (check_chario() & 1) {
			next_char();
			tcsetattr(0, TCSADRAIN, &saved_term);
			select_vzfile();
			tcsetattr(0, TCSADRAIN, &term);
		}
	}
	exit(0);
}
