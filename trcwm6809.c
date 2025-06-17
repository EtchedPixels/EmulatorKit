/*
 *	The original is a 6309 but that means we'd need 6309 emulation
 *
 *	6309 CPU @3MHz
 *	Banked memory
 *	16550A @7.3728MHHz (only tx/rx rts/cts wired)
 *	Bitbang SD
 *
 *	0000-7FFF	Paged (A15-A19 from U7)
 *	8000-DFFF	Fixed RAM (A16-A19 held low by U8, A15 held high)
 *	E000-E7FF
 *	E800-EFFF	Page register (D0-D4 write only)
 *	F000-FFFF	8K Boot flash (4K mapped)
 *
 *	GAL decode for memory (A15-A10 only)
 *	RAM A15 low | A14 low | A13 low
 *	IO  11100X
 *	ROM 1111XX
 *	WMAP 11101X
 *	Fixed RAM 1XXXX
 *
 *	0000-DFFF	RAM
 *	E000-E7FF	I/O
 *	E800-EFFF	MAP register (W/O)
 *
 *	GAL decode for I/O
 *	Only A4-A6 are considered plus the general I/O select
 *	E000-E007	16x50
 *	E010		Expansion (SD)
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
#include "d6809.h"
#include "e6809.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "16x50.h"
#include "sdcard.h"

static uint8_t rom[4096];
static uint8_t ram[1024 * 1024];
static uint8_t page;
static uint8_t sd_out;
static uint8_t sd_in;

static unsigned fast;

struct uart16x50 *uart;
struct sdcard *sdcard;

/* The CPU runs at CLK/4 */
static uint16_t clockrate =  1200/4;

static uint8_t live_irq;

#define IRQ_16550A	1


static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_CPU	16
#define TRACE_IRQ	32
#define TRACE_UART	64
#define TRACE_SPI	128
#define TRACE_SD	256

static int trace = 0;

static void int_clear(unsigned int irq)
{
	live_irq &= ~(1 << irq);
}

static void int_set(unsigned int irq)
{
	live_irq |= 1 << irq;
}

void recalc_interrupts(void)
{
	if (uart16x50_irq_pending(uart))
		int_set(IRQ_16550A);
	else
		int_clear(IRQ_16550A);
}

static uint8_t bitcnt;
static uint8_t txbits, rxbits;

static void spi_clock_high(void)
{
	txbits <<= 1;
	txbits |= !!(sd_out & 0x40);
	bitcnt++;
	if (bitcnt == 8) {
		rxbits = sd_spi_in(sdcard, txbits);
		if (trace & TRACE_SPI)
			fprintf(stderr, "spi %02X | %02X\n", rxbits, txbits);
		bitcnt = 0;
	}
}

/* Bad design choice: SD input bit is bit 6 /CD is 7, the other way
   around would have been faster! */
static void spi_clock_low(void)
{
	sd_in &= 0xBF;
	sd_in |= (rxbits & 0x40);
	rxbits <<= 1;
	rxbits |= 1;
}


static uint8_t sd_input(void)
{
	return sd_in;
}

/* Again poor design choices. 7 is /SS 6 is MOSI 5 is SCK */

static void sd_output(uint8_t data)
{
	uint8_t delta = data ^ sd_out;
	sd_out = data;

	if (delta & 0x80) {
		if (data & 0x80) {
			bitcnt = 0;
			sd_spi_raise_cs(sdcard);
		} else {
			sd_spi_lower_cs(sdcard);
		}
	}
	if (delta & 0x20) {
		if (data & 0x20)
			spi_clock_high();
		else
			spi_clock_low();
	}
}

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

unsigned char do_e6809_read8(unsigned addr, unsigned debug)
{
	unsigned char r = 0xFF;
	/* Stop the debugger causing side effects in the I/O window */
	if ((addr >> 12) == 0xE && debug)
		return 0xFF;
	if ((addr & 0xF000) == 0xF000)
		r = rom[addr & 0xFFF];
	else if ((addr & 0xF800) == 0xE000) {
		if ((addr & 0x70) == 0x00)
			r = uart16x50_read(uart, addr & 7);
		else if ((addr & 0x70) == 0x10)
			return sd_input();
	}
	else if (addr & 0x8000)
		r = ram[addr];		/* Page 1 */
	else
		r = ram[(addr & 0x7FFF) | (page << 15)];
	if ((trace & TRACE_MEM) && !debug)
		fprintf(stderr, "R %04X = %02X\n", addr, r);
	return r;
}

unsigned char e6809_read8(unsigned addr)
{
	return do_e6809_read8(addr, 0);
}

unsigned char e6809_read8_debug(unsigned addr)
{
	return do_e6809_read8(addr, 1);
}

void e6809_write8(unsigned addr, unsigned char val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X = %02X\n", addr, val);
	/* Stop the debugger causing side effects in the I/O window */
	if ((addr & 0xF000) == 0xF000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: *** ROM\n");
		return;
	} else if ((addr & 0xF800) == 0xE000) {
		if ((addr & 0x70) == 0x00) {
			uart16x50_write(uart, addr & 7, val);
			return;
		} else if ((addr & 0x70) == 0x10) {
			sd_output(val);
			return;
		}
	}
	else if ((addr & 0xF800) == 0xE800) {
		page = val & 0x1F;
		return;
	}
	else if (addr & 0x8000)
		ram[addr] = val;	/* Page 1 */
	else
		ram[(addr & 0x7FFF) | (page << 15)] = val;
}

static const char *make_flags(uint8_t cc)
{
	static char buf[9];
	char *p = "EFHINZVC";
	char *d = buf;

	while(*p) {
		if (cc & 0x80)
			*d++ = *p;
		else
			*d++ = '-';
		cc <<= 1;
		p++;
	}
	*d = 0;
	return buf;
}

/* Called each new instruction issue */
void e6809_instruction(unsigned pc)
{
	char buf[80];
	struct reg6809 *r = e6809_get_regs();
	if (trace & TRACE_CPU) {
		d6809_disassemble(buf, pc);
		fprintf(stderr, "%04X: %-16.16s | ", pc, buf);
		fprintf(stderr, "%s %02X:%02X %04X %04X %04X %04X\n",
			make_flags(r->cc),
			r->a, r->b, r->x, r->y, r->u, r->s);
	}
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
	fprintf(stderr, "trcwm6809: [-f] [-S sdcardpath] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "trcwm6809.rom";
	char *sdpath = NULL;
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "d:fr:S:")) != -1) {
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
	if (read(fd, rom, 4096) != 4096) {
		fprintf(stderr, "trcwm6809: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
		close(fd);
	}

	sdcard = sd_create("sd0");
	if (sdpath) {
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
	}
	if (trace & TRACE_SD)
		sd_trace(sdcard, 1);
	sd_blockmode(sdcard);

	uart = uart16x50_create();
	uart16x50_trace(uart, trace & TRACE_UART);
	uart16x50_attach(uart, &console);
	uart16x50_set_clock(uart, 7372800);

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

	e6809_reset(trace & TRACE_CPU);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i;
		for (i = 0; i < 100; i++) {
			while(cycles < clockrate)
				cycles += e6809_sstep(live_irq, 0);
			cycles -= clockrate;
			recalc_interrupts();
		}
		/* Drive the serial */
		uart16x50_event(uart);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
