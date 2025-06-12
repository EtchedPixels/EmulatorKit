/*
 *	Platform features
 *	Z80A @ 4Mhz
 *	Up to 2MB ROM per ROM card 	(only one card emulated)
 *	Up to 1MB RAM per RAM card	(only one card emulated)
 *	16550A UART @1.8432Mhz at I/O 0x68
 *	DS1302 bitbanged RTC
 *	8255 for PPIDE etc
 *	Memory banking
 *
 *	0x78	bit 7 RAM enable for lower RAM (1 = enable)
 *		bit 6 boot override
 *		bit 5 unused
 *		bits 4-0 A19-A15 on RAMs.
 *	`	Upper 32K fixed (can be jumped for 16K0)
 *		2 x 512K or 2 x 128K SRAM (or one or in theory a mix...)
 *
 *	0x7C	Same for ROM but bit 5 is A20 and up to 2 x 1MByte
 *		1 = enable ?? (cbios and schematic notes disagree!)
 *
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
#include "serialdevice.h"
#include "ttycon.h"
#include "16x50.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "z80dis.h"

static uint8_t ram[32][32768];	/* 1MB ROM for now */
static uint8_t rom[32][32768];	/* 1MB ROM for now */
static unsigned rombank;
static unsigned rambank;
static uint8_t romlatch;
static uint8_t ramlatch;
static uint8_t rom_on;
static uint8_t lram_on;
static uint8_t fast;
static uint8_t ide;
static uint8_t timer_hack;

static struct ppide *ppide;
static Z80Context cpu_z80;
static struct rtc *rtc;
static struct uart16x50 *uart;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_RTC	16
#define TRACE_PPIDE	32
#define TRACE_CPU	64
#define TRACE_BANK	128
#define TRACE_UART	256

static int trace = 0;

static const char *onoff[2] = { "off", "on " };

static void recalc_banks(void)
{
	/* Model the default banks with booten strapped so that boot override
	   works as a disable */
	rom_on = 0;
	lram_on = 0;
	if ((romlatch & 0xE0) == 0x00)
		rom_on = 1;
	if ((ramlatch & 0xE0) == 0x80)
		lram_on = 1;
	rombank = romlatch & 0x3F;
	rambank = ramlatch & 0x1F;

	if (trace & TRACE_BANK) {
		fprintf(stderr, "MMU: RAM %s [%02X] ROM %s [%02X]%s\n",
			onoff[lram_on], rambank, onoff[rom_on], rombank,
			(rom_on && lram_on) ? " (contention)":"");
	}
}

/* Is this right for the high memory case ? */
static uint8_t *mem_mmu(uint16_t addr)
{
	if (addr >= 0x8000)
		return ram[31] + (addr & 0x7FFF);
	if (rom_on && lram_on)
		fprintf(stderr, "***CONTENTION: 0x%04X\n", addr);
	if (rom_on)
		return rom[rombank] + addr;
	if (lram_on)
		return ram[rambank] + addr;
	return NULL;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t *r = mem_mmu(addr);
	if (trace & TRACE_MEM) {
		fprintf(stderr, "R %04X -> ", addr);
		if (r)
			fprintf(stderr, "%02X\n", *r);
		else
			fprintf(stderr, "No map\n");
	}
	if (r)
		return *r;
	return 0xFF;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t *r = mem_mmu(addr);
	if (trace & TRACE_MEM) {
		fprintf(stderr, "W %04X <- %02X", addr, val);
		if (r) {
			fprintf(stderr, "\n");
		} else
			fprintf(stderr, "(No map)\n");
	}
	if (r)
		*r = val;
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t *p = mem_mmu(addr);
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
	uint8_t *p = mem_mmu(addr);
	if (p == NULL)
		return 0xFF;
	return *p;
}

static void cpu_trace(unsigned unused)
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

unsigned int check_chario(void)
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

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (ppide && addr >= 0x60 && addr <= 0x67) 	/* Aliased */
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x68 && addr < 0x70)
		return uart16x50_read(uart, addr & 7);
	if (addr >= 0x70 && addr <= 0x77)
		return rtc_read(rtc);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr & 0xFF, val);
	addr &= 0xFF;
	if (ppide && addr >= 0x60 && addr <= 0x67)	/* Aliased */
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x68 && addr < 0x70)
		uart16x50_write(uart, addr & 7, val);
	else if (addr >= 0x70 && addr <= 0x77)
		rtc_write(rtc, val);
	else if (addr >= 0x78 && addr <= 0x7B) {
		if (trace & TRACE_BANK)
			fprintf(stderr, "RAM bank to %02X\n", val);
		ramlatch = val;
		recalc_banks();
	} else if (addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_BANK)
			fprintf(stderr, "ROM bank to %02X\n", val);
		romlatch = val;
		recalc_banks();
	}
	else if (addr == 0xFD) {
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
	fprintf(stderr, "rb-mbc: [-r rompath] [-i idepath] [-f] [-t] [-d tracemask] [-R]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "rb-mbc.rom";
	char *idepath[2] = { NULL, NULL };
	int i;

	while((opt = getopt(argc, argv, "r:i:d:ft")) != -1) {
		switch(opt) {
			case 'r':
				rompath = optarg;
				break;
				break;
			case 'i':
				if (ide == 2)
					fprintf(stderr, "rb-mbc: only two disks per controller.\n");
				else
					idepath[ide++] = optarg;
				break;
			case 'd':
				trace = atoi(optarg);
				break;
			case 'f':
				fast = 1;
				break;
			case 't':
				timer_hack = 1;
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
	for (i = 0; i < 32; i++) {
		if (read(fd, rom[i], 32768) != 32768) {
			if (i < 16) {
				fprintf(stderr, "rb-mbc: banked rom image should be 512K or 1024K.\n");
				exit(EXIT_FAILURE);
			} else
				memset(rom[i], 0xFF, 32768);
		}
	}
	close(fd);

	if (ide) {
		ppide = ppide_create("cf");
		fd = open(idepath[0], O_RDWR);
		if (fd == -1) {
			perror(idepath[0]);
			ide = 0;
		} else if (ppide_attach(ppide, 0, fd) == 0)
			ide = 1;
		if (idepath[1]) {
			fd = open(idepath[1], O_RDWR);
			if (fd == -1)
				perror(idepath[1]);
			else
				ppide_attach(ppide, 1, fd);
		}
	}

	rtc = rtc_create();
	rtc_trace(rtc, trace & TRACE_RTC);
	uart = uart16x50_create();
	uart16x50_trace(uart, trace & TRACE_UART);
	uart16x50_attach(uart, &console);

	recalc_banks();

	/* No real need for interrupt accuracy so just go with the timer. If we
	   ever do the UART as timer hack it'll need addressing! */
	tc.tv_sec = 0;
	tc.tv_nsec = 100000000L;

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
	cpu_z80.trace = cpu_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* 4MHz Z80 - 4,000,000 tstates / second */
	while (!done) {
		Z80ExecuteTStates(&cpu_z80, 400000);
		/* Do 100ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		uart16x50_event(uart);
		if (timer_hack)
			uart16x50_dsr_timer(uart);
	}
	exit(0);
}
