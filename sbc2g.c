/*
 *	SBC-2g-512: Rienk's enhanced version of the Grant Searle design
 *
 *	Platform features
 *
 *	Z80 at 7.372MHz
 *	Zilog SIO/2 at 0x80-0x83
 *	IDE at 0x10-0x17 no high or control access
 *
 *	16K ROM that can be banked out
 *	512K RAM with low 32K banked high common
 *
 *	Optional
 *	1.	Support for timer via SIO hack
 *
 *	I/O is mapped as
 *	0 SIO A data
 *	1 SIO B data
 *	2 SIO A ctrl
 *	3 SIO B ctrl
 *
 *	30 set the bank 0-15 (high memory is always bank 1)
 *	38 write pages out the ROM (forever)
 *
 *	CF is classically mapped at 10-17 as 8bit IDE
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

static uint8_t ram[512 * 1024];
static uint8_t rom[16384];

static uint8_t romen = 1;
static uint8_t fast = 0;
static uint8_t timerhack = 0;
static uint8_t banknum = 0;

static Z80Context cpu_z80;
static struct z80_sio *sio;

/* IRQ source that is live */
static uint8_t live_irq;

#define IRQ_SIO	1

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_BANK	32
#define TRACE_IRQ	64
#define TRACE_CPU	128

static int trace = 0;

static void reti_event(void);

static uint8_t do_mem_read(uint16_t addr, unsigned quiet)
{
	static uint8_t rstate;
	uint8_t r;

	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R");
	if (addr < 0x4000 && romen)
		r = rom[addr];
	else if (addr >= 0x8000)
		r = ram[addr];	/* Bank 1 lands correctly */
	else
		r = ram[addr + 32768 * banknum];
	if (quiet)
		return r;

	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);

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

static uint8_t mem_read(int unused, uint16_t addr)
{
	return do_mem_read(addr, 0);
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (addr < 0x4000 && romen) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W %04X : ROM\n", addr);
		return;
	}
	if (addr == 0x138E && banknum == 0)
		fprintf(stderr, "***SCRIBBLE %0X from %04X\n",
			val, cpu_z80.M1);
	if (addr >= 0x8000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "WH %04X -> %02X\n", addr, val);
		ram[addr] = val;
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	ram[addr + banknum * 32768] = val;
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
	if (ide0 == NULL)
		return 0xFF;
	return ide_read8(ide0, addr);
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (ide0)
		ide_write8(ide0, addr, val);
}

/*
 *	ROM control
 */

static void control_rom(uint8_t val)
{
	romen = 0;
	if (trace & TRACE_BANK)
		fprintf(stderr, "ROM paged out.\n");

}

static void ram_select(uint8_t val)
{
	banknum = val & 0x0F;
	if (trace & TRACE_BANK)
		fprintf(stderr, "RAM bank set to %d.\n", banknum);
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
	else if (addr == 0x30)
		ram_select(val);
	else if (addr == 0x38)
		control_rom(val);
	else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
}

static void poll_irq_event(void)
{
	int v = sio_check_im2(sio);
	if (v < 0)
		return;
	live_irq = IRQ_SIO;
	Z80INT(&cpu_z80, v);
}

static void reti_event(void)
{
	sio_reti(sio);
	live_irq = 0;
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
	fprintf(stderr, "sbc2g: [-f] [-b] [-t] [-i path] [-r path] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "sbc2g.rom";
	char *idepath = "sbc2g.cf";

	while ((opt = getopt(argc, argv, "d:i:r:ft")) != -1) {
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
	l = read(fd, rom, 16384);
	if (l < 16384) {
		fprintf(stderr, "sbc2g: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
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
			ui_event();
			/* Do 5ms of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
			/* If there is no pending Z80 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
			poll_irq_event();
		}
		timer_pulse();
	}
	exit(0);
}
