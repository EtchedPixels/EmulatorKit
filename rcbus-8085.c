/*
 *	Platform features
 *
 *	8085 at 6.144MHz
 *	Motorola 6850 at 0xA0-0xA7
 *	IDE at 0x10-0x17 no high or control access (mirrored at 0x90-97)
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at 0x0C
 *	8085 bitbang port
 *	16550A at 0xC0
 *	WizNET ethernet
 *
 *	Alternate MMU option using highmmu on 8085/MMU card
 *
 *	TODO:
 *	Bitbang serial emulation
 *	82C54 timer
 *	Emulate TMS9918A timer bits at least
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
#include "intel_8085_emulator.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"
#include "16x50.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "event.h"
#include "system.h"
#include "tms9918a.h"
#include "tms9918a_render.h"
#include "w5100.h"
#include "sasi.h"
#include "ncr5380.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t bankhigh = 0;
static uint8_t mmureg = 0;
static uint8_t rtc;
static uint8_t fast = 0;
static uint8_t wiznet = 0;
static uint8_t acia_uart;

struct ppide *ppide;
struct rtc *rtcdev;
struct uart16x50 *uart;
static struct tms9918a *vdp;
static struct tms9918a_renderer *vdprend;
static struct sasi_bus *sasi;
static struct ncr5380 *ncr;

static uint8_t have_tms;

static uint16_t tstate_steps = 369;	/* rcbus speed (7.4MHz)*/

/* Who is pulling on the interrupt line */

static uint8_t live_irq;

#define IRQ_ACIA	1
#define IRQ_16550A	2
#define IRQ_TMS9918A	3

static nic_w5100_t *wiz;

volatile int emulator_done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_PPIDE	16
#define TRACE_512	32
#define TRACE_RTC	64
#define TRACE_ACIA	128
#define TRACE_CPU	512
#define TRACE_IRQ	1024
#define TRACE_UART	2048
#define TRACE_TMS9918A  4096
#define TRACE_SCSI 	8192

static int trace = 0;

static void poll_irq_event(void);

/* FIXME: emulate paging off correctly, also be nice to emulate with less
   memory fitted */
uint8_t i8085_do_read(uint16_t addr)
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

uint8_t i8085_debug_read(uint16_t addr)
{
	return i8085_do_read(addr);	/* No side effects */
}

uint8_t i8085_read(uint16_t addr)
{
	return i8085_do_read(addr);
}

void i8085_write(uint16_t addr, uint8_t val)
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
		i8085_set_int(INT_RST65);
	else
		i8085_clear_int(INT_RST65);
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


struct acia *acia;


static void acia_check_irq(struct acia *acia)
{
	if (acia_irq_pending(acia))
		int_set(IRQ_ACIA);
	else
		int_clear(IRQ_ACIA);
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

static uint8_t do_i8085_inport(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr >= 0xA0 && addr <= 0xA7) && acia)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if ((addr == 0x98 || addr == 0x99) && vdp)
		return tms9918a_read(vdp, addr & 1);
	if (addr == 0x0C && rtc)
		return rtc_read(rtcdev);
	if (addr >= 0xC0 && addr <= 0xCF && uart)
		return uart16x50_read(uart, addr & 0x0F);
	if (addr >= 0x58 && addr <= 0x5F && ncr)
		return ncr5380_read(ncr, addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

uint8_t i8085_inport(uint8_t addr)
{
	uint8_t r = do_i8085_inport(addr);
	/* Get the IRQ flag back righr */
	poll_irq_event();
	return r;
}

void i8085_outport(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if (addr == 0xFF && bankhigh) {
		mmureg = val;
		if (trace & TRACE_512)
			fprintf(stderr, "MMUreg set to %02X\n", val);
	} else if ((addr >= 0xA0 && addr <= 0xA7) && acia)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		my_ide_write(addr & 7, val);
	else if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (bank512 && addr >= 0x78 && addr <= 0x7B) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(stderr, "Bank %d set to %d\n", addr & 3, val);
	} else if (bank512 && addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_512)
			fprintf(stderr, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0x0C && rtc)
		rtc_write(rtcdev, val);
	else if ((addr == 0x98 || addr == 0x99) && vdp)
		tms9918a_write(vdp, addr & 1, val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		uart16x50_write(uart, addr & 0x0F, val);
	else if (addr >= 0x58 && addr <= 0x5F && ncr)
		ncr5380_write(ncr, addr & 7, val);
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
	poll_irq_event();
}

/* For now we don't emulate the bitbang port */

int i8085_get_input(void)
{
	return 0;
}

/* And we emulate wiring the SIM bit to M1 */

void i8085_set_output(int value)
{
}

static void poll_irq_event(void)
{
	if (acia)
		acia_check_irq(acia);
	if (uart) {
		if (uart16x50_irq_pending(uart))
			int_set(IRQ_16550A);
		else
			int_clear(IRQ_16550A);
	}
	if (vdp && tms9918a_irq_pending(vdp))
		int_set(IRQ_TMS9918A);
	else
		int_clear(IRQ_TMS9918A);
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
	fprintf(stderr, "rcbus-8085: [-1] [-a] [-b] [-B] [-e rombank] [-f] [-i idepath] [-I ppidepath] [-R] [-r rompath] [-e rombank] [-w] [-d debug] [-S symbols]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	int rombank = 0;
	char *rompath = "rcbus-8085.rom";
	char *sasipath = NULL;
	char *idepath;
	int acia_input;
	int uart_16550a = 0;

	while ((opt = getopt(argc, argv, "1abBd:e:fi:I:N:r:RwS:T")) != -1) {
		switch (opt) {
		case '1':
			uart_16550a = 1;
			acia_uart = 0;
			break;
		case 'a':
			acia_uart = 1;
			acia_input = 1;
			uart_16550a = 0;
			break;
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
		case 'N':
			sasipath = optarg;
			break;
		case 'R':
			rtc = 1;
			break;
		case 'T':
			have_tms = 1;
			break;
		case 'w':
			wiznet = 1;
			break;
		case 'S':
			i8085_load_symbols(optarg);
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (acia_uart == 0 && uart_16550a == 0) {
		fprintf(stderr, "rcbus-8085: no UART selected, defaulting to 68B50\n");
		acia_uart = 1;
		acia_input = 1;
	}
	if (rom == 0 && bank512 == 0 && bankhigh == 0) {
		fprintf(stderr, "rcbus-8085: no ROM\n");
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
			fprintf(stderr, "rcbus-8085: short rom '%s'.\n", rompath);
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
			fprintf(stderr, "rcbus-8085: banked rom image should be 512K.\n");
			exit(EXIT_FAILURE);
		}
		bankenable = 1;
		close(fd);
	}

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
	if (sasipath) {
		sasi = sasi_bus_create();
		sasi_disk_attach(sasi, 0, sasipath, 512);
		sasi_bus_reset(sasi);
		ncr = ncr5380_create(sasi);
		ncr5380_trace(ncr, trace & TRACE_SCSI);
	}


	if (acia_uart) {
		acia = acia_create();
		if (trace & TRACE_ACIA)
			acia_trace(acia, 1);
		acia_attach(acia, acia_input ? &console : &console_wo);
	}
	if (uart_16550a) {
		uart = uart16x50_create();
		if (trace & TRACE_UART)
			uart16x50_trace(uart, 1);
		uart16x50_attach(uart, acia_input ? & console_wo : &console);
	}

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

	i8085_reset();
	if (trace & TRACE_CPU) {
		i8085_log = stderr;
	}

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
			i8085_exec(tstate_steps);
			if (acia)
				acia_timer(acia);
			if (uart_16550a)
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
		if (wiznet)
			w5100_process(wiz);
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
