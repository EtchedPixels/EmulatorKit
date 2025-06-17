/*
 *	Initial NS32k emulation - very basic to test plans for the actual
 *	hardware.
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

#include "ns32k/32016.h"
#include "16x50.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];
static uint8_t rtc;
static uint8_t fast = 0;
static uint8_t wiznet = 0;
struct ppide *ppide;
struct rtc *rtcdev;
struct uart16x50 *uart;

static uint16_t tstate_steps = 369;	/* rcbus speed (7.4MHz)*/

/* Who is pulling on the interrupt line */

static uint8_t live_irq;

#define IRQ_16550A	1

static nic_w5100_t *wiz;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_PPIDE	16
#define TRACE_IDE	32
#define TRACE_RTC	64
#define TRACE_CPU	128
#define TRACE_IRQ	256
#define TRACE_UART	512

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

void recalc_interrupts(void)
{
	if (live_irq)
		ns32016_set_irq(1);
	else
		ns32016_set_irq(0);
}

static void int_set(int src)
{
	live_irq |= (1 << src);
	recalc_interrupts();
}

static void int_clear(int src)
{
	live_irq &= ~(1 << src);
	recalc_interrupts();
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide_r %d\n", addr);
	return ide_read8(ide0, addr);
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide_w %d <- %02X\n", addr, val);
	ide_write8(ide0, addr, val);
}

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

uint8_t ns32016_do_port_read(uint32_t addr)
{
	if (addr & 1)
		return 0xFF;
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr >>= 1;
	addr &= 0xFF;
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read(rtcdev);
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		return uart16x50_read(uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void ns32016_do_port_write(uint32_t addr, uint8_t val)
{
	if (addr & 1)
		return;
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr >>= 1;
	addr &= 0xFF;
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		my_ide_write(addr & 7, val);
	else if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr == 0x0C && rtc)
		rtc_write(rtcdev, val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		uart16x50_write(uart, addr & 0x0F, val);
	else if (0 && addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

uint8_t ns32016_do_read(uint32_t addr, unsigned int debug)
{
	if ((addr & 0x00F00000) == 0x00F00000) {
		if (debug)
			return 0xFF;
		return ns32016_do_port_read(addr);
	}
	/* Only 1MB then wraps */
	addr &= 0xFFFFF;
	return ramrom[addr];
}

uint8_t ns32016_read8_debug(uint32_t addr)
{
	return ns32016_do_read(addr, 1);	/* No side effects */
}

uint8_t ns32016_read8(uint32_t addr)
{
	uint8_t r = ns32016_do_read(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %06X = %02X\n", addr, r);
	return r;
}

void ns32016_write8(uint32_t addr, uint8_t val)
{
	if ((addr & 0x00F00000) == 0x00F00000) {
		ns32016_do_port_write(addr, val);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %06X = %02X\n", addr, val);
	if (addr > 0x8000)
		ramrom[addr & 0xFFFFF] = val;

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
	fprintf(stderr, "rcbus-ns32k: [-1] [-a] [-b] [-B] [-e rombank] [-f] [-i idepath] [-I ppidepath] [-R] [-r rompath] [-e rombank] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "rcbus-ns32k.rom";
	char *idepath = NULL;

	while ((opt = getopt(argc, argv, "d:fi:I:r:Rw")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
			break;
		case 'I':
			ide = 2;
			idepath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'R':
			rtc = 1;
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
	if (read(fd, ramrom, 32768) != 32768) {
		fprintf(stderr, "rcbus-ns32k: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide) {
		/* FIXME: clean up when classic cf becomes a driver */
		if (ide == 1) {
			ide0 = ide_allocate("cf");
			if (ide0) {
				int ide_fd = open(idepath, O_RDWR);
				if (ide_fd == -1) {
					perror(idepath);
					ide = 0;
				}
				else if (ide_attach(ide0, 0, ide_fd) == 0) {
					ide = 1;
					ide_reset_begin(ide0);
				}
			} else
				ide = 0;
		} else {
			ppide = ppide_create("ppide");
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			} else
				ppide_attach(ppide, 0, ide_fd);
			if (trace & TRACE_PPIDE)
				ppide_trace(ppide, 1);
		}
	}

	uart = uart16x50_create();
	if (trace & TRACE_UART)
		uart16x50_trace(uart, 1);
	uart16x50_set_input(uart, 1);

	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

	if (rtc) {
		rtcdev = rtc_create();
		rtc_reset(rtcdev);
		if (trace & TRACE_RTC)
			rtc_trace(rtcdev, 1);
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

	ns32016_init();
	ns32016_reset_addr(0);

	ns32016_trace((trace & TRACE_CPU) ? 3 : 0);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 100 times then poll the
	   slow stuff and nap for 5ms. */
	while (!done) {
		int i;
		/* 36400 T states for base rcbus - varies for others */
		for (i = 0; i < 100; i++) {
			ns32016_exec(tstate_steps);
			uart16x50_event(uart);
			if (uart16x50_irq_pending(uart))
				int_set(IRQ_16550A);
			else
				int_clear(IRQ_16550A);
		}
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
