/*
 *	Platform features
 *
 *	1802 at 6.144MHz (FIXME - sensible speed!)
 *	Motorola 6850 at 0xA0-0xA7
 *	IDE at 0x10-0x17 no high or control access (mirrored at 0x90-97)
 *	PPIDE at 0x20
 *	Simple memory 32K ROM / 32K RAM
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	Etched Pixels MMU at 0xFF
 *	RTC at 0x0C
 *	16550A at 0xC0
 *	WizNET ethernet
 *
 *	TODO:
 *	Possibly emulate the graphics option
 *	More accurate clock rate
 *	CPU debug tracing
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
#include "1802.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"
#include "16x50.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "w5100.h"

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

static struct ppide *ppide;
static struct rtc *rtcdev;
static struct acia *acia;
static struct uart16x50 *uart;
static nic_w5100_t *wiz;

static uint16_t mcycles = 10;	/* Machine cycles per sequence. The 1802
				   is pretty slow */

/* Who is pulling on the interrupt line */

static uint8_t live_irq;

#define IRQ_ACIA	1
#define IRQ_16550A	2


static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_LED	16
#define TRACE_PPIDE	32
#define TRACE_RTC	64
#define TRACE_ACIA	128
#define TRACE_UART	256
#define TRACE_512	512

static int trace = 0;

struct cp1802 cpu;

/* Pulled high except for non emulated PIXIE */
uint8_t cp1802_ef(struct cp1802 *cpu)
{
	return 0xFF;
}

void cp1802_q_set(struct cp1802 *cpu)
{
	/* Whatever - maybe log this */
}

void cp1802_out(struct cp1802 *cpu, uint8_t r, uint8_t v)
{
	if (r == 0)
		return;
	/* TODO */
}

uint8_t cp1802_in(struct cp1802 *cpu, uint8_t r)
{
	if (r == 0)
		return 0xFF;
	/* TODO */
	return 0xFF;
}

uint8_t cp1802_dma_in(struct cp1802 *cpu)
{
	return 0xFF;	/* TODO */
}

void cp1802_dma_out(struct cp1802 *cpu, uint8_t val)
{
	/* TODO */
}

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
	cp1802_interrupt(&cpu, live_irq);
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

uint8_t cp1802_inport(uint8_t addr)
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
	if (addr == 0x0C && rtc)
		return rtc_read(rtcdev);
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		return uart16x50_read(uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void cp1802_outport(uint8_t addr, uint8_t val)
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
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		uart16x50_write(uart, addr & 0x0F, val);
	else if (addr == 0x80) {
		if (trace & TRACE_LED)
			printf("[%02X]\n", val);
	} else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

/* FIXME: emulate paging off correctly, also be nice to emulate with less
   memory fitted */
uint8_t cp1802_read(struct cp1802 *cpu, uint16_t addr)
{
	if (addr >> 8 == 0xFF)
		return cp1802_inport(addr & 0xFF);
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

void cp1802_write(struct cp1802 *cpu, uint16_t addr, uint8_t val)
{
	if (addr >> 8 == 0xFF) {
		cp1802_outport(addr & 0xFF, val);
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
		if (addr >= 32768 && !bank512)
			ramrom[addr] = val;
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	}
}

static void poll_irq_event(void)
{
	if (acia) {
		if (acia_irq_pending(acia))
			int_set(IRQ_ACIA);
		else
			int_clear(IRQ_ACIA);
	}
	if (uart) {
		if (uart16x50_irq_pending(uart))
			int_set(IRQ_16550A);
		else
			int_clear(IRQ_16550A);
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
	fprintf(stderr, "rcbus-1802: [-1] [-A] [-b] [-B] [-e bank] [-f] [-i cfidepath] [-I ppidepath]\n             [-R] [-r rompath] [-t type] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int type = 1802;
	int opt;
	int fd;
	int rom = 1;
	int rombank = 0;
	char *rompath = "rcbus-1802.rom";
	char *idepath;
	int acia_input;
	int uart_16550a = 0;

	while ((opt = getopt(argc, argv, "1abBd:e:fi:I:r:Rt:w")) != -1) {
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
		case 'R':
			rtc = 1;
			break;
		case 't':
			type = atoi(optarg);
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

	if (type != 1802 && type != 1804 && type != 1805 && type != 1806) {
		fprintf(stderr, "rcbus-1802: unknown CPU type. Please select from 1802, 1804, 1805, 1806.\n");
		exit(1);
	}
	if (acia_uart == 0 && uart_16550a == 0) {
		fprintf(stderr, "rcbus-1802: no UART selected, defaulting to 68B50\n");
		acia_uart = 1;
		acia_input = 1;
	}
	if (rom == 0 && bank512 == 0 && bankhigh == 0) {
		fprintf(stderr, "rcbus-1802: no ROM\n");
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
			fprintf(stderr, "rcbus-1802: short rom '%s'.\n", rompath);
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
			fprintf(stderr, "rcbus-1802: banked rom image should be 512K.\n");
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

	if (acia_uart) {
		acia = acia_create();
		acia_trace(acia, trace & TRACE_ACIA);
		acia_attach(acia, &console);
	}

	if (uart_16550a) {
		uart = uart16x50_create();
		uart16x50_trace(uart, trace & TRACE_UART);
		if (acia_input)
			uart16x50_attach(uart, &console_wo);
		else
			uart16x50_attach(uart, &console);
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

	cp1802_init(&cpu, type);

//	if (trace & TRACE_CPU)
//		cp1802_set_debug();

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		int i;
		/* 36400 T states for base rcbus - varies for others */
		for (i = 0; i < 100; i++) {
			/* TODO: need to loop for the desired tstates */
			cpu.mcycles = 0;
			while(cpu.mcycles < mcycles)
				cp1802_run(&cpu);
			if (acia)
				acia_timer(acia);
			if (uart)
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
