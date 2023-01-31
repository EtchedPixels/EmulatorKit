/*
 *	Platform features
 *
 *	68HC11A CPU
 *	512K RAM
 *	Top 16K is pageable ROM (controlled by PA3 - low is ROM enable)
 *	A18-A16 are PA6-PA3
 *	SD on the SPI, internal serial
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
#include <errno.h>
#include "6800.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "w5100.h"
#include "sdcard.h"

static uint8_t ram[512 * 1024];		/* Covers the banked card */
static uint8_t rom[32768];		/* System EPROM */
static uint8_t monitor[8192];		/* Monitor ROM - usually Buffalo */
static uint8_t eerom[2048];		/* EEROM - not properly emulated yet */

static uint8_t fast = 0;

static uint32_t flatahigh = 0;
static uint8_t romen = 1;

struct sdcard *sdcard;

/* The CPU E clock runs at CLK/4. The rcbus standard clock is fine
   and makes serial rates easy */
static uint16_t clockrate =  364/4;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_CPU	16
#define TRACE_IRQ	32
#define TRACE_SD	64
#define TRACE_SPI	128
#define TRACE_UART	256

static int trace = 0;

struct m6800 cpu;

static uint16_t lastch = 0xFFFF;

int check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;
	char c;


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
	if (FD_ISSET(0, &i)) {
		r |= 1;
		if (lastch == 0xFFFF) {
			if (read(0, &c, 1) == 1)
				lastch = c;
			if (c == ('V' & 31)) {
				cpu.debug ^= 1;
				fprintf(stderr, "CPUTRACE now %d\n", cpu.debug);
				lastch = 0xFFFF;
				r &= ~1;
			}
		}
	}
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

unsigned int next_char(void)
{
	char c = lastch;
	lastch = 0xFFFF;
	if (c == 0x0A)
		c = '\r';
	return c;
}

void recalc_interrupts(void)
{
}

/* Serial glue: a bit different as the serial port is on chip and emulated
   by the CPU emulator */

void m6800_sci_change(struct m6800 *cpu)
{
	static uint_fast8_t pscale[4] = {1, 3, 4, 13 };
	unsigned int baseclock = 7372800 / 4;	/* E clock */
	unsigned int prescale = pscale[(cpu->io.baud >> 4) & 0x03];
	unsigned int divider = 1 << (cpu->io.baud & 7);

	/* SCI changed status - could add debug here FIXME */
	if (!(trace & TRACE_UART))
		return;

	baseclock /= prescale;
	baseclock /= divider;
	baseclock /= 16;

	fprintf(stderr, "[UART  %d baud]\n", baseclock);
}

void m6800_tx_byte(struct m6800 *cpu, uint8_t byte)
{
	write(1, &byte, 1);
}

static void flatarecalc(struct m6800 *cpu)
{
	uint8_t bits = cpu->io.padr;
	fprintf(stderr, "pactl %02X padr %02X\n", cpu->io.pactl, cpu->io.padr);

	if (!(cpu->io.pactl & 0x08))
		bits &= 0xF7;
	/* We should check for OC/IC function and blow up messily
	   if set */
	flatahigh = 0;
	if (bits & 0x40)
		flatahigh += 0x40000;
	if (bits & 0x20)
		flatahigh += 0x20000;
	if (bits & 0x10)
		flatahigh += 0x10000;
	romen = !!(bits & 0x08);
}


/* I/O ports */

void m6800_port_output(struct m6800 *cpu, int port)
{
	/* Port A is the flat model A16-A20 */
	if (port == 1)
		flatarecalc(cpu);
	if (sdcard && port == 4) {
		if (cpu->io.pddr & 0x20)
			sd_spi_raise_cs(sdcard);
		else
			sd_spi_lower_cs(sdcard);
	}
}

uint8_t m6800_port_input(struct m6800 *cpu, int port)
{
	if (port == 5)
		return 0x00;
	return 0xFF;
}

void m68hc11_port_direction(struct m6800 *cpu, int port)
{
	flatarecalc(cpu);
}

static uint8_t spi_rxbyte;

/* Should fix this to model whether D bit 5 is assigned as GPIO */

void m68hc11_spi_begin(struct m6800 *cpu, uint8_t val)
{
	spi_rxbyte = 0xFF;
	if (sdcard) {
		if (trace & TRACE_SPI)
			fprintf(stderr, "SPI -> %02X\n", val);
		spi_rxbyte = sd_spi_in(sdcard, val);
		if (trace & TRACE_SPI)
			fprintf(stderr, "SPI <- %02X\n", spi_rxbyte);
	}
}

uint8_t m68hc11_spi_done(struct m6800 *cpu)
{
	return spi_rxbyte;
}

uint8_t *m6800_map(uint16_t addr, int wr)
{
	if (addr >= 0xC000 && romen) {
		if (wr)
			return NULL;
		return rom + (addr & 0x3FFF);
	}
	return ram + flatahigh + addr;
}

uint8_t m6800_debug_read(struct m6800 *cpu, uint16_t addr)
{
	return *m6800_map(addr, 0);
}

uint8_t m6800_read(struct m6800 *cpu, uint16_t addr)
{
	uint8_t r;
	r = *m6800_map(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, r);
	return r;
}


void m6800_write(struct m6800 *cpu, uint16_t addr, uint8_t val)
{
	uint8_t *rp = m6800_map(addr, 1);
	if (rp == NULL)
		fprintf(stderr, "%04X: write to ROM.\n", addr);
	else
		*rp = val;
}

static void poll_irq_event(void)
{
	/* For now only internal IRQ */
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "mini11: [-f] [-r rom] [-S sdcard] [-m monitor] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "mini11.rom";
	char *monpath = NULL;
	char *sdpath = NULL;
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "r:d:fSm:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'S':
			sdpath = optarg;
			break;
		case 'm':
			monpath = optarg;
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
	if (read(fd, rom, 32768) != 32768) {
		fprintf(stderr, "mini11: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (monpath) {
		fd = open(monpath, O_RDONLY);
		if (fd == -1) {
			perror(monpath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, monitor, 8192) != 8192) {
			fprintf(stderr, "mini11: short monitor '%s'.\n", monpath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}

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

	/* 68HC11E variants */
	if (monpath) /* 68HC11A with EEROM and ROM */
		m68hc11a_reset(&cpu, 8, 0x03, monitor, eerom);
	else	/* 68HC11A0 */
		m68hc11a_reset(&cpu, 0, 0, NULL, NULL);

	if (trace & TRACE_CPU)
		cpu.debug = 1;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i;
		unsigned int j;
		/* 36400 T states for base rcbus - varies for others */
		for (j = 0; j < 10; j++) {
			for (i = 0; i < 10; i++) {
				while(cycles < clockrate)
					cycles += m68hc11_execute(&cpu);
				cycles -= clockrate;
			}
			/* Drive the internal serial */
			i = check_chario();
			if (i & 1)
				m68hc11_rx_byte(&cpu, next_char());
			if (i & 2)
				m68hc11_tx_done(&cpu);
		}
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
