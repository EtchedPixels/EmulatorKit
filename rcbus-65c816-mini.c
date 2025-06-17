/*
 *	Platform features
 *
 *	65C816 processor card for rcbus with 16bit addressing and I/O at $FExx
 *	Motorola 68B50
 *	IDE at 0x10-0x17 no high or control access
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at 0x0C
 *	16550A at 0xC0
 *	Minimal 6522VIA emulation
 *
 *	Also supports the protected mode card. In this configuration
 *	memory is split 56K/8K and there is a user/kernel flip flop. This
 *	goes into kernel mode on a vector pull and can be put back into user
 *	mode by a write to FEFE. I/O access in user mode is blocked and a
 *	watchdog runs to catch stuff that blocks interrupts etc.
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
#include <lib65816/cpu.h>
#include <lib65816/cpuevent.h>
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"
#include "ide.h"
#include "rtc_bitbang.h"
#include "6522.h"
#include "16x50.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;
static uint8_t bankhigh;

static uint8_t fast = 0;
static uint8_t wiznet = 0;
static uint8_t iopage = 0xFE;
static uint8_t mmureg;
static uint8_t mmumode = 1;
static unsigned watchdog;

static uint16_t tstate_steps = 200;

/* Who is pulling on the interrupt line */

#define IRQ_ACIA	1
#define IRQ_16550A	2
#define IRQ_VIA		3

static nic_w5100_t *wiz;
struct via6522 *via;
struct acia *acia;
struct uart16x50 *uart;
struct rtc *rtc;

static int acia_narrow;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_IRQ	4
#define TRACE_UNK	8
#define TRACE_512	32
#define TRACE_RTC	64
#define TRACE_CPU	128
#define TRACE_ACIA	512
#define TRACE_UART	2048
#define TRACE_VIA	4096

static int trace = 0;

/* We do this in the 65C816 loop instead. Provide a dummy for the device models */
void recalc_interrupts(void)
{
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

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

/*
 *	6522 VIA support - we don't do anything with the pins on the VIA
 *	right now
 */

void via_recalc_outputs(struct via6522 *via)
{
}

void via_handshake_a(struct via6522 *via)
{
}

void via_handshake_b(struct via6522 *via)
{
}

uint8_t mmio_read_65c816(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr >= 0x60 && addr <= 0x6F)
		return via_read(via, addr & 0x0F);
	if (addr == 0x0C && rtc)
		return rtc_read(rtc);
	if (addr >= 0xC0 && addr <= 0xCF && uart)
		return uart16x50_read(uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void mmio_write_65c816(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr >= 0x60 && addr <= 0x6F)
		via_write(via, addr & 0x0F, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (addr >= 0x78 && addr <= 0x7B && !bankhigh) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(stderr, "Bank %d set to %d\n", addr & 3, val);
	} else if (addr >= 0x7C && addr <= 0x7F && !bankhigh) {
		if (trace & TRACE_512)
			fprintf(stderr, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0x0C && rtc)
		rtc_write(rtc, val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		uart16x50_write(uart, addr & 0x0F, val);
	else if (addr == 0xFF && bankhigh) {
		mmureg = val;
		mmumode = !(addr & 1);
		if (trace & TRACE_512)
			fprintf(stderr, "MMUreg set to %02X\n", val);
	} else if (addr == 0x00) {
		printf("trace set to %d\n", val);
		trace = val;
		if (trace & TRACE_CPU)
			CPU_setTrace(1);
		else
			CPU_setTrace(0);
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

uint8_t do_65c816_read(uint16_t addr)
{
	/* TODO: fix this so we can tell vector pull from the emulation */
	if (addr >= 0xFFF0 && bankhigh) {
		/* Emulate the 2nd generation board for now */
		mmureg = 2;
		mmumode = 1;	/* Kernel mode */
		/* TODO: clear NMI */
	}
	if (bankhigh) {
		uint8_t reg = mmureg;
		uint8_t val;
		uint32_t higha;
		if (addr < 0xE000)
			reg >>= 1;
		higha = (reg & 0x40) ? 1 : 0;
		higha |= (reg & 0x10) ? 2 : 0;
		higha |= (reg & 0x4) ? 4 : 0;
		higha |= (reg & 0x01) ? 8 : 0;	/* ROM/RAM */

		val = ramrom[(higha << 16) + addr];
		if (trace & TRACE_MEM) {
			fprintf(stderr, "R %04X[%02X] = %02X\n",
				(unsigned int)addr,
				(unsigned int)higha,
				(unsigned int)val);
		}
		return val;
	}
	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(stderr, "R %04X[%02X] = %02X\n", addr, (unsigned int) bankreg[bank], (unsigned int) ramrom[(bankreg[bank] << 14) + (addr & 0x3FFF)]);
		addr &= 0x3FFF;
		return ramrom[(bankreg[bank] << 14) + addr];
	}
	/* When banking is off the entire 64K is occupied by repeats of ROM 0 */
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr & 0x3FFF]);
	return ramrom[addr & 0x3FFF];
}

uint8_t read65c816(uint32_t addr, uint8_t debug)
{
	uint8_t r;

	addr &= 0xFFFF;

	if (addr >> 8 == iopage && mmumode == 1) {
		if (debug)
			return 0xFF;
		else
			return mmio_read_65c816(addr);
	}
	r = do_65c816_read(addr);
	return r;
}

void write65c816(uint32_t addr, uint8_t val)
{
	addr &= 0xFFFF;

	if (addr >> 8 == iopage && mmumode) {
		mmio_write_65c816(addr, val);
		return;
	}
	if (bankhigh) {
		uint8_t reg = mmureg;
		uint8_t higha;
		if (addr < 0xE000)
			reg >>= 1;
		higha = (reg & 0x40) ? 1 : 0;
		higha |= (reg & 0x10) ? 2 : 0;
		higha |= (reg & 0x4) ? 4 : 0;
		higha |= (reg & 0x01) ? 8 : 0;	/* ROM/RAM */

		if (trace & TRACE_MEM) {
			fprintf(stderr, "W %04X[%02X] = %02X\n",
				(unsigned int)addr,
				(unsigned int)higha,
				(unsigned int)val);
		}
		if (!(higha & 8)) {
			if (trace & TRACE_MEM)
				fprintf(stderr, "[Discard: ROM]\n");
			return;
		}
		ramrom[(higha << 16)+ addr] = val;
		return;
	}
	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(stderr, "W %04X[%02X] = %02X\n", (unsigned int) addr, (unsigned int) bankreg[bank], (unsigned int) val);
		if (bankreg[bank] >= 32) {
			addr &= 0x3FFF;
			ramrom[(bankreg[bank] << 14) + addr] = val;
		}
		/* ROM writes go nowhere */
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X = %02X\n", addr, val);
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	}
}

void wdm(void)
{
}

void system_process(void)
{
	static int n = 0;
	static struct timespec tc;
	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;
	if (acia)
		acia_timer(acia);
	if (uart)
		uart16x50_event(uart);
	via_tick(via, 100);

	if (acia) {
		if (acia_irq_pending(acia))
			CPU_addIRQ(IRQ_ACIA);
		else
			CPU_clearIRQ(IRQ_ACIA);
	}
	if (via_irq_pending(via))
		CPU_addIRQ(IRQ_VIA);
	else
		CPU_clearIRQ(IRQ_VIA);

	if (uart) {
		if (uart16x50_irq_pending(uart))
			CPU_addIRQ(IRQ_16550A);
		else
			CPU_clearIRQ(IRQ_16550A);
	}
	if (bankhigh) {
		if (mmumode == 0) {
			watchdog++;
			/* TODO: right value in terms of clocking rate and tsteps */
			if (watchdog == 65536)
				CPU_nmi();
		} else
			watchdog = 0;
	}
	if (n++ == 100) {
		n = 0;
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
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
	fprintf(stderr, "rcbus: [-1] [-A] [-a] [-c] [-f] [-R] [-B] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int opt;
	int fd;
	char *rompath = "rcbus-65c816.rom";
	char *idepath;
	int input = 0;
	int hasrtc = 0;

	while ((opt = getopt(argc, argv, "1Aad:fi:r:RwB")) != -1) {
		switch (opt) {
		case '1':
			input = 2;
			break;
		case 'a':
			input = 1;
			acia_narrow = 0;
			break;
		case 'A':
			input = 1;
			acia_narrow = 1;
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
		case 'R':
			hasrtc = 1;
			break;
		case 'w':
			wiznet = 1;
			break;
		case 'B':
			bankhigh = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (input == 0) {
		fprintf(stderr, "rcbus: no UART selected, defaulting to 16550A\n");
		input = 2;
	}

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ramrom, 524288) != 524288) {
		fprintf(stderr, "rcbus: banked rom image should be 512K.\n");
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
			}
			if (ide_attach(ide0, 0, ide_fd) == 0) {
				ide = 1;
				ide_reset_begin(ide0);
			}
		} else
			ide = 0;
	}

	if (input == 1) {
		acia = acia_create();
		acia_attach(acia, &console);
		acia_trace(acia, trace & TRACE_ACIA);
	} else {
		uart = uart16x50_create();
		uart16x50_attach(uart, &console);
		uart16x50_trace(uart, trace & TRACE_UART);
	}

	via = via_create();

	if (hasrtc) {
		rtc = rtc_create();
		rtc_trace(rtc, trace & TRACE_RTC);
	}

	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}


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

	if (trace & TRACE_CPU)
		CPU_setTrace(1);

	CPUEvent_initialize();
	CPU_setUpdatePeriod(tstate_steps);
	CPU_reset();
	CPU_run();
	exit(0);
}
