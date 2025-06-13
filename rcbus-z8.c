/*
 *	Platform features
 *
 *	Z8 at 7.3728MHz
 *	Internal serial
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
#include "z8.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t bankhigh = 0;
static uint8_t mmureg[2] = { 0, 0 };
static uint8_t rtc;
static uint8_t fast = 0;
static uint8_t wiznet = 0;

struct ppide *ppide;
struct rtc *rtcdev;

static uint16_t mcycles = 368;	/* Clocks per 50us */

/* Who is pulling on the interrupt line */

static uint8_t live_irq;

#define IRQ_16550A	1

static nic_w5100_t *wiz;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_LED	16
#define TRACE_PPIDE	32
#define TRACE_RTC	64
#define TRACE_CPU	128
#define TRACE_UART	256
#define TRACE_512	512

static int trace = 0;

struct z8 *cpu;

/*
 *	Ports are a TODO
 *
 *	Allocation is as follows
 *
 *	P0/P1 - address/data bus
 *
 *	P2:
 *	7:	MOSI
 * 	6:	IOCS
 *	5:	CS1
 *	4:	CTS
 *	3:	P23 }
 *	2:	P22 }	Unassigned
 *	1:	P21 }
 *	0:	MISO
 *
 *	P3:
 *	7:	Serial TX (altfunc)
 *	6:	Used for SPI clock
 *	5:	CS0
 * 	4:	Data/Code (altfunc)
 *	3:	IRQ1
 *	2:	IRQ0	- RCbus interrupt
 *	1:	IRQ2
 *	0:	Serial RX (altfunc)
 *
 *	I/O space is activated for the upper 32K of data space if IOCS is low
 *	otherwise it is treated as a RAM data access
 *
 *	MMU board at I/O FE/FF
 *
 *	FE	Bank low, bank high (low 56K, high 8K) 4bits
 *	FF	Ditto for code memory, starts 0
 *
 *	Physical memory is 16 x 64K banks, low half ROM rest RAM
 *	(may change so ROM is also controlled by a P2 pin) and 1MB
 */

static uint8_t zport[4];

uint8_t z8_port_read(struct z8 *z8, uint8_t port)
{
	return 0xFF;
}

void z8_port_write(struct z8 *z8, uint8_t port, uint8_t val)
{
	zport[port] = val;
}

void z8_tx(struct z8 *z8, uint8_t ch)
{
	write(1, &ch, 1);
}

int check_chario(void)
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

static void int_event(void)
{
	int c = check_chario();
	if (c & 1)
		z8_rx_char(cpu, next_char());
	if (c & 2)
		z8_tx_done(cpu);
}

void recalc_interrupts(void)
{
	if (live_irq)
		z8_raise_irq(cpu, 0);
	else
		z8_clear_irq(cpu, 0);
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

/* UART: very mimimal for the moment */

struct uart16x50 {
	uint8_t ier;
	uint8_t iir;
	uint8_t fcr;
	uint8_t lcr;
	uint8_t mcr;
	uint8_t lsr;
	uint8_t msr;
	uint8_t scratch;
	uint8_t ls;
	uint8_t ms;
	uint8_t dlab;
	uint8_t irq;
#define RXDA	1
#define TEMT	2
#define MODEM	8
	uint8_t irqline;
};

static struct uart16x50 uart;
static unsigned int uart_16550a;

static void uart_init(struct uart16x50 *uptr)
{
	uptr->dlab = 0;
}

/* Compute the interrupt indicator register from what is pending */
static void uart_recalc_iir(struct uart16x50 *uptr)
{
	if (uptr->irq & RXDA)
		uptr->iir = 0x04;
	else if (uptr->irq & TEMT)
		uptr->iir = 0x02;
	else if (uptr->irq & MODEM)
		uptr->iir = 0x00;
	else {
		uptr->iir = 0x01;	/* No interrupt */
		uptr->irqline = 0;
		int_clear(IRQ_16550A);
		return;
	}
	/* Ok so we have an event, do we need to waggle the line */
	if (uptr->irqline)
		return;
	uptr->irqline = uptr->irq;
	int_set(IRQ_16550A);
}

/* Raise an interrupt source. Only has an effect if enabled in the ier */
static void uart_interrupt(struct uart16x50 *uptr, uint8_t n)
{
	if (uptr->irq & n)
		return;
	if (!(uptr->ier & n))
		return;
	uptr->irq |= n;
	uart_recalc_iir(uptr);
}

static void uart_clear_interrupt(struct uart16x50 *uptr, uint8_t n)
{
	if (!(uptr->irq & n))
		return;
	uptr->irq &= ~n;
	uart_recalc_iir(uptr);
}

static void uart_event(struct uart16x50 *uptr)
{
	uint8_t r = check_chario();
	uint8_t old = uptr->lsr;
	uint8_t dhigh;
	if (r & 1)
		uptr->lsr |= 0x01;	/* RX not empty */
	if (r & 2)
		uptr->lsr |= 0x60;	/* TX empty */
	dhigh = (old ^ uptr->lsr);
	dhigh &= uptr->lsr;		/* Changed high bits */
	if (dhigh & 1)
		uart_interrupt(uptr, RXDA);
	if (dhigh & 0x2)
		uart_interrupt(uptr, TEMT);
}

static void show_settings(struct uart16x50 *uptr)
{
	uint32_t baud;

	if (!(trace & TRACE_UART))
		return;

	baud = uptr->ls + (uptr->ms << 8);
	if (baud == 0)
		baud = 1843200;
	baud = 1843200 / baud;
	baud /= 16;
	fprintf(stderr, "[%d:%d",
		baud, (uptr->lcr &3) + 5);
	switch(uptr->lcr & 0x38) {
		case 0x00:
		case 0x10:
		case 0x20:
		case 0x30:
			fprintf(stderr, "N");
			break;
		case 0x08:
			fprintf(stderr, "O");
			break;
		case 0x18:
			fprintf(stderr, "E");
			break;
		case 0x28:
			fprintf(stderr, "M");
			break;
		case 0x38:
			fprintf(stderr, "S");
			break;
	}
	fprintf(stderr, "%d ",
		(uptr->lcr & 4) ? 2 : 1);

	if (uptr->lcr & 0x40)
		fprintf(stderr, "break ");
	if (uptr->lcr & 0x80)
		fprintf(stderr, "dlab ");
	if (uptr->mcr & 1)
		fprintf(stderr, "DTR ");
	if (uptr->mcr & 2)
		fprintf(stderr, "RTS ");
	if (uptr->mcr & 4)
		fprintf(stderr, "OUT1 ");
	if (uptr->mcr & 8)
		fprintf(stderr, "OUT2 ");
	if (uptr->mcr & 16)
		fprintf(stderr, "LOOP ");
	fprintf(stderr, "ier %02x]\n", uptr->ier);
}

static void uart_write(struct uart16x50 *uptr, uint8_t addr, uint8_t val)
{
	switch(addr) {
	case 0:	/* If dlab = 0, then write else LS*/
		if (uptr->dlab == 0) {
			if (uptr == &uart) {
				putchar(val);
				fflush(stdout);
			}
			uart_clear_interrupt(uptr, TEMT);
			uart_interrupt(uptr, TEMT);
		} else {
			uptr->ls = val;
			show_settings(uptr);
		}
		break;
	case 1:	/* If dlab = 0, then IER */
		if (uptr->dlab) {
			uptr->ms= val;
			show_settings(uptr);
		}
		else
			uptr->ier = val;
		break;
	case 2:	/* FCR */
		uptr->fcr = val & 0x9F;
		break;
	case 3:	/* LCR */
		uptr->lcr = val;
		uptr->dlab = (uptr->lcr & 0x80);
		show_settings(uptr);
		break;
	case 4:	/* MCR */
		uptr->mcr = val & 0x3F;
		show_settings(uptr);
		break;
	case 5:	/* LSR (r/o) */
		break;
	case 6:	/* MSR (r/o) */
		break;
	case 7:	/* Scratch */
		uptr->scratch = val;
		break;
	}
}

static uint8_t uart_read(struct uart16x50 *uptr, uint8_t addr)
{
	uint8_t r;

	switch(addr) {
	case 0:
		if (uptr->dlab)
			return uptr->ls;
		/* receive buffer */
		if (uptr == &uart && uptr->dlab == 0) {
			uart_clear_interrupt(uptr, RXDA);
			return next_char();
		}
		break;
	case 1:
		if (uptr->dlab)
			return uptr->ms;
		/* IER */
		return uptr->ier;
	case 2:
		/* IIR */
		return uptr->iir;
	case 3:
		/* LCR */
		return uptr->lcr;
	case 4:
		/* mcr */
		return uptr->mcr;
	case 5:
		/* lsr */
		r = check_chario();
		uptr->lsr = 0;
		if (r & 1)
			uptr->lsr |= 0x01;	/* Data ready */
		if (r & 2)
			uptr->lsr |= 0x60;	/* TX empty | holding empty */
		/* Reading the LSR causes these bits to clear */
		r = uptr->lsr;
		uptr->lsr &= 0xF0;
		return r;
	case 6:
		/* msr */
		r = uptr->msr;
		/* Reading clears the delta bits */
		uptr->msr &= 0xF0;
		uart_clear_interrupt(uptr, MODEM);
		return r;
	case 7:
		return uptr->scratch;
	}
	return 0xFF;
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

/* Real time clock state machine and related state.

   Give the host time and don't emulate time setting except for
   the 24/12 hour setting.

 */

uint8_t z8_inport(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
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
	else if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		return uart_read(&uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void z8_outport(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if (addr == 0xFF && bankhigh) {
		mmureg[1] = val;
		if (trace & TRACE_512)
			fprintf(stderr, "MMUreg E set to %02X\n", val);
	} else if (addr == 0xFE && bankhigh) {
		mmureg[0]= val;
		if (trace & TRACE_512)
			fprintf(stderr, "MMUreg C set to %02X\n", val);
	} else if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
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
	else if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		uart_write(&uart, addr & 0x0F, val);
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
uint8_t z8_do_read(struct z8 *cpu, unsigned space, uint16_t addr, int debug)
{
	if (space == 1 && (addr & 0x8000) && !(zport[2] & 0x40)) {
		if (debug)
			return 0xFF;
		return z8_inport(addr);
	}
	if (bankhigh) {
		uint8_t reg = mmureg[space];
		uint8_t val;
		uint32_t higha;
		if (addr < 0xE000)
			reg >>= 1;
		higha = (reg & 0x40) ? 1 : 0;
		higha |= (reg & 0x10) ? 2 : 0;
		higha |= (reg & 0x4) ? 4 : 0;
		higha |= (reg & 0x01) ? 8 : 0;	/* ROM/RAM */

		val = ramrom[(higha << 16) + addr];
		if (!debug && (trace & TRACE_MEM)) {
			fprintf(stderr, "R%c %04X[%02X] = %02X\n",
				"CE"[space],
				(unsigned int)addr,
				(unsigned int)higha,
				(unsigned int)val);
		}
		return val;
	} else 	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (!debug && (trace & TRACE_MEM))
			fprintf(stderr, "R %04x[%02X] = %02X\n", addr, (unsigned int) bankreg[bank], (unsigned int) ramrom[(bankreg[bank] << 14) + (addr & 0x3FFF)]);
		addr &= 0x3FFF;
		return ramrom[(bankreg[bank] << 14) + addr];
	}
	if (!debug && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

uint8_t z8_read_data(struct z8 *cpu, uint16_t addr)
{
	return z8_do_read(cpu, 1, addr, 0);
}

uint8_t z8_read_code(struct z8 *cpu, uint16_t addr)
{
	return z8_do_read(cpu, 0, addr, 0);
}

uint8_t z8_read_code_debug(struct z8 *cpu, uint16_t addr)
{
	return z8_do_read(cpu, 0, addr, 1);
}

void z8_do_write(struct z8 *cpu, unsigned space, uint16_t addr, uint8_t val)
{
	if (space == 1 && (addr & 0x8000) && !(zport[2] & 0x40)) {
		z8_outport(addr, val);
		return;
	}
	if (bankhigh) {
		uint8_t reg = mmureg[space];
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

void z8_write_code(struct z8 *z8, uint16_t addr, uint8_t val)
{
	z8_do_write(z8, 0, addr, val);
}

void z8_write_data(struct z8 *z8, uint16_t addr, uint8_t val)
{
	z8_do_write(z8, 1, addr, val);
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
	fprintf(stderr, "rcbus-z8: [-1] [-b] [-B] [-e bank] [-f] [-i cfidepath] [-I ppidepath]\n             [-R] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	int rombank = 0;
	char *rompath = "rcbus-z8.rom";
	char *idepath;

	while ((opt = getopt(argc, argv, "1bBd:e:fi:I:r:Rt:w")) != -1) {
		switch (opt) {
		case '1':
			uart_16550a = 1;
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
		case 'w':
			wiznet = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (uart_16550a == 0)
		fprintf(stderr, "rcbus: no UART selected, defaulting to internal\n");

	if (rom == 0 && bank512 == 0 && bankhigh == 0) {
		fprintf(stderr, "rcbus: no ROM\n");
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
			fprintf(stderr, "rcbus: short rom '%s'.\n", rompath);
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
			fprintf(stderr, "rcbus: banked rom image should be 512K.\n");
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

	if (uart_16550a)
		uart_init(&uart);

	cpu = z8_create();
	z8_reset(cpu);
	if (trace & TRACE_CPU)
		z8_set_trace(cpu, 1);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		int i;
		/* 36400 T states for base rcbus - varies for others */
		for (i = 0; i < 100; i++) {
			/* TODO: need to loop for the desired tstates */
			cpu->cycles = 0;
			while(cpu->cycles < mcycles)
				z8_execute(cpu);
			if (uart_16550a)
				uart_event(&uart);
			else {
				int_event();
			}
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
