/*
 *	Platform features
 *
 *	80C188 at 12MHz
 *	IDE at 0x10-0x17 no high or control access (mirrored at 0x90-97)
 *	First 512K RAM Second 512K ROM
 *	RTC at 0x0C
 *	16550A at 0xC0
 *	WizNET ethernet
 *
 *	FIXME: switch to uart lib
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
#include "80x86/e8086.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "16x50.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];

static uint8_t rtc;
static uint8_t fast = 0;
static uint8_t wiznet = 0;

e8086_t *cpu;
struct ppide *ppide;
struct rtc *rtcdev;
static nic_w5100_t *wiz;
struct uart16x50 *uart;

static uint16_t tstate_steps = 369;	/* rcbus speed (7.4MHz)  for now */

static volatile int done;

/* Who is pulling on the interrupt line */
static uint8_t live_irq;

#define IRQ_16550A	1


#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_PPIDE	16
#define TRACE_RTC	32
#define TRACE_CPU	64
#define TRACE_IRQ	128
#define TRACE_UART	256

static int trace = 0;

uint8_t i808x_do_read(uint32_t addr)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %06X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

uint8_t i808x_debug_read(uint16_t addr)
{
	return i808x_do_read(addr);	/* No side effects */
}

uint8_t i808x_read8(void *mem, unsigned long addr)
{
	return i808x_do_read(addr);
}

uint16_t i808x_read16(void *mem, unsigned long addr)
{
	uint16_t r = i808x_do_read(addr);
	r |= i808x_do_read(addr + 1);
	return r;
}


void do_i808x_write(uint32_t addr, uint8_t val)
{
	if (addr >= 512 * 1024) {
		fprintf(stderr, "W: %06X: write to ROM of %02X.\n", addr, val);
		return;
	}
	ramrom[addr] = val;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %06X = %02X\n", addr, val);
}

void i808x_write8(void *mem, unsigned long addr, uint8_t val)
{
	do_i808x_write(addr, val);
}

void i808x_write16(void *mem, unsigned long addr, uint16_t val)
{
	do_i808x_write(addr, val);
	do_i808x_write(addr + 1, val >> 8);
}

extern void recalc_interrupts(void);

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

/* FIXME: we really need to do the right 80C188 vectoring */
void recalc_interrupts(void)
{
	if (uart16x50_irq_pending(uart))
		int_set(live_irq);
	else
		int_clear(live_irq);
	if (live_irq)
		e86_irq(cpu, 0x20);
	else
		e86_irq(cpu, 0);
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

/* 16x50 callbacks */
void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Nothing we care about */
}

/* Our port handling is 8bit wide because our bus is 8bit wide without any
   wide transaction indications */
static uint8_t i808x_inport(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %04x\n", addr);
	addr &= 0xFF;
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read(rtcdev);
	else if (addr >= 0xC0 && addr <= 0xCF) {
		uint8_t r = uart16x50_read(uart, addr & 0x0F);
		recalc_interrupts();
		return r;
	}
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void i808x_outport(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %04x <- %02x\n", addr, val);
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
	else if (addr >= 0xC0 && addr <= 0xCF) {
		uart16x50_write(uart, addr & 0x0F, val);
		recalc_interrupts();
	} else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

static uint8_t i808x_in8(void *mem, unsigned long addr)
{
	return i808x_inport(addr & 0xFFFF);
}

static uint16_t i808x_in16(void *mem, unsigned long addr)
{
	uint16_t r = i808x_inport(addr);
	r |= i808x_inport(addr + 1) << 8;
	return r;
}

static void i808x_out8(void *mem, unsigned long addr, uint8_t val)
{
	i808x_outport(addr, val);
}

static void i808x_out16(void *mem, unsigned long addr, uint16_t val)
{
	i808x_outport(addr, val);
	i808x_outport(addr + 1, val >> 8);
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
	fprintf(stderr, "rcbus-80c188: [-1] [-f] [-R] [-r rompath] [-e rombank] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "rcbus-808x.rom";
	char *idepath;

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
	if (read(fd, ramrom + 512 * 1024, 512 * 1024) < 512 * 1024) {
		fprintf(stderr, "rcbus: short rom '%s'.\n", rompath);
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
	uart16x50_trace(uart, !!(trace & TRACE_UART));
	uart16x50_attach(uart, &console);

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

	cpu = e86_new();
	if (cpu == NULL) {
		fprintf(stderr, "rcbus: failed to create CPU instance.\n");
		exit(1);
	}
	e86_init(cpu);
	/* FIXME: no 80C188 emulation so need to tweak emulator later */
	e86_set_80186(cpu);
	/* Bus interfaces */
	e86_set_mem(cpu, NULL, i808x_read8, i808x_write8, i808x_read16, i808x_write16);
	e86_set_prt(cpu, NULL, i808x_in8, i808x_out8, i808x_in16, i808x_out16);
	e86_set_ram(cpu, ramrom, sizeof(ramrom));

	/* Reset the CPU */
	e86_reset(cpu);

	if (trace & TRACE_CPU) {
//		i808x_log = stderr;
	}

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
			e86_clock(cpu, tstate_steps);
			uart16x50_event(uart);
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
