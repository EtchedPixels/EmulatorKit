/*
 *	Platform features
 *
 *	Z80 at 7.372MHz
 *	Zilog SIO/2 at 0x80-0x83
 *	IDE at 0x10-0x17 no high or control access
 *
 *	16K ROM that can be banked out
 *	128K RAM of which 64K is normally accessible
 *
 *	Optional
 *	1.	Support for the 'A16 via SIO' hack using W/RDYB
 *	2.	Support for timer via SIO hack
 *	3.	Support for A16 via ROM_PAGE15 on Tom's SBC
 *
 *	I/O is mapped as
 *	0 SIO A data
 *	1 SIO B data
 *	2 SIO A ctrl
 *	3 SIO B ctrl
 *
 *	CF is classically mapped at 10-17 as 8bit IDE
 *
 *	The ROM disable is at 0x38
 *
 *	If it seems an awful lot like RC2014 then there's a reason for that
 *	because RC2014 draws heavily from it as well as Retrobrew/N8VEM
 *
 *	Tom Szolgya's system is also emulated. That adds ROM banking on
 *	using ports 0x3E and 0x3F (bit 0 of each)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include "libz80/z80.h"
#include "z80dis.h"
#include "ide.h"
#include "event.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "z80sio.h"

static uint8_t ram[131072];
static uint8_t rom[16384 * 4];

static uint8_t romen = 1;
static uint8_t banken = 0;
static uint8_t fast = 0;
static uint8_t timerhack = 0;
static uint8_t bankhack = 0;
static uint8_t tom = 0;

static uint8_t rombank = 0;
static uint8_t rombanken = 0;

static Z80Context cpu_z80;
static struct z80_sio *sio;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_BANK	32
#define TRACE_IRQ	64
#define TRACE_CPU	128
#define TRACE_IDE	256

static int trace = 0;

static void reti_event(void);

static uint8_t do_mem_read(uint16_t addr, unsigned quiet)
{
	uint8_t r;

	if (!(trace & TRACE_MEM))
		quiet = 1;

	if (!quiet)
		fprintf(stderr, "R");
	if (addr < 0x4000 && romen) {
		if (!quiet)
			fprintf(stderr, "R%1d", rombank);
		r = rom[rombank * 0x4000 + addr];
	} else if (banken == 1) {
		if (!quiet)
			fprintf(stderr, "H");
		r = ram[addr + 65536];
	}
	else
		r = ram[addr];
	if (!quiet)
		fprintf(stderr, " %04X <- %02X\n", addr, r);
	return r;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate;
	uint8_t r = do_mem_read(addr, 0);

	/* Look for ED with M1, followed directly by 4D and if so trigger
	   the interrupt chain */
	if (cpu_z80.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r== 0xCB) {
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

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (addr < 0x4000 && romen) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W %04X : ROM %d\n", addr, rombank);
		return;
	}
	if (banken) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "WH %04X -> %02X\n", addr, val);
		ram[addr + 65536] = val;
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	ram[addr] = val;
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

/* Channel is A0, C/D is A1 */
static unsigned sio_port[4] = {
	SIOA_D,
	SIOB_D,
	SIOA_C,
	SIOB_C
};

/* Clock timer hack. 10Hz timer wired to DCD */
static void timer_pulse(void)
{
	static unsigned dcd;
	if (timerhack) {
		sio_set_dcd(sio, 0, dcd ^= 1);
		if (trace & TRACE_SIO)
			fprintf(stderr, "DCD1 is now %s.\n", dcd ? "high" : "low");
	}
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	uint8_t r = ide_read8(ide0, addr);
	if (trace & TRACE_IDE)
		fprintf(stderr, "cf: R %d = %02X\n", addr & 7, r);
	return r;
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IDE)
		fprintf(stderr, "cf: W %d = %02X\n", addr & 7, val);
	ide_write8(ide0, addr, val);
}

/*
 *	ROM control
 */

static void control_rom(uint8_t val)
{
	uint8_t olden = romen;
	if (tom)
		romen = !(val & 1);
	else
		romen = 0;
	if (olden != romen && (trace & TRACE_BANK))
		fprintf(stderr, "ROM enabled %d.\n", romen);

}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x00 && addr <= 0x03)
		return sio_read(sio, sio_port[addr & 3]);
	if (addr >= 0x10 && addr <= 0x17)
		return my_ide_read(addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr >= 0x00 && addr <= 0x03)
		sio_write(sio, sio_port[addr & 3], val);
	else if (addr >= 0x10 && addr <= 0x17)
		my_ide_write(addr & 7, val);
	else if (addr == 0x38)
		control_rom(val);
	else if (addr == 0x3E) {
		if (bankhack == 2) {
			banken = val & 1;
			if (trace & TRACE_BANK)
				fprintf(stderr, "RAM A16 now %d.\n", banken);
		} else if (rombanken) {
			rombank &= 1;
			rombank |= (val & 1) ? 2 : 0;
			if (trace & TRACE_BANK)
				fprintf(stderr, "rombank now %d.\n", rombank);
		}
	} else if (addr == 0x3F && rombanken) {
		rombank &= 2;
		rombank |= (val & 1);
		if (trace & TRACE_BANK)
			fprintf(stderr, "rombank now %d.\n", rombank);
	} else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
}

static void poll_irq_event(void)
{
	int v = sio_check_im2(sio);
	if (v < 0)
		return;
	Z80INT(&cpu_z80, v);
}

static void reti_event(void)
{
	sio_reti(sio);
	poll_irq_event();
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
}

static void usage(void)
{
	fprintf(stderr, "searle: [-f] [-b] [-t] [-T] [-i path] [-r path] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "searle.rom";
	char *idepath = "searle.cf";

	while ((opt = getopt(argc, argv, "d:i:r:fbBtT")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 't':
			timerhack = 1;
			break;
		case 'T':
			tom = 1;
			rombanken = 1;
			break;
		case 'b':
			bankhack = 1;
			break;
		case 'B':
			bankhack = 2;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	l = read(fd, rom, 65536);
	if (l < 16384) {
		fprintf(stderr, "searle: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	if (l != 16384 && l != 65536 && l != 32768) {
		fprintf(stderr, "searle: ROM size must be 16K, 32K or 64K.\n");
		exit(EXIT_FAILURE);
	}
	if (l > 16384)
		rombanken = 1;
	if (l == 32768)
		memcpy(rom + 32768, rom ,32768);

	close(fd);

	ide0 = ide_allocate("cf");
	if (ide0) {
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			ide = 0;
		}
		if (ide_attach(ide0, 0, fd) == 0) {
			ide = 1;
			ide_reset_begin(ide0);
		}
	}

	ui_init();

	sio = sio_create();
	sio_reset(sio);
	sio_trace(sio, 0, !!(trace & TRACE_SIO));
	sio_trace(sio, 1, !!(trace & TRACE_SIO));
	sio_attach(sio, 0, &console);
	sio_attach(sio, 1, &console_wo);

	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;

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

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		int l;
		for (l = 0; l < 10; l++) {
			int i;
			/* 36400 T states */
			for (i = 0; i < 100; i++) {
				Z80ExecuteTStates(&cpu_z80, 364);
				sio_timer(sio);
			}
			if (ui_event())
				done = 1;
			/* Do 5ms of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
			poll_irq_event();
		}
		timer_pulse();
	}
	exit(0);
}
