/*
 *	Platform features
 *	Z180
 *	2 x 512K RAM
 *	512K pagable in 32K window over the lowest addresses
 *	Retrobrew PPIDE
 *	Retrobrew RTC
 *	8bit control latch
 *
 *	Not modelled
 *
 *	NEC7220 or similar GPU
 *	VT82C42 keyboard/mouse in PS/2 mouse
 *	RAMDAC
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

#include "system.h"
#include "libz180/z180.h"
#include "serialdevice.h"
#include "z180_io.h"

#include "ttycon.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "z80dis.h"

static uint8_t ramrom[512 * 1024 + 1024 * 1024]; 	/* Top 512K is ROM */

static uint8_t fast = 0;
static uint8_t int_recalc = 0;
static int ide = 0;
static struct z180_io *io;
static struct ppide *ppide;
static uint8_t acr;
static struct rtc *rtc;

static uint16_t tstate_steps = 737;	/* 18.432MHz */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

static Z180Context cpu_z180;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_UNK	0x000004
#define TRACE_RTC	0x000008
#define TRACE_CPU	0x000010
#define TRACE_CPU_IO	0x000020
#define TRACE_IRQ	0x000040
#define TRACE_IDE	0x000080

static int trace = 0;

static void reti_event(void);

/*
 *	Describes the interface between the Z180 external bus and the
 *	memory
 */
uint8_t z180_phys_read(int unused, uint32_t addr)
{
	/* Our input is a Z180 bus address */
	addr &= 0xFFFFF;
	if (addr < 0x08000 && !(acr & 0x80)) {
		addr += 0x100000;	/* ROM space */
		addr += (acr & 0x0F) << 15;
	}
	return ramrom[addr];
}

void z180_phys_write(int unused, uint32_t addr, uint8_t val)
{
	addr &= 0xFFFFF;
	if (addr < 0x0800 && !(acr & 0x80))
		return;
	ramrom[addr] = val;
}

/*
 *	Handle the CPU 16 to 20 bit logical mapping
 */
static uint8_t do_mem_read(uint16_t addr, int quiet)
{
	uint32_t pa = z180_mmu_translate(io, addr);
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X[%06X] -> %02X\n", addr, pa, ramrom[pa]);
	return z180_phys_read(0, pa);
}

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	uint32_t pa = z180_mmu_translate(io, addr);
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %04X[%06X] <- %02X\n", addr, pa, val);
	z180_phys_write(0, pa, val);
}

uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r = do_mem_read(addr, 0);

	if (cpu_z180.M1) {
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

static void rhyophyre_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z180.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z180.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z180.R1.br.A, cpu_z180.R1.br.F,
		cpu_z180.R1.wr.BC, cpu_z180.R1.wr.DE, cpu_z180.R1.wr.HL,
		cpu_z180.R1.wr.IX, cpu_z180.R1.wr.IY, cpu_z180.R1.wr.SP);
}

void recalc_interrupts(void)
{
	int_recalc = 1;
}

uint8_t z180_csio_write(struct z180_io *io, uint8_t bits)
{
	return 0xFF;
}

uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if (z180_iospace(io, addr))
		return z180_read(io, addr);

	addr &= 0xFF;
	if (addr >= 0x84 && addr <= 0x87)
		return rtc_read(rtc);
	if (addr >= 0x88 && addr <= 0x8B && ppide)
		return ppide_read(ppide, addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	unsigned int known = 0;

	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);

	if (z180_iospace(io, addr)) {
		z180_write(io, addr, val);
		known = 1;
	}
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x83)
		acr = val;
	else if (addr >= 0x84 && addr <= 0x87)
		rtc_write(rtc, val);
	else if (addr >= 0x88 && addr <= 0x8B && ppide)
		ppide_write(ppide, addr & 7, val);
	else if (addr == 0xFD) {
		trace &= 0xFF00;
		trace |= val;
		printf("trace set to %04X\n", trace);
	} else if (addr == 0xFE) {
		trace &= 0xFF;
		trace |= val << 8;
		printf("trace set to %d\n", trace);
	} else if (!known && (trace & TRACE_UNK))
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
	live_irq = 0;
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
	fprintf(stderr, "rhyophyre: [-f] [-I ppidepath] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "RPH_std.rom";
	char *idepath = NULL;

	uint8_t *p = ramrom;
	while (p < ramrom + sizeof(ramrom))
		*p++= rand();

	while ((opt = getopt(argc, argv, "r:I:d:f")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'I':
			idepath = optarg;
			ide = 1;
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

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ramrom + 0x100000, 0x80000) != 0x80000) {
		fprintf(stderr, "rhyophyre: ROM image should be 512K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide) {
		ppide = ppide_create("ppi0");
		if (ppide) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			}
			ppide_attach(ppide, 0, ide_fd);
			ppide_reset(ppide);
		}
	}

	io = z180_create(&cpu_z180);
	z180_ser_attach(io, 0, &console);
	z180_ser_attach(io, 1, &console_wo);
	z180_trace(io, trace & TRACE_CPU_IO);

	rtc = rtc_create();
	rtc_trace(rtc, trace & TRACE_RTC);

	/* 20ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;

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

	Z180RESET(&cpu_z180);
	cpu_z180.ioRead = io_read;
	cpu_z180.ioWrite = io_write;
	cpu_z180.memRead = mem_read;
	cpu_z180.memWrite = mem_write;
	cpu_z180.trace = rhyophyre_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int states = 0;
		unsigned int i, j;
		/* We have to run the DMA engine and Z180 in step per
		   instruction otherwise we will mess up on stalling DMA */

		/* Do an emulated 20ms of work (368640 clocks) */
		for (i = 0; i < 50; i++) {
			for (j = 0; j < 10; j++) {
				while (states < tstate_steps) {
					unsigned int used;
					used = z180_dma(io);
					if (used == 0)
						used = Z180Execute(&cpu_z180);
					states += used;
				}
				z180_event(io, states);
				states -= tstate_steps;
			}
		}

		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending Z180 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
//			if (!live_irq)
//				poll_irq_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z180.IFF1|cpu_z180.IFF2))
				int_recalc = 0;
		}
	}
	exit(0);
}
