/*
 *	6808 CPU
 *	ACIA at $FEA0/A1
 *	IDE at $FE10-$FE17 no high or control access (mirrored at $FE90-97)
 *	32K fixed low, banked high memory
 *	32K ROM (can be banked out)
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
#include "acia.h"

static uint8_t ram[4][32768];
static uint8_t rom[32768];

static uint8_t bankreg;

static uint8_t fast = 0;

/* The CPU runs at CLK/4 so for sane RS232 we run at the usual clock
   rate and get 115200 baud - which is pushing it admittedly! */
static uint16_t clockrate = 364 / 4;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_ACIA	16
#define TRACE_CPU	32
#define TRACE_IRQ	64

static int trace = 0;

struct m6800 cpu;
struct acia *acia;
struct wd17xx *wdfdc;

int check_chario(void)
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

	if (select(2, &i, NULL, NULL, &tv) == -1) {
		if (errno == EINTR)
			return 0;
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
	if (acia_irq_pending(acia))
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else
		m6800_clear_interrupt(&cpu, IRQ_IRQ1);
}

/* Not relevant for 6800 */

void m6800_sci_change(struct m6800 *cpu)
{
}

void m6800_tx_byte(struct m6800 *cpu, uint8_t byte)
{
}

/* I/O ports: nothing for now */

void m6800_port_output(struct m6800 *cpu, int port)
{
}

uint8_t m6800_port_input(struct m6800 *cpu, int port)
{
	return 0xFF;
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	return ide_read8(ide0, addr);
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	ide_write8(ide0, addr, val);
}

/* I/O space */

static uint8_t m6800_do_ior(uint8_t addr)
{
	if (addr == 0xA0 || addr == 0xA1)
		return acia_read(acia, addr & 1);
	if (ide && (addr >= 0x10 && addr <= 0x17))
		return my_ide_read(addr & 7);
	return 0xFF;
}

static uint8_t m6800_ior(uint16_t addr)
{
	uint8_t r = m6800_do_ior(addr);
	if (trace & TRACE_IO)
		fprintf(stderr, "IR %04X = %02X\n", addr, r);
	return r;
}

static void m6800_iow(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "IW %04X = %02X\n", addr, val);
	if (addr >= 0xA0 && addr <= 0xA1)
		acia_write(acia, addr & 1, val);
	else if (ide && addr >= 0x10 && addr <= 0x17) {
		/* IDE at 0x8080 for now */
		my_ide_write(addr & 7, val);
	} else if (addr == 0x38)
		bankreg = val & 3;
	else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown I/O write to 0x%02X of %02X\n",
			addr, val);
}

uint8_t m6800_read_op(struct m6800 *cpu, uint16_t addr, int debug)
{
	uint8_t r;
	if (addr >= 0xFE00 && addr < 0xFF00) {
		if (debug)
			return 0xFF;
		return m6800_ior(addr);
	}
	if (addr < 0x8000)
		r = ram[0][addr];
	else if (bankreg == 0)
		r = rom[addr & 0x7FFF];
	else
		r = ram[bankreg][addr & 0x7FFF];
	if (!debug && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X = %02X\n", addr, r);
	return r;
}

uint8_t m6800_debug_read(struct m6800 *cpu, uint16_t addr)
{
	return m6800_read_op(cpu, addr, 1);
}

uint8_t m6800_read(struct m6800 *cpu, uint16_t addr)
{
	return m6800_read_op(cpu, addr, 0);
}

void m6800_write(struct m6800 *cpu, uint16_t addr, uint8_t val)
{
	if (addr < 0x8000)
		ram[0][addr] = val;
	else if (addr >= 0xFE00 && addr < 0xFF00)
		m6800_iow(addr, val);
	else if (bankreg == 0) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X (ROM) with %02X\n", addr, val);
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X = %02X\n", addr, val);
		ram[bankreg][addr & 0x7FFF] = val;
	}
}

static void poll_irq_event(void)
{
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
	fprintf(stderr,
		"rc2014-6800: [-f] [-i path] [-R] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "rc2014-6800.rom";
	char *idepath;
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "d:fi:r:")) != -1) {
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
		fprintf(stderr, "rc2014-6800: short rom '%s'.\n",
			rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide) {
		ide0 = ide_allocate("cf");
		if (ide0) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			} else if (ide_attach(ide0, 0, ide_fd) == 0) {
				ide = 1;
				ide_reset_begin(ide0);
			}
		} else
			ide = 0;
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

	m6800_reset(&cpu, CPU_6800, INTIO_NONE, 3);

	acia = acia_create();

	if (trace & TRACE_ACIA)
		acia_trace(acia, 1);
	acia_set_input(acia, 1);

	if (trace & TRACE_CPU)
		cpu.debug = 1;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i;
		for (i = 0; i < 100; i++) {
			while (cycles < clockrate)
				cycles += m6800_execute(&cpu);
			cycles -= clockrate;
		}
		/* Drive the internal serial */
		i = check_chario();
		acia_timer(acia);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
