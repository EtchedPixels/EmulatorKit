/*
 *	Platform features
 *
 *	65C816 processor card for rcbus with flat addressing
 *
 *	Because the 65C816 memory management is somewhat deranged the
 *	memory can be folded to just keep the RAM present and have RAM
 *	at 0 post initial bootstrap.
 *
 *	Memory map
 *	Top quadrant decides what something is
 *	00	memory
 *	01	unused
 *	10	control latch
 *	11	I/O space
 *
 *	At boot we are in the top of the first 64K of ROM as the physical
 *	mapping is the flat memory card so
 *		00000-7FFFF ROM
 *		80000-FFFFF RAM
 *
 *	All I/O is effectively 'far' but this doesn't really matter.
 *
 *	Physically RAM addresses are permuted within the 64K bank so that
 *	logical ABCDE is physical ADEBC. This is done so that I/O devices
 *	which don't care abotut the upper bits appear 256 bytes wide.
 *	Other than the ROM needing shuffling this has no real inconveniences
 *	but makes disk I/O a crapload faster.
 *
 *	Motorola 68B50
 *	IDE at 0x10-0x17 no high or control access
 *	RTC at 0x0C
 *	16550A at 0xC0
 *	Minimal 6522VIA emulation
 *
 *	TODO
 *	MMU emulation including ABORT
 *	Interrupt via NMI and mask register
 *	Wire CPU VP to the iolatch
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
#include "sram_mmu8.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static uint8_t fast = 0;
static uint8_t wiznet = 0;

static uint16_t tstate_steps = 200;

static uint8_t iolatch;

/* Who is pulling on the interrupt line */

#define IRQ_ACIA	1
#define IRQ_16550A	2
#define IRQ_VIA		3

static nic_w5100_t *wiz;
static struct via6522 *via;
static struct acia *acia;
static struct uart16x50 *uart;
static struct rtc *rtc;
static struct sram_mmu *mmu;

static int acia_narrow;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_IRQ	4
#define TRACE_UNK	8
#define TRACE_CPU	16
#define TRACE_RTC	64
#define TRACE_ACIA	128
#define TRACE_UART	256
#define TRACE_VIA	512
#define TRACE_MMU	1024

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


/* The address lines are permuted */
static uint32_t bytemangle(uint32_t addr)
{
	uint32_t r = (addr & 0x0F0000);
	r |= (addr & 0xFF00) >> 8;
	r |= (addr & 0xFF) << 8;
	if (iolatch & 4)
		r |= 0x80000;
	return r;
}

/* The 65C816 version of the MMU card flips the address lines back */
static uint32_t backpermute(uint32_t addr)
{
	uint32_t r = (addr & 0x030000);
	r |= (addr & 0xFF00) >> 8;
	r |= (addr & 0xFF) << 8;
	return r;
}

static uint8_t mmio_read_65c816(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr = bytemangle(addr);
	addr &=0xFF;
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

static void mmio_write_65c816(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr = bytemangle(addr);
	addr &=0xFF;
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr == 0x38 && mmu)
		sram_mmu_set_latch(mmu, val);
	else if (addr >= 0x60 && addr <= 0x6F)
		via_write(via, addr & 0x0F, val);
	else if (addr == 0x0C && rtc)
		rtc_write(rtc, val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		uart16x50_write(uart, addr & 0x0F, val);
	else if (addr == 0x00) {
		printf("trace set to %d\n", val);
		trace = val;
		if (trace & TRACE_CPU)
			CPU_setTrace(1);
		else
			CPU_setTrace(0);
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

uint8_t do_65c816_read(uint32_t addr)
{
	uint8_t *ptr;
	unsigned int abrt;
	addr = bytemangle(addr);

	if (mmu == NULL || !(addr & 0x80000))
		return ramrom[addr & 0xFFFFF];

	ptr = sram_mmu_translate(mmu, backpermute(addr), 0, !(iolatch & 1), 0, &abrt);
	/* TODO ABORT CYCLE */
	if (ptr)
		return *ptr;
	return 0xFF;
}

void do_65c816_write(uint32_t addr, uint8_t val)
{
	uint8_t *ptr;
	unsigned int abrt;
	addr = bytemangle(addr);

	/* Low 512K is fixed ROM mapping */
	if (addr < 0x80000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
		return;
	}
	if (mmu == NULL) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X = %02X\n", addr, val);
		ramrom[addr & 0xFFFFF] = val;
		return;
	}
	ptr = sram_mmu_translate(mmu, backpermute(addr), 1, !(iolatch & 1), 0, &abrt);
	if (abrt)
		CPU_abort();
	else if (ptr)
		*ptr = val;
	/* MMU provided fault diagnostics itself */
}

uint8_t read65c816(uint32_t addr, uint8_t debug)
{
	uint8_t r = 0xFF;

	switch(addr >> 22) {
	case 0:
		r = do_65c816_read(addr);
		break;
	case 1:
		break;
	case 2:
		break;
	case 3:
		if (debug)
			return 0xFF;
		else if (!(iolatch & 1))
			r = mmio_read_65c816(addr);
	}
	return r;
}

void write65c816(uint32_t addr, uint8_t val)
{
	switch(addr >> 22) {
	case 0:
		do_65c816_write(addr, val);
		break;
	case 1:
		break;
	case 2:
		if (iolatch & 1)
			break;
		if (trace & TRACE_MMU)
			fprintf(stderr, "[iolatch to %02X]\n", val);
		iolatch = val;
		break;
	case 3:
		if (iolatch & 1)
			break;
		mmio_write_65c816(addr, val);
		break;
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
	fprintf(stderr, "rcbus-65c816: [-1] [-A] [-a] [-b] [-c] [-f] [-R] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int opt;
	int fd;
	char *rompath = "rcbus-65c816-flat.rom";
	char *idepath;
	int input = 0;
	int hasrtc = 0;

	while ((opt = getopt(argc, argv, "1Aabd:fi:r:Rw")) != -1) {
		switch (opt) {
		case '1':
			input = 2;
			break;
		case 'a':
			input = 1;
			acia_narrow = 0;
			break;
		case 'b':
			if (mmu == NULL)
				mmu = sram_mmu_create();
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
		fprintf(stderr, "rcbus: ROM image should be 512K.\n");
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
	if (mmu)
		sram_mmu_trace(mmu, trace & TRACE_MMU);

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
