/*
 *	Platform features
 *	Z80A @ 8Mhz
 *	1MB ROM (max), 512K RAM (can also be set to 128K)
 *	16550A UART @1.8432Mhz at I/O 0x68
 *	DS1302 bitbanged RTC
 *	8255 for PPIDE etc
 *	Memory banking
 *	0x78-7B: RAM bank
 *	0x7C-7F: ROM bank (or set bit 7 to get RAM bank)
 *
 *	IRQ from serial only, or from ECB bus but not serial
 *	Optional PropIO v2 for I/O ports (keyboard/video/sd)
 *
 *	General stuff to tackle
 *	Interrupt jumper (ECB v 16x50)
 *	ECB and timer via UART interrupt hack (timer done)
 *	DS1302 burst mode for memory
 *	Do we care about DS1302 writes ?
 *	Whine/break on invalid PPIDE sequences to help debug code
 *	Memory jumpers (is it really 16/48 or 48/16 ?)
 *	Z80 CTC card (ECB)
 *	4UART needs connecting to something as does uart0 when in PropIO
 *	SCG would be fun but major work (does provide vblank though)
 *
 *	Fix usage!
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
#include "ppide.h"
#include "propio.h"
#include "ramf.h"
#include "rtc_bitbang.h"
#include "w5100.h"

#define HIRAM	63

static uint8_t ramrom[64][32768];	/* 512K ROM for now */
static uint8_t rombank;
static uint8_t rambank;
static uint8_t ram_mask = 0x0F;		/* 16 * 32K */

static uint8_t ide;
static struct ppide *ppi0;
static struct propio *propio;
static uint8_t timerhack;
static uint8_t fast;
static uint8_t wiznet;

static Z80Context cpu_z80;

static nic_w5100_t *wiz;
static struct rtc *rtc;
static struct uart16x50 *uart[5];
static struct ramf *ramf;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_RTC	16
#define TRACE_PPIDE	32
#define TRACE_PROP	64
#define TRACE_BANK	128
#define TRACE_UART	256
#define TRACE_CPU	512

static int trace = 0;

static uint8_t do_mem_read(uint16_t addr, unsigned quiet)
{
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X: ", addr);
	if (addr > 32767) {
		if (!quiet && (trace & TRACE_MEM))
			fprintf(stderr, "HR %04X<-%02X\n",
				addr & 0x7FFF, ramrom[HIRAM][addr & 0x7FFF]);
		return ramrom[HIRAM][addr & 0x7FFF];
	}
	if (rombank & 0x80) {
		if (!quiet && (trace & TRACE_MEM))
			fprintf(stderr, "LR%d %04X<-%02X\n",
				rambank, addr, ramrom[32 + (rambank)][addr]);
		return ramrom[32 + (rambank)][addr];
	}
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "LF%d %04X<->%02X\n",
			rombank & 0x1F, addr, ramrom[rombank & 0x1F][addr]);
	return ramrom[rombank & 0x1F][addr];
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	return do_mem_read(addr, 0);
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X: ", addr);
	if (addr > 32767) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "HR %04X->%02X\n",addr, val);
		ramrom[HIRAM][addr & 0x7FFF] = val;
	}
	else if (rombank & 0x80) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "LR%d %04X->%02X\n", (rambank), addr, val);
		ramrom[32 + (rambank)][addr] = val;
	} else if (trace & TRACE_MEM)
		fprintf(stderr, "LF%d %04X->ROM\n",
			(rombank & 0x1F), addr);
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

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (ppi0 && addr >= 0x60 && addr <= 0x67) 	/* Aliased */
		return ppide_read(ppi0, addr & 3);
	if (addr >= 0x68 && addr < 0x70)
		return uart16x50_read(uart[0], addr & 7);
	if (addr >= 0x70 && addr <= 0x77)
		return rtc_read(rtc);
	if (ramf && (addr >= 0xA0 && addr <= 0xA7))
		return ramf_read(ramf, addr & 7);
	if (propio && (addr >= 0xA8 && addr <= 0xAF))
		return propio_read(propio, addr);
	if (addr >= 0xC0 && addr <= 0xDF)
		return uart16x50_read(uart[((addr - 0xC0) >> 3) + 1], addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr & 0xFF, val);
	addr &= 0xFF;
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (ppi0 && addr >= 0x60 && addr <= 0x67)	/* Aliased */
		ppide_write(ppi0, addr & 3, val);
	else if (addr >= 0x68 && addr < 0x70)
		uart16x50_write(uart[0], addr & 7, val);
	else if (addr >= 0x70 && addr <= 0x77)
		rtc_write(rtc, val);
	else if (addr >= 0x78 && addr <= 0x79) {
		if (trace & TRACE_BANK)
			fprintf(stderr, "RAM bank to %02X\n", val);
		rambank = val & ram_mask;
	} else if (addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_BANK) {
			fprintf(stderr, "ROM bank to %02X\n", val);
			if (val & 0x80)
				fprintf(stderr, "Using RAM bank %d\n", rambank);
		}
		rombank = val;
	}
	else if (ramf && addr >=0xA0 && addr <=0xA7)
		ramf_write(ramf, addr & 0x07, val);
	else if (propio && addr >= 0xA8 && addr <= 0xAF)
		propio_write(propio, addr & 0x03, val);
	else if (addr >= 0xC0 && addr <= 0xDF)
		uart16x50_write(uart[((addr - 0xC0) >> 3) + 1], addr & 7, val);
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
	fprintf(stderr, "rcbv2: [-1] [-f] [-r rompath] [-i idepath] [-t] [-p] [-s sdcardpath] [-d tracemask] [-R]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "sbc.rom";
	char *ppath = NULL;
	char *idepath[2] = { NULL, NULL };
	int i;
	char *ramfpath = NULL;
	unsigned int prop = 0;

	while((opt = getopt(argc, argv, "1r:i:s:ptd:fR:w")) != -1) {
		switch(opt) {
			case '1':
				ram_mask = 0x03;	/* 4 x 32K banks only */
				break;
			case 'r':
				rompath = optarg;
				break;
			case 'i':
				if (ide == 2)
					fprintf(stderr, "sbcv2: only two disks per controller.\n");
				else
					idepath[ide++] = optarg;
				break;
			case 's':
				ppath = optarg;
			case 'p':
				prop = 1;
				break;
			case 't':
				timerhack = 1;
				break;
			case 'd':
				trace = atoi(optarg);
				break;
			case 'f':
				fast = 1;
				break;
			case 'R':
				ramfpath = optarg;
				break;
			case 'w':
				wiznet = 1;
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
	for (i = 0; i < 16; i++) {
		if (read(fd, ramrom[i], 32768) != 32768) {
			fprintf(stderr, "sbcv2: banked rom image should be 512K.\n");
			exit(EXIT_FAILURE);
		}
	}
	close(fd);

	if (ide) {
		ppi0 = ppide_create("cf");
		if (ppi0) {
			fd = open(idepath[0], O_RDWR);
			if (fd == -1) {
				perror(idepath[0]);
				ide = 0;
			} else if (ppide_attach(ppi0, 0, fd) == 0) {
				ide = 1;
				ppide_reset(ppi0);
			}
			if (idepath[1]) {
				fd = open(idepath[1], O_RDWR);
				if (fd == -1)
					perror(idepath[1]);
				ppide_attach(ppi0, 1, fd);
			}
		} else
			ide = 0;
	}

	rtc = rtc_create();
	rtc_trace(rtc, trace & TRACE_RTC);

	if (prop) {
		propio = propio_create(ppath);
		propio_attach(propio, &console);
		propio_trace(propio, trace & TRACE_PROP);
	}

	if (ramfpath)
		ramf = ramf_create(ramfpath);

	for (i = 0; i < 5; i++) {
		uart[i] = uart16x50_create();
		if (!prop && i == 0)
			uart16x50_attach(uart[i], &console);
		else
			uart16x50_attach(uart[i], &console_wo);
	}

	uart16x50_trace(uart[0], trace & TRACE_UART);

	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

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
	cpu_z80.trace = z80_trace;

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
		uart16x50_event(uart[0]);
		uart16x50_event(uart[1]);
		uart16x50_event(uart[2]);
		uart16x50_event(uart[3]);
		uart16x50_event(uart[4]);
		uart16x50_dsr_timer(uart[0]);
		if (wiznet)
			w5100_process(wiz);
		if (uart16x50_irq_pending(uart[0]))
			Z80INT(&cpu_z80, 0xFF);
	}
	exit(0);
}
