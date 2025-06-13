/*
 *	Platform features
 *
 *	Main board:
 *	Z80A @ 4MHz
 *	Bitbanger port (with IRQ)	(emulated as unused)
 *	32K EPROM, 32K base RAM
 *
 *	Front panel board:
 *	7 x 7 segment displays (scanned)	(not emulated)
 *	16 key keypad				(emulated as never pressed)
 *	Hardware reset
 *	1ms timer interrupt
 *
 *	CP/M board
 *	8250 UART @1.8432Mhz
 *	128K or 512K RAM (banked low 32K over EPROM)
 *	MicroSD card
 *
 *	TODO
 *	Is there any sane way to handle the 7 segment displays ?
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
#include <sys/types.h>
#include <sys/mman.h>
#include "libz80/z80.h"
#include "z80dis.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "16x50.h"
#include "sdcard.h"

static uint8_t bankram[16][32768];
static uint8_t eprom[32768];
static uint8_t ram[32768];

static uint8_t fpreg = 0xB8;	/* No keys no bitbanger */
static uint8_t fpcol;
static uint8_t bankreg;
static uint8_t qreg[8];

static uint8_t fast;

static Z80Context cpu_z80;
static volatile int done;

static struct sdcard *sdcard;
static struct uart16x50 *uart;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	4
#define TRACE_BANK	8
#define TRACE_UART	16
#define TRACE_LED	32
#define TRACE_QREG	64
#define TRACE_FPREG	128
#define TRACE_SD	256
#define TRACE_SPI	512
#define TRACE_CPU	1024

static int trace = 0;

static uint8_t do_mem_read(uint16_t addr, unsigned quiet)
{
	uint8_t r;

	if (!(trace & TRACE_MEM))
		quiet = 1;

	if (!quiet)
		fprintf(stderr, "R %04X: ", addr);
	if (addr >= 0x8000) {
		r = ram[addr & 0x7FFF];
	} else if (qreg[1] == 0)
		r = eprom[addr];
	else
		r = bankram[bankreg][addr];

	if (!quiet)
		fprintf(stderr, "<- %02X\n", r);
	return r;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	return do_mem_read(addr, 0);
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X: -> %02X\n", addr, val);
	if (addr >= 0x8000)
		ram[addr & 0x7FFF] = val;
	else /* Writes through under the EPROM even if EPROM mapped */
		bankram[bankreg][addr] = val;
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


static uint8_t sd_bits;
static uint8_t sd_bitct;

static uint8_t spi_byte_sent(uint8_t val)
{
	uint8_t r = 0xFF;
	if (sdcard)
		r  = sd_spi_in(sdcard, val);
	if (trace & TRACE_SPI)
		fprintf(stderr,	"[SPI %02X:%02X]\n", val, r);
	fflush(stdout);
	return r;
}

static void spi_select(uint8_t val)
{
	if (val) {
		if (trace & TRACE_SPI)
			fprintf(stderr,	"[Raised \\CS]\n");
		sd_bits = 0;
		sd_bitct = 0;
		if (sdcard)
			sd_spi_raise_cs(sdcard);
		return;
	} else {
		if (trace & TRACE_SPI)
			fprintf(stderr,	"[Lowered \\CS]\n");
		if (sdcard)
			sd_spi_lower_cs(sdcard);
	}
}

static void spi_clock(void)
{
	static uint8_t rxbits = 0xFF;

	if (!qreg[0]) {
		fprintf(stderr, "SPI clock: no op.\n");
		return;
	}

	if (trace & TRACE_SPI)
		fprintf(stderr, "[SPI clock - txbit = %d ", qreg[5]);
	sd_bits <<= 1;
	sd_bits |= qreg[5];
	sd_bitct++;
	if (sd_bitct == 8) {
		rxbits = spi_byte_sent(sd_bits);
		sd_bitct = 0;
	}
	/* Falling edge */
	uart16x50_signal_event(uart, (rxbits & 0x80) ^ 0x80);
	if (trace & TRACE_SPI)
		fprintf(stderr, "rxbit = %d]\n", rxbits >> 7);
	rxbits <<= 1;
	rxbits |= 0x01;
}

static void recalc_interrupts(void)
{
	if (uart16x50_irq_pending(uart) || (fpreg & 0x40))
		Z80INT(&cpu_z80, 0xFF);	/* actually undefined */
	else
		Z80NOINT(&cpu_z80);
}

static void fpreg_write(uint8_t val)
{
	fpreg &= ~0x47;	/* IRQ clear, clear counter */
	/* 7 segment scanner not yet simulated */
	fpcol++;
	fpcol &=7;
	fpreg |= fpcol;
	if (trace & TRACE_FPREG)
		fprintf(stderr, "fpreg write %02x now %02x\n", val, fpreg);
	recalc_interrupts();
}

static uint8_t qreg_read(uint8_t addr)
{
	if (trace & TRACE_QREG)
		fprintf(stderr, "Q%d Read\n", addr);
	spi_clock();
	return 0xFF;
}

static void qreg_write(uint8_t reg, uint8_t v)
{
	if (trace & TRACE_QREG)
		fprintf(stderr, "Q%d -> %d.\n", reg, v);

	if (qreg[reg] == v)
		return;

	qreg[reg] = v;

	if ((trace & TRACE_LED) && reg == 2)
		fprintf(stderr, "Yellow LED to %d.\n", v);
	if ((trace & TRACE_LED) && reg == 6)
		fprintf(stderr, "Green LED to %d.\n", v);
	/* FIXME: model UART reset */
	if (reg == 4)
		spi_select(v);
}

/* 16x50 callbacks */
void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	bankreg = mcr & 0x1F;
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x40 && addr <= 0x4F)
		return fpreg;
	if (addr >= 0xC0 && addr <= 0xC7)
		qreg_read(addr & 7);
	if (addr >= 0xC8 && addr <= 0xCF) {
		uint8_t r = uart16x50_read(uart, addr & 7);
		recalc_interrupts();
		return r;
	}
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr & 0xFF, val);
	addr &= 0xFF;
	if (addr >= 0x40 && addr <= 0x4F)
		fpreg_write(val);
	else if (addr >= 0xC0 && addr <= 0xC7)
		qreg_write(addr & 7, val & 1);
	else if (addr >= 0xC8 && addr <= 0xCF) {
		uart16x50_write(uart, addr & 7, val);
		recalc_interrupts();
	} else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	}
	else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %02X of %02X\n",
			addr & 0xFF, val);
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
	fprintf(stderr, "z80mc: [-f] [-r rompath] [-s sdcardpath] [-d tracemask]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "z80mc.rom";
	char *sdpath = NULL;

	while((opt = getopt(argc, argv, "r:s:d:f")) != -1) {
		switch(opt) {
			case 'r':
				rompath = optarg;
				break;
			case 's':
				sdpath = optarg;
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
	if (read(fd, eprom, 16384) != 16384) {
		fprintf(stderr, "z80mc: ROM image should be 16K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (sdpath) {
		sdcard = sd_create("sd0");
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
		if (trace & TRACE_SD)
			sd_trace(sdcard, 1);
	}

	uart = uart16x50_create();
	uart16x50_trace(uart, !!(trace & TRACE_UART));
	uart16x50_attach(uart, &console);

	/* No real need for interrupt accuracy so just go with the timer. If we
	   ever do the UART as timer hack it'll need addressing! */
	tc.tv_sec = 0;
	tc.tv_nsec = 1000000L;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON|ECHO);
		term.c_cc[VMIN] = 1;
		term.c_cc[VTIME] = 0;
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

	qreg[5] = 1;
	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* 4MHz Z80 - 4,000,000 tstates / second, and 1000 ints/sec */
	while (!done) {
		Z80ExecuteTStates(&cpu_z80, 4000);
		/* Do 1ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		uart16x50_event(uart);
		fpreg |= 0x40;
		recalc_interrupts();
	}
	exit(0);
}
