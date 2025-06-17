	/*
 *	6808 CPU
 *	ACIA at $FEA0/A1
 *	IDE at $FE10-$FE17 no high or control access (mirrored at $FE90-97)
 *	32K fixed low, banked high memory
 *	32K ROM (can be banked out)
 *
 *	Optional 512K banked RAM instead
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
#include <sys/select.h>
#include "6800.h"
#include "ide.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"
#include "16x50.h"
#include "6840.h"

static uint8_t ramrom[1024 * 1024];

static uint8_t banksel;
static uint8_t bankreg[4];
static unsigned int bank512;
static unsigned int bankenable;
static uint8_t fast = 0;

/* The CPU runs at CLK/4 so for sane RS232 we run at the usual clock
   rate and get 115200 baud - which is pushing it admittedly! */
static uint16_t clockrate = 364 / 4;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_UART	16
#define TRACE_CPU	32
#define TRACE_IRQ	64
#define TRACE_PTM	128

static int trace = 0;

struct m6800 cpu;
struct acia *acia;
struct uart16x50 *uart;
struct m6840 *ptm;

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
	int pend;

	if (acia)
		pend = acia_irq_pending(acia);
	else
		pend = uart16x50_irq_pending(uart);

	if (m6840_irq_pending(ptm))
		pend |= 1;

	if (pend)
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else
		m6800_clear_interrupt(&cpu, IRQ_IRQ1);
}


void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
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

/* Clock timer 3 off timer 2 */
void m6840_output_change(struct m6840 *m, uint8_t outputs)
{
	static int old = 0;
	if ((outputs ^ old) & 2) {
		/* timer 2 high to low -> clock timer 3 */
		if (!(outputs & 2))
			m6840_external_clock(ptm, 3);
	}
	old = outputs;
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
	if (acia && (addr == 0xA0 || addr == 0xA1))
		return acia_read(acia, addr & 1);
	if (uart && (addr >= 0xC0 && addr <= 0xC7))
		return uart16x50_read(uart, addr & 7);
	if (ide && (addr >= 0x10 && addr <= 0x17))
		return my_ide_read(addr & 7);
	if (ptm && (addr >= 0x60 && addr <= 0x67))
		return m6840_read(ptm, addr & 7);
	return 0xFF;
}

static uint8_t m6800_ior(uint16_t addr)
{
	uint8_t r = m6800_do_ior(addr);
	if (trace & TRACE_IO)
		fprintf(stderr, "IR %04X = %02X\n", addr, r);
	recalc_interrupts();
	return r;
}

static void m6800_iow(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "IW %04X = %02X\n", addr, val);
	if (acia && addr >= 0xA0 && addr <= 0xA1)
		acia_write(acia, addr & 1, val);
	else if (ide && addr >= 0x10 && addr <= 0x17) {
		/* IDE at 0xFE10 for now */
		my_ide_write(addr & 7, val);
	} else if (addr == 0x38)
		banksel = val & 3;
	else if (addr >= 0x78 && addr <= 0x7B)
		bankreg[addr - 0x78] = val & 0x3F;
	else if (addr >= 0x7C && addr <= 0x7F)
		bankenable = val & 1;
	else if (uart && addr >= 0xC0 && addr <= 0xC7)
		uart16x50_write(uart, addr & 7, val);
	else if (ptm && addr >= 0x60 && addr <= 0x67)
		m6840_write(ptm, addr, val);
	else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown I/O write to 0x%02X of %02X\n",
			addr, val);
	recalc_interrupts();
}

static uint8_t *mmu_map(uint16_t addr, int write)
{
	unsigned int page;
	if (bank512 == 0) {
		/* Low 32K is fixed RAM bank */
		if (addr < 0x8000)
			return ramrom + addr + 0x18000;
		/* Writes to banksel 0 (ROM) not allowed */
		if (banksel == 0 && write)
			return NULL;
		/* 0 is ROM 1-3 RAM 32K chunks */
		addr &= 0x7FFF;
		return ramrom + addr + 0x8000 * banksel;
	}
	if (bankenable == 0) {
		if (write)
			return NULL;
		return ramrom + (addr & 0x3FFF);
	}
	page = bankreg[addr >> 14];
	if (page < 0x20 && write)
		return NULL;
	return ramrom + (addr & 0x3FFF) + (page << 14);
}

uint8_t m6800_read_op(struct m6800 *cpu, uint16_t addr, int debug)
{
	uint8_t r;
	uint8_t *ptr;
	if (addr >= 0xFE00 && addr < 0xFF00) {
		if (debug)
			return 0xFF;
		return m6800_ior(addr);
	}
	ptr = mmu_map(addr, 0);
	if (ptr == NULL)
		r = 0xFF;
	else
		r = *ptr;
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
	uint8_t *ptr;
	if (addr >= 0xFE00 && addr < 0xFF00)
		m6800_iow(addr, val);
	else {
		ptr = mmu_map(addr, 1);
		if (ptr == NULL) {
			if (trace & TRACE_MEM)
				fprintf(stderr, "W: %04X (ROM) with %02X\n", addr, val);
		} else  {
			if (trace & TRACE_MEM)
				fprintf(stderr, "W: %04X = %02X\n", addr, val);
			*ptr = val;
		}
	}
}

static void poll_irq_event(void)
{
	recalc_interrupts();
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
		"rcbus-6800: [-1] [-b] [-f] [-i path] [-R] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	unsigned int uarttype = 0;		/* ACIA */
	char *rompath = "rcbus-6800.rom";
	char *idepath;
	unsigned int cycles = 0;
	unsigned int romsize = 32768;

	while ((opt = getopt(argc, argv, "1bd:fi:r:")) != -1) {
		switch (opt) {
		case '1':
			/* 1655x */
			uarttype = 1;
			break;
		case 'b':
			bank512 = 1;
			romsize = 524288;
			break;
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
	if (read(fd, ramrom, romsize) != romsize) {
		fprintf(stderr, "rcbus-6800: short rom '%s'.\n",
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

	if (uarttype == 0) {
		acia = acia_create();
		if (trace & TRACE_UART)
			acia_trace(acia, 1);
		acia_attach(acia, &console);
	} else {
		uart = uart16x50_create();
		if (trace & TRACE_UART)
			uart16x50_trace(uart, 1);
		uart16x50_attach(uart, &console);
	}

	ptm = m6840_create();
	m6840_trace(ptm, trace & TRACE_PTM);

	if (trace & TRACE_CPU)
		cpu.debug = 1;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i, j;
		for (i = 0; i < 100; i++) {
			while (cycles < clockrate)
				cycles += m6800_execute(&cpu);
			m6840_tick(ptm, cycles);
			for (j = 0; j < cycles; j++)
				m6840_external_clock(ptm, 2);
			cycles -= clockrate;
		}
		/* Drive the internal serial */
		i = check_chario();
		if (acia)
			acia_timer(acia);
		else
			uart16x50_event(uart);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
