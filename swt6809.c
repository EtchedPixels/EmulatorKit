/*
 *	SWTPC 6809
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
#include "acia.h"

static uint8_t rom[4096];
static uint8_t ram[1024 * 1024];
static uint8_t dat[16];

static unsigned fast;

struct acia *acia;

/* 1MHz */
static uint16_t clockrate =  100;

static uint8_t live_irq;

#define IRQ_ACIA	1


static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_CPU	16
#define TRACE_IRQ	32
#define TRACE_ACIA	64

static int trace = 0;

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
	if (acia_irq_pending(acia))
		int_set(IRQ_ACIA);
	else
		int_clear(IRQ_ACIA);
}

/* For now assume RAM only */
static uint8_t *dat_xlate(unsigned addr)
{
	return ram + dat[addr >> 12] + (addr & 0xFFF);
}

unsigned char do_e6809_read8(unsigned addr, unsigned debug)
{
        unsigned char r = 0xFF;
        /* Stop the debugger causing side effects in the I/O window */
	if ((addr & 0xFFFE) == 0xE004)
		r = acia_read(acia, addr & 1);
	else if (addr >= 0xF800)
		r = rom[addr & 0x7FF];
	else if (addr < 0xE000)
		r = *dat_xlate(addr);
	if (!debug && (trace & TRACE_MEM))
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
	if ((addr & 0xFFFE) == 0xE004)
		acia_write(acia, addr & 1, val);
	else if (addr >= 0xFFF0)
		dat[addr & 0x0F] = ~val;
	else if (addr >= 0xF800)
		fprintf(stderr, "***ROM WRITE %04X = %02X\n", addr, val);
	else if (addr < 0xE000)
		*dat_xlate(addr) = val;
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
	fprintf(stderr, "swt6809: [-f] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "swt6809.rom";
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "d:fr:")) != -1) {
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
	if (read(fd, rom, 2048) != 2048) {
		fprintf(stderr, "swt6809: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
		close(fd);
	}

	acia = acia_create();
	acia_set_input(acia, 1);
	acia_trace(acia, trace & TRACE_ACIA);

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
		acia_timer(acia);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
