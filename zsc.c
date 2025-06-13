/*
 *	Z80 at 2MHz
 *	ACIA at 0
 *	8bit IDE at 80-87
 *	Timer interrupt at 08
 *	Bank select at 0x10
 *	Write protect at 0x11
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
#include "libz80/z80.h"
#include "acia.h"
#include "ide.h"

static uint8_t baseram[49152];
static uint8_t bankram[16][16384];
static uint8_t rom[4][16384];

static uint8_t romen = 1;
static uint8_t fast = 0;
static uint8_t banknum = 0;
static uint8_t wprotect = 0;

static Z80Context cpu_z80;
static struct acia *acia;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	8
#define TRACE_ACIA	16
#define TRACE_BANK	32
#define TRACE_IRQ	64

static int trace = 0;

static uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	if (addr < 16384) {
		if (romen)
			r = rom[banknum & 3][addr];
		else
			r = bankram[banknum][addr];
	} else
		r = baseram[addr - 16384];

	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	if (addr <16384) {
		if (romen)
			fprintf(stderr, "[PC = %04X: ROM write %04X %02x]\n",
				cpu_z80.PC, addr, val);
		else {
			if (banknum < 8 && (wprotect &  (1 << banknum))) {
				fprintf(stderr, "[PC = %04X: FAULT write %04X %02x]\n",
						cpu_z80.PC, addr, val);
			} else
				bankram[banknum][addr] = val;
		}
	} else if (addr >= 23296)
		baseram[addr - 16384] = val;
	else if (trace & TRACE_MEM)
		fprintf(stderr, "[PC = %04X: LORAM write %04X %02x]\n",
			cpu_z80.PC, addr, val);
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

static uint8_t timer_int;

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr < 0x10)
		return acia_read(acia, addr & 3);
	if (addr >= 0x80 && addr <= 0x87)
		return my_ide_read(addr & 7);
	if (addr == 0x10) {
		uint8_t r = timer_int ? 0x80 : 0;
		timer_int = 0;
		return r;
	}
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr < 0x10)
		acia_write(acia, addr & 3, val);
	else if (addr >= 0x80 && addr <= 0x87)
		my_ide_write(addr & 7, val);
	else if (addr == 0x10) {
		banknum = val & 0x0F;
		romen = val & 0x80;
		if (trace & TRACE_BANK)
			fprintf(stderr, "Bank set to %d, ROMEN %d.\n",
				banknum, romen);
	} else if (addr == 0x11) {
		wprotect = val;
		if (trace & TRACE_BANK)
			fprintf(stderr, "Wprotect set to %X\n", wprotect);
	} else if (addr == 0xFD)
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
	fprintf(stderr, "zsc: [-f] [-t] [-i path] [-r path] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "zsc.rom";
	char *idepath = "zsc.cf";

	while ((opt = getopt(argc, argv, "d:i:r:ft")) != -1) {
		switch (opt) {
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
	l = read(fd, rom, 65536);
	if (l < 16384) {
		fprintf(stderr, "zsc: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	ide0 = ide_allocate("cf");
	if (ide0) {
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			ide = 0;
		} else if (ide_attach(ide0, 0, fd) == 0) {
			ide = 1;
			ide_reset_begin(ide0);
		}
	}

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

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		int l;
		/* 50 Hz outer loop for a 2MHz CPU */
		/* 40,000 T states a loop */
		for (l = 0; l < 4; l++) {
			int i;
			/* We do 2000 tstates per ms */
			for (i = 0; i < 5; i++) {
				Z80ExecuteTStates(&cpu_z80, 2000);
				acia_timer(acia);
				if (acia_irq_pending(acia) || timer_int)
					Z80INT(&cpu_z80, 0xFF);
			}
			/* Do 5ms of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
		}
		timer_int = 1;
	}
	exit(0);
}
