/*
 *	Z80Master S-100 Z80 at 4MHz with port 0xD2/0xD3 memory mapping
 *	0x30: Dual CF-Card/Hard Disk IDE S100 Bus Board
 *	0x00: Propellor Console I/O (interrupt not currently emulated)
 *	0XF8: Tarbell 8" floppy
 */

#include <stdio.h>
#include <stdbool.h>
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
#include "ppide.h"
#include "wd17xx.h"
#include "tarbell_fdc.h"

static uint8_t rom[2][4096];
static uint8_t ram[1048576];

static uint8_t port_d2, port_d3;

static Z80Context cpu_z80;
static struct ppide *ppide;
static struct wd17xx *fdc;

static unsigned int fast;
static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	8
#define TRACE_FDC	16
#define TRACE_BANK	32
#define TRACE_IRQ	64

static int trace = 0;

static uint8_t *mem_map(uint16_t addr, bool wr)
{
	if (addr >= 0x8000) {
		/* High memory always writes through to RAM */
		if (wr)
			return ram + addr;
		/* ROM off - RAM mapped */
		if (addr < 0xF000 || (port_d3 & 0x01))
			return ram + addr;
		return &rom[(port_d3 >> 1) & 1][addr & 0x0FFF];
	}
	/* Banked space */
	if (addr < 0x4000)
		return ram + (addr & 0x3FFF) + ((port_d2 & 0xFC) << 12);
	return ram + (addr & 0x3FFF) + ((port_d3 & 0xFC) << 12);
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	r = *mem_map(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	*mem_map(addr,  1) = val;
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

void recalc_interrupts(void)
{
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr == 0x00) {
		int n = check_chario();
		uint8_t r = (n & 1) << 1;
		if (n & 2)
			r |= 4;
		return r;
	}
	if (addr == 0x01)
		return next_char();
	if (addr >= 0x30 && addr <= 0x33)
		return ppide_read(ppide, addr & 3);
	if (fdc && addr >= 0xF8 && addr <= 0xFC)
		return tbfdc_read(fdc, addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr == 0x01)
		write(1, &val, 1);
	else if (addr == 0xD2)
		port_d2 = val;
	else if (addr == 0xD3)
		port_d3 = val;
	else if (addr >= 0x30 && addr <= 0x33)
		ppide_write(ppide, addr & 3, val);
	else if (fdc && addr >= 0xF8 && addr <= 0xFC)
		tbfdc_write(fdc, addr & 7, val);
	else if (addr == 0xFD)
		trace = val;
	else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
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
	fprintf(stderr, "s100: [-A drive] [-B drive] [-f] [-i path] [-r path] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "s100.rom";
	char *idepath = "s100.cf";
	char *drive_a = NULL, *drive_b = NULL;

	while ((opt = getopt(argc, argv, "d:i:r:ft")) != -1) {
		switch (opt) {
		case 'A':
			drive_a = optarg;
			break;
		case 'B':
			drive_b = optarg;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			idepath = optarg;
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
	l = read(fd, &rom[0], 4096);
	if (l < 4096) {
		fprintf(stderr, "s100: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	l = read(fd, &rom[1], 4096);
	if (l < 4096) {
		if (l == 0)
			memcpy(&rom[1], &rom[0], sizeof(rom[0]));
		else {
			fprintf(stderr, "s100: short rom '%s'.\n", rompath);
			exit(EXIT_FAILURE);
		}
	}
	close(fd);

	if (drive_a || drive_b) {
		fdc = tbfdc_create();
		if (trace & TRACE_FDC)
			wd17xx_trace(fdc, 1);
		if (drive_a)
			wd17xx_attach(fdc, 0, drive_a, 1, 80, 26, 128);
		if (drive_b)
			wd17xx_attach(fdc, 0, drive_b, 1, 80, 26, 128);
	}

	ppide = ppide_create("ppide0");
	if (ppide) {
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		} else if (ppide_attach(ppide, 0, fd) == 0) {
			ppide_reset(ppide);
		}
	}

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

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;

	/* Cheap way to emulate the nop stuffer */
	memset(ram, 0, 65536);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		int l;
		/* 50 Hz outer loop for a 4MHz CPU */
		/* 80,000 T states a tick */
		for (l = 0; l < 20; l++) {
			/* We do 4000 tstates per ms */
			Z80ExecuteTStates(&cpu_z80, 4000);
		}
		/* Do 20ms of I/O and delays */
		if (fdc)
			wd17xx_tick(fdc, 20);
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
