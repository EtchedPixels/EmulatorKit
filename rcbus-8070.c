/*
 *	NS807x CPU card
 *	IDE adapter
 *	16x50 serial
 *	TMS video
 *
 *	TODO:
 *	RTC
 *	Flat memory
 *	Timer card
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
#include "serialdevice.h"
#include "ttycon.h"
#include "16x50.h"
#include "ide.h"
#include "ns807x.h"
#include "event.h"
#include "system.h"
#include "tms9918a.h"
#include "tms9918a_render.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t bankhigh = 0;
static uint8_t mmureg = 0;
static uint8_t fast = 0;

static struct ide_controller *ide;
static struct uart16x50 *uart;
static struct tms9918a *vdp;
static struct tms9918a_renderer *vdprend;
static struct ns8070 *cpu;
static uint8_t addr_hi;

static uint8_t have_tms;

static uint16_t tstate_steps = 369;	/* rcbus speed (7.4MHz)*/

/* Who is pulling on the interrupt line */

static uint8_t live_irq;

#define IRQ_16550A	1
#define IRQ_TMS9918A	2

volatile int emulator_done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_512	16
#define TRACE_RTC	32
#define TRACE_CPU	64
#define TRACE_IRQ	128
#define TRACE_UART	256
#define TRACE_TMS9918A  512

static int trace = 0;

static void poll_irq_event(void);

/* FIXME: emulate paging off correctly, also be nice to emulate with less
   memory fitted */
uint8_t ns807x_read(uint16_t addr)
{
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
	} else 	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(stderr, "R %04x[%02X] = %02X\n", addr, (unsigned int) bankreg[bank], (unsigned int) ramrom[(bankreg[bank] << 14) + (addr & 0x3FFF)]);
		addr &= 0x3FFF;
		return ramrom[(bankreg[bank] << 14) + addr];
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

void ns807x_write(uint16_t addr, uint8_t val)
{
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
	} else if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(stderr, "W %04x[%02X] = %02X\n", (unsigned int) addr, (unsigned int) bankreg[bank], (unsigned int) val);
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
		if (addr >= 8192 && !bank512)
			ramrom[addr] = val;
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	}
}

void recalc_interrupts(void)
{
	if (live_irq)
		ns8070_set_a(cpu, 1);
	else
		ns8070_set_a(cpu, 0);
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

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

static uint8_t do_ns807x_inport(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if (ide && addr >= 0x10 && addr <= 0x17)
		return ide_read8(ide, addr & 7);
	if ((addr == 0x98 || addr == 0x99) && vdp)
		return tms9918a_read(vdp, addr & 1);
	if (addr >= 0xC0 && addr <= 0xCF)
		return uart16x50_read(uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static uint8_t ns807x_inport(uint8_t addr)
{
	uint8_t r = do_ns807x_inport(addr);
	/* Get the IRQ flag back righr */
	poll_irq_event();
	return r;
}

static void ns807x_outport(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if (addr == 0xFF && bankhigh) {
		mmureg = val;
		if (trace & TRACE_512)
			fprintf(stderr, "MMUreg set to %02X\n", val);
	} else if (ide && addr >= 0x10 && addr <= 0x17)
		ide_write8(ide, addr & 7, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (bank512 && addr >= 0x78 && addr <= 0x7B) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(stderr, "Bank %d set to %d\n", addr & 3, val);
	} else if (bank512 && addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_512)
			fprintf(stderr, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if ((addr == 0x98 || addr == 0x99) && vdp)
		tms9918a_write(vdp, addr & 1, val);
	else if (addr >= 0xC0 && addr <= 0xCF)
		uart16x50_write(uart, addr & 0x0F, val);
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
	poll_irq_event();
}

uint8_t ns8070_read(struct ns8070 *cpu, uint16_t addr)
{
	if ((addr & 0xFF00) == 0xFE00)
		return ns807x_inport(addr & 0xFF);
	return ns807x_read(addr);
}

void ns8070_write(struct ns8070 *cpu, uint16_t addr, uint8_t v)
{
	if ((addr & 0xFF00) == 0xFE00)
		ns807x_outport(addr & 0xFF, v);
	ns807x_write(addr, v);
}

void ns8070_flag_change(struct ns8070 *cpu, uint8_t bits)
{
	/* We will need these for the flat board */
	addr_hi = bits;
}

static void poll_irq_event(void)
{
	if (uart16x50_irq_pending(uart))
		int_set(IRQ_16550A);
	else
		int_clear(IRQ_16550A);

	if (vdp && tms9918a_irq_pending(vdp))
		int_set(IRQ_TMS9918A);
	else
		int_clear(IRQ_TMS9918A);
}

/* Move into NS807x code ? */
static void ns807x_exec(int tstate_steps)
{
	do {
		tstate_steps -= ns8070_execute_one(cpu);
	} while(tstate_steps > 0);
}


static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	emulator_done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "rcbus-8070: [-b] [-B] [-e rombank] [-f] [-i idepath] [-R] [-r rompath] [-e rombank] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	int rombank = 0;
	char *rompath = "rcbus-8070.rom";
	char *idepath;

	while ((opt = getopt(argc, argv, "bBd:e:fi:r:T")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'e':
			rombank = atoi(optarg);
			break;
		case 'b':
			bank512 = 1;
			bankhigh = 0;
			rom = 0;
			break;
		case 'B':
			bankhigh = 1;
			bank512 = 0;
			rom = 0;
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
		case 'T':
			have_tms = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (rom == 0 && bank512 == 0 && bankhigh == 0) {
		fprintf(stderr, "rcbus-8070: no ROM\n");
		exit(EXIT_FAILURE);
	}

	if (rom) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		bankreg[0] = 0;
		bankreg[1] = 1;
		bankreg[2] = 32;
		bankreg[3] = 33;
		if (lseek(fd, 8192 * rombank, SEEK_SET) < 0) {
			perror("lseek");
			exit(1);
		}
		if (read(fd, ramrom, 65536) < 2048) {
			fprintf(stderr, "rcbus-8070: short rom '%s'.\n", rompath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}

	if (bank512|| bankhigh ) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom, 524288) != 524288) {
			fprintf(stderr, "rcbus-8070: banked rom image should be 512K.\n");
			exit(EXIT_FAILURE);
		}
		bankenable = 1;
		close(fd);
	}

	if (idepath) {
		int ide_fd = open(idepath, O_RDWR);
		ide = ide_allocate("cf");
		if (ide_fd == -1) {
			perror(idepath);
			exit(1);
		}
		if (ide_attach(ide, 0, ide_fd) == 0)
			ide_reset_begin(ide);
		else
			ide = NULL;
	}

	uart = uart16x50_create();
	if (trace & TRACE_UART)
		uart16x50_trace(uart, 1);
	uart16x50_attach(uart, &console);

	if (have_tms) {
		vdp = tms9918a_create();
		tms9918a_trace(vdp, !!(trace & TRACE_TMS9918A));
		vdprend = tms9918a_renderer_create(vdp);
	}
	/* 20ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;

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

	cpu = ns8070_create(NULL);
	ns8070_reset(cpu);
	if (trace & TRACE_CPU)
		ns8070_trace(cpu, 1);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 100 times then poll the
	   slow stuff and nap for 5ms. */
	while (!emulator_done) {
		int i;
		/* 36400 T states for base rcbus - varies for others */
		for (i = 0; i < 400; i++) {
			ns807x_exec(tstate_steps);
			uart16x50_event(uart);
			poll_irq_event();
			/* We want to run UI events regularly it seems */
			if (ui_event())
				emulator_done = 1;
		}
		/* 50Hz which is near enough */
		if (vdp) {
			tms9918a_rasterize(vdp);
			tms9918a_render(vdprend);
		}
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
