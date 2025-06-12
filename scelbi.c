/*
 *	SCELBI-8H
 *	8008 CPU at 500KHz
 *	Data buffer and input card (6 8bit input ports 0-5)
 *	Output card (8 8bit output ports 10-17)
 *	Bit bang serial on port 005/016 using the input/output cards
 *	1K SRAM (1101s) epandable to 4K SRAM
 *	No ROM
 *
 *	SCELBI-8B
 *	8008 CPU at 500KHz
 *	Data buffer and input card as above
 *	Output card as above
 *	Option for second output card as 20-27
 *	Bit bang serial on port 005/016 using the input/output cards
 *	4K SRAM (2102s) expandable to 16K RAM maximum
 *
 *
 *	The serial is emulated by knowing the pattern of IN instructions
 *	and OUT instructions used by the software. It's not strictly correct
 *	but works for pretty much everything and avoids fretting about
 *	baud rates and the like.
 *
 *	The front panel emulation is replaced by a simple command line
 *	interface.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <time.h>

#include "i8008.h"
#include "dgvideo.h"
#include "dgvideo_render.h"
#include "scopewriter.h"
#include "scopewriter_render.h"
#include "event.h"
#include "asciikbd.h"

static struct i8008 *cpu;
static struct dgvideo *dgvideo;
static struct dgvideo_renderer *dgrender;
static struct scopewriter *sw;
static struct scopewriter_renderer *swrender;
static struct asciikbd *kbd;

/* We need this outside the main loop for noise emulation */
static unsigned int cycle = 0;

static uint8_t memory[16384];
static uint8_t memflags[64];	/* Which pages can be read or written */
#define MF_READ		1
#define MF_WRITE	2

static struct termios saved_term, term;

static int fast;

uint8_t mem_read(struct i8008 *cpu, uint16_t addr, unsigned int dbg)
{
	uint8_t bank = (addr >> 8) & 0x3F;
	if (memflags[bank] & MF_READ)
		return memory[addr];
	return 0xff;
}

void mem_write(struct i8008 *cpu, uint16_t addr, uint8_t val)
{
	uint8_t bank = (addr >> 8) & 0x3f;
	if (memflags[bank] & MF_WRITE)
		memory[addr] = val;
}

/*
 *	Typical machine I/O configurations
 *
 *	Intel SIM8-01 (bit bang)
 *		port 0 bit 0, raw sample of the serial input line
 *		port 12 bit 0, serial output driver
 *		port 13 bit 0, turns on and off the tape
 *
 *	Intel Intellec (has a UART)
 *		port 0 teletype data in (8 bit)
 *		port 1 teletype status in
 *               0: data ready
 *               1: overrun
 *               2: transmit buffer empty
 *               3: framing error
 *               4: parity error (inhibited)
 *               5: data available (tape)
 *               6: punch ready
 *		port 8 teletype data out (8 bit)
 *		port 9 teletype control
 *                0: reader advance
 *                1: punch command
 *                2: reader command
 *                3: data out enable
 *                4: data in
 *                5: data out
 *                6: R/_W
 *                7: R/_W A
 *              (PROM programmer or punch)
 *              port A  Prom addr in 7-0
 *              port B Prom data in 7-0 | punch data 7-0
 *
 *
 */

/* Implementation for emulation of dumb serial port. We umm cheat a bit
   and "know" the I/O pattern that will be used rather than trying to
   reverse timing */

void io_write(struct i8008 *cpu, uint8_t port, uint8_t val)
{
	static int serop = 0;
	static uint8_t serbyte = 0;

	if (port == 014) {
		asciikbd_ack(kbd);
		return;
	}
	if (port == 010) {
		if (sw) {
			scopewriter_switches(sw, SW_RD, val & 1);
			return;
		}
	}
	if (port == 011) {
		if (sw) {
			scopewriter_switches(sw, SW_LOAD, val & 1);
			return;
		}
	}
	if (port == 012) {
		if (sw) {
			scopewriter_switches(sw, SW_PB, val & 1);
			return;
		}
	}
	if (port == 013) {
		if (sw) {
			scopewriter_write(sw, val);
			return;
		}
	}
	if (port == 017) {
		if (dgvideo) {
			dgvideo_write(dgvideo, val);
			dgvideo_noise(dgvideo, cycle * 2500 + i8008_get_cycles(cpu), val);
			return;
		}
	}
	if (port != 016) {
		fprintf(stderr, "Port %o = %o\n", port,
			(unsigned int) val);
		return;
	}
	/*printf("%04x: OUT 016: serop %d, serbyte %d, val %d\n", reg_pc,
	   serop, serbyte, val); */
	if (serop == 0) {
		serop++;	/* Start bit */
		serbyte = 0;
		return;
	}
	if (serop == 9) {	/* Stop bit */
		serop = 0;
		serbyte &= 0x7F;
		fprintf(stderr, "%c", serbyte & 0x7f);
		if (serbyte == '\r')
			fprintf(stderr, "\n");
		return;
	}
	if (val & 1)
		serbyte |= 1 << (serop - 1);
	serop++;
}

uint8_t io_read(struct i8008 *cpu, uint8_t port)
{
	char c;
	static int serop = 0;
	static uint8_t serbyte = 0;
	uint8_t v;

	if (port == 4) {
		if (asciikbd_ready(kbd)) {
			v = asciikbd_read(kbd) | 0x80;
			return v;
		}
		return 0;
	}
	if (port != 5) {
		fprintf(stderr, "Port %o read\n", port);
		return 0377;
	}

	if (serop == 0) {
		/* Serial stays high until a start bit */
		if (read(0, &c, 1) == 0)
			return 0x80;
		serbyte = c | 0x80;
		serop++;
		return 0;	/* Look a start bit .. */
	}
	v = (serbyte & (1 << (serop - 1))) ? 0x80 : 0;
	serop++;
	if (serop == 9) {
		serop = 0;
	}
	return v;
}

static void machine_halted(void)
{
	char buf[256];
	uint16_t breakpt;

retry:
	printf("H.");
	fgets(buf, 256, stdin);
	if (*buf == '?' || *buf == 'h') {
		printf("\n"
"b oooo      - set breakpoint\n"
"c           - resume execution\n"
"g oooo oooo - dump memory start length\n"
"h           - this help\n"
"i oo oo oo  - jam an instruction into the machine\n"
"q           - quit emulation\n"
"S           - single step on\n"
"s           - single step off\n"
"T           - trace on\n"
"t           - trace off\n"
"w oooo oo   - write a memory location\n"
"x oooo      - jam a jump to oooo into the machine\n");
		return;
	}

	if (*buf == 'r') {
		i8008_dump(cpu);
		return;
	}
	if (*buf == 'c' || *buf == '\n') {
		i8008_resume(cpu);
		return;
	}
	if (*buf == 'b') {
		if (sscanf(buf, "b %ho", &breakpt) == 0) {
			i8008_breakpoint(cpu, 0xFFFF);
			printf("cleared.\n");
		} else {
			i8008_breakpoint(cpu, breakpt);
			printf("set.\n");
		}
		return;
	}
	if (*buf == 'i') {
		uint8_t jambuf[3];
		int len =
			sscanf(buf, "i %hho %hho %hho", jambuf, jambuf + 1,
			       jambuf + 2);
		i8008_stuff(cpu, jambuf, len);
		return;
	}
	if (*buf == 'w') {
		unsigned int v, d;
		if (sscanf(buf, "w %o,%o", &v, &d) == 2) {
			mem_write(cpu, v, d);
		} else {
			printf("w?\n");
		}
		return;
	}
	if (*buf == 'g') {
		unsigned int v, d;
		if (sscanf(buf, "g %o,%o", &v, &d) == 2) {
			int i = 0;
			for (i = 0; i < d; i++) {
				printf("%o(%d) %o(%d)\n", v + i, v + i,
				       (unsigned int) mem_read(cpu, v + i,
							       1),
				       (unsigned int) mem_read(cpu, v + i,
							       1));
			}
			return;
		}
		printf("g?\n");
		return;
	}
	if (*buf == 'x') {
		uint16_t addr;
		if (sscanf(buf, "x %ho", &addr) == 1) {
			uint8_t buf[3];
			buf[0] = 0104;
			buf[1] = addr & 0xFF;
			buf[2] = addr >> 8;

			i8008_stuff(cpu, buf, 3);
			return;
		}
		printf("x?\n");
		return;
	}
	if (*buf == 's') {
		i8008_singlestep(cpu, 0);
		return;
	}
	if (*buf == 'S') {
		i8008_singlestep(cpu, 1);
		return;
	}
	if (*buf == 't') {
		i8008_trace(cpu, 0);
		return;
	}
	if (*buf == 'T') {
		i8008_trace(cpu, 1);
		return;
	}
	if (*buf == 'q')
		exit(0);
	printf("?\n");
	goto retry;
}

static void intr(int unused)
{
	i8008_halt(cpu, 1);
	signal(SIGINT, intr);
}

static void run_system(void)
{
	static struct timespec tc;
	/* Execute runs code until an interrupt interferes, we then
	   drop into halted state and expect machine_halted to make our
	   decisions and also to sleep when appropriate.

	   Note that the 8008 starts halted */
/* 5ms - it's a balance between nice behaviour and simulation
   smoothness */
	signal(SIGINT, intr);
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;

	cpu = i8008_create();
	i8008_reset(cpu);
	i8008_trace(cpu, 0);
	while (1) {
		i8008_execute(cpu, 2500);	/* 500Khz */
		cycle++;
		/* We do the video rendering every 20ms (50Hz) and the
		   rest of our cycle runs faster so things like window
		   resizing are not laggy */
		if ((cycle & 3) == 3) {
			cycle = 0;
			if (dgrender)
				dgvideo_render(dgrender);
			if (swrender)
				scopewriter_render(swrender);
			if (dgvideo)
				dgvideo_rasterize(dgvideo);
		}
		if (ui_event())
			break;
		nanosleep(&tc, NULL);
		if (i8008_halted(cpu)) {
			tcsetattr(0, TCSADRAIN, &saved_term);
			do {
				if (dgrender)
					dgvideo_render(dgrender);
				if (swrender)
					scopewriter_render(swrender);
				machine_halted();
			} while (i8008_halted(cpu));
			tcsetattr(0, TCSADRAIN, &term);
		}
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	i8008_free(cpu);
}


static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}


void usage(void)
{
	fprintf(stderr,
		"scelbi [-f] [-m kb] [-l load] [-b loadbase] [-r rom] [-v] [-s]\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	FILE *f;
	unsigned int p = 0;
	int i;
	int opt;
	unsigned int memsize = 4;
	uint16_t base;
	const char *loadpath = NULL;
	const char *rompath = NULL;
	unsigned int has_sw = 0, has_dg = 0;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON | ECHO);
		term.c_cc[VMIN] = 0;
		term.c_cc[VTIME] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VSTOP] = 0;
		tcsetattr(0, TCSADRAIN, &term);
	}

	while ((opt = getopt(argc, argv, "fl:m:r:b:sv")) != -1) {
		switch (opt) {
		case 'f':
			fast = 1;
			break;
		case 'm':
			memsize = atoi(optarg);
			if (memsize < 1 || memsize > 16) {
				fprintf(stderr,
					"scelbi: memory range should be 1 to 16K\n");
				exit(1);
			}
			break;
		case 'l':
			loadpath = optarg;
			break;
		case 'b':
			if (sscanf(optarg, "%ho", &base) != 1) {
				fprintf(stderr,
					"scelbi: octal base address required.\n");
				exit(1);
			}
			if (base >= sizeof(memory)) {
				fprintf(stderr,
					"scelbi: load address too high.\n");
				exit(1);
			}
			break;
		case 'r':
			rompath = optarg;
			break;
		case 's':
			has_sw = 1;
			break;
		case 'v':
			has_dg = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	ui_init();

	if (has_dg) {
		dgvideo = dgvideo_create();
		dgrender = dgvideo_renderer_create(dgvideo);
	}
	if (has_sw) {
		sw = scopewriter_create();
		swrender = scopewriter_renderer_create(sw);
	}
	kbd = asciikbd_create();

	/* Memory enables */
	for (i = 0; i < 4 * memsize; i++)
		memflags[i] = MF_READ | MF_WRITE;

	if (loadpath) {
		f = fopen(loadpath, "r");
		if (f == NULL) {
			perror(loadpath);
			exit(1);
		}
		p = fread(memory + base, 1, sizeof(memory) - base, f);
		fclose(f);
		printf("[Loaded %d bytes at %o-%o from %s.]\n", p, base, base + p - 1,
		       loadpath);
	}
	if (rompath) {
		uint16_t l = 14336;
		/* Emulate a 256 byte or 2K EPROM in the top space */
		f = fopen(rompath, "r");
		if (f == NULL) {
			perror(rompath);
			exit(1);
		}
		p = fread(memory + l, 1, 2048, f);
		fclose(f);
		for (i = 4 * 14; i < 4 * 16; i++)
			memflags[i] = MF_READ;
		printf("[Loaded %d bytes at %o from %s.]\n", p, 14336,
		       rompath);
	}
	run_system();
	exit(0);
}
