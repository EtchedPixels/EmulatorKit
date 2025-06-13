/*
 *	Platform features
 *	Z80A @ 20Mhz
 *	544K RAM
 *	32K EEPROM
 *	4 x 16550A UART @14.7456MHz (actually 2 x 16C2550A)
 *	GIDE
 *	Memory banking (32K)
 *	72421B RTC
 *
 *	Unimplemented at this point
 *	82077A/PC8477B FDC including 1.44MB
 *	RTC setting
 *
 *	The only IRQ on this system is from the RTC.
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
#include <sys/types.h>
#include <sys/mman.h>
#include "libz80/z80.h"
#include "ide.h"

static uint8_t eeprom[32768];
static uint8_t fixedram[32768];
static uint8_t bankedram[16][32768];
static uint8_t gpreg;

static struct ide_controller *ide0;
static uint8_t fast;
static uint8_t ide;

static Z80Context cpu_z80;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	4
#define TRACE_RTC	8
#define TRACE_BANK	16
#define TRACE_UART	32
#define TRACE_IDE	64
#define TRACE_LED	128

static int trace = 0;

static uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t r;
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X: [", addr);

	if ((gpreg & 0x80) == 0 && addr < 0x8000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "EE");
		r = eeprom[addr];
	} else if (addr < 0x4000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "LF");
		r = fixedram[addr];
	} else if (addr >= 0xC000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "HF");
		r = fixedram[addr - 0x8000];
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%2d", gpreg & 0x0F);
		r = bankedram[gpreg&0x0F][addr - 0x4000];
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "] -> %02X\n", r);
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t *m;

	if ((gpreg & 0x80) == 0 && addr < 0x8000) {
		fprintf(stderr, "EEPROM write not yet emulated.\n");
		return;
	}

	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X: [", addr);
	if (addr < 0x4000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "LF");
		m = fixedram + addr;
	} else if (addr >= 0xC000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "HF");
		m = fixedram + addr - 0x8000;
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%2d", gpreg&0x0F);
		m = &bankedram[gpreg&0x0F][addr - 0x4000];
	}
	*m = val;
	if (trace & TRACE_MEM)
		fprintf(stderr, "] <- %02X\n", val);
}

static int check_chario(void)
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
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

static unsigned int next_char(void)
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

static struct uart16x50 uart[4];

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
		return;
	}
	/* Ok so we have an event, do we need to waggle the line */
	if (uptr->irqline)
		return;
	uptr->irqline = uptr->irq;
#ifdef UART_IRQ
	Z80INT(&cpu_z80, 0xFF);		/* actually undefined */
#endif
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
		baud = 14745600;
	baud = 14745600 / baud;
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
			if (uptr == &uart[0]) {
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
		/* receive buffer */
		if (uptr == &uart[0] && uptr->dlab == 0) {
			uart_clear_interrupt(uptr, RXDA);
			return next_char();
		}
		break;
	case 1:
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
		if (uptr != &uart[0])
			return 0x60;
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

uint8_t fdc_read(uint8_t addr)
{
	return 0xFF;
}

void fdc_write(uint8_t addr, uint8_t val)
{
}

static struct tm *tmhold;
uint8_t rtc_status = 2;
uint8_t rtc_ce = 0;
uint8_t rtc_cf = 0;

uint8_t do_rtc_read(uint8_t addr)
{
	uint8_t r;
	if (tmhold == NULL && addr < 13)
		return 0xFF;

	switch(addr & 0x0F) {
	case 0:
		return tmhold->tm_sec % 10;
	case 1:
		return tmhold->tm_sec / 10;
	case 2:
		return tmhold->tm_min % 10;
	case 3:
		return tmhold->tm_min / 10;
	case 4:
		return tmhold->tm_hour % 10;
	case 5:
		/* Check AM/PM behaviour */
		r = tmhold->tm_hour;
		if (rtc_cf & 4)		/* 24hr */
			r /= 10;
		else if (r >= 12) {	/* 12hr PM */
			r -= 12;
			r /= 10;
			r |= 4;
		} else			/* 12hr AM */
			r /= 10;
		return r;
	case 6:
		return tmhold->tm_mday % 10;
	case 7:
		return tmhold->tm_mday / 10;
	case 8:
		return tmhold->tm_mon % 10;
	case 9:
		return tmhold->tm_mon / 10;
	case 10:
		return tmhold->tm_year % 10;
	case 11:
		return (tmhold->tm_year %100) / 10;
	case 12:
		return tmhold->tm_wday;
	case 13:
		return rtc_status;
	case 14:
		return rtc_ce;
	case 15:
		return 4;
	}
	#pragma GCC diagnostic ignored "-Wreturn-type"
}

uint8_t rtc_read(uint8_t addr)
{
	uint8_t v = do_rtc_read(addr);
	if (trace & TRACE_RTC)
		fprintf(stderr, "[RTC read %x of %X[\n", addr, v);
	return v;
}

void rtc_write(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_RTC)
		fprintf(stderr, "[RTC write %X to %X]\n", addr, val);
	switch(addr) {
		case 13:
			if ((val & 0x04) == 0)
				rtc_status &= ~4;
			if (val & 0x01) {
				time_t t;
				rtc_status &= ~2;
				time(&t);
				tmhold = gmtime(&t);
			} else
				rtc_status |= 2;
			/* FIXME: sort out hold behaviour */
			break;
		case 14:
			rtc_ce = val & 0x0F;
			break;
		case 15:
			rtc_cf = val & 0x0F;
			break;
	}
}

/* +6 is the IDE altstatus/devctrl and the drive address register */
/* 8-15 are the taskfile registers */

/* The 16bit data is latched so that inir/otir work */

static uint16_t gide_latch;
static uint8_t gide_lstate;

static uint8_t gide_read(uint16_t addr)
{
	uint8_t r = 0xFF;
	if (addr == 6)
		r =  ide_read16(ide0, ide_altst_r);
	else if (addr > 8) {
		r = ide_read16(ide0, addr & 7);
		gide_lstate = 0;
	}
	else if (addr == 8) {
		if (gide_lstate) {
			gide_lstate = 0;
			return gide_latch >> 8;
		}
		gide_latch = ide_read16(ide0, ide_data);
		gide_lstate = 1;
		return gide_latch;

	}
	if (trace & TRACE_IDE)
		fprintf(stderr, "IDE read %d = %02X\n", addr, r);
	return r;
}

static void gide_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IDE)
		fprintf(stderr, "IDE write %d = %02X\n", addr, val);
	if (addr == 6)
		ide_write16(ide0, ide_devctrl_w, val);
	if (addr > 8) {
		gide_lstate = 0;
		ide_write16(ide0, addr & 7, val);
	}
	else if (addr == 8) {
		if (gide_lstate == 0) {
			gide_lstate = 1;
			gide_latch = val;
		} else {
			ide_write16(ide0, ide_data, gide_latch | (val << 8));
			gide_lstate = 0;
		}
	}
}

static void ledmod(const char *name, int onoff)
{
	printf("[%s LED now %s].\n", name, onoff?"on":"off");
}

void gp_write(uint8_t val)
{
	uint8_t delta = val ^ gpreg;

	if (trace & TRACE_BANK) {
		if (delta & 0x0F)
			fprintf(stderr, "[Bank set to %d].\n", val & 0x0F);
	}
	if (trace & TRACE_LED) {
		if (delta & 0x10)
			ledmod("Red", val & 0x10);
		if (delta & 0x20)
			ledmod("Yellow", val & 0x20);
		if (delta & 0x40)
			ledmod("Green", val & 0x40);
	}
	gpreg = val;
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x10 && addr <= 0x17)
		return uart_read(&uart[0], addr & 7);
	if (addr >= 0x18 && addr <= 0x1F)
		return uart_read(&uart[1], addr & 7);
	if (addr >= 0x48 && addr <= 0x4F)
		return uart_read(&uart[2], addr & 7);
	if (addr >= 0x50 && addr <= 0x57)
		return uart_read(&uart[3], addr & 7);
	if (addr >= 0x20 && addr <= 0x2F)
		return rtc_read(addr & 0x0F);
	if (addr >= 0x30 && addr <= 0x3F)
		return gide_read(addr & 0x0F);
	if (addr >= 0x40 && addr <= 0x47)
		return fdc_read(addr & 0x07);
	if (addr == 0xF8)
		return gpreg;
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr & 0xFF, val);
	addr &= 0xFF;
	if (addr >= 0x10 && addr <= 0x17)
		uart_write(&uart[0], addr & 7, val);
	else if (addr >= 0x18 && addr <= 0x1F)
		uart_write(&uart[1], addr & 7, val);
	else if (addr >= 0x48 && addr <= 0x4F)
		uart_write(&uart[2], addr & 7, val);
	else if (addr >= 0x50 && addr <= 0x57)
		uart_write(&uart[3], addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x2F)
		rtc_write(addr & 0x0F, val);
	else if(addr >= 0x30 && addr <= 0x3F)
		gide_write(addr & 0x0F, val);
	else if (addr >= 0x40 && addr <= 0x47)
		fdc_write(addr & 0x07, val);
	else if (addr == 0xF8)
		gp_write(val);
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	}
	else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %02X of %02X\n",
			addr & 0xFF, val);
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
	fprintf(stderr, "smallz80: [-f] [-r rompath] [-i idepath] [-d tracemask]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "smallz80.rom";
	char *idepath[2] = { NULL, NULL };

	while((opt = getopt(argc, argv, "r:i:d:f")) != -1) {
		switch(opt) {
			case 'r':
				rompath = optarg;
				break;
			case 'i':
				if (ide == 2)
					fprintf(stderr, "smallz80: only two disks per controller.\n");
				else
					idepath[ide++] = optarg;
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
	if (read(fd, eeprom, 32768) != 32768) {
		fprintf(stderr, "smallz80: ROM image should be 32K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	ide0 = ide_allocate("cf");
	if (ide0) {
		fd = open(idepath[0], O_RDWR);
		if (fd == -1)
			perror(idepath[0]);
		else
			ide_attach(ide0, 0, fd);

		if (idepath[1]) {
			fd = open(idepath[1], O_RDWR);
			if (fd == -1)
				perror(idepath[1]);
			else
				ide_attach(ide0, 1, fd);
		}
	} else {
		fprintf(stderr, "smallz80: unable to initialize IDE emulation.\n");
	}

	ide_reset_begin(ide0);

	uart_init(&uart[0]);
	uart_init(&uart[1]);
	uart_init(&uart[2]);
	uart_init(&uart[3]);

	/* 1/64th of a second */
	tc.tv_sec = 0;
	tc.tv_nsec = 15625000L;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON|ECHO);
		term.c_cc[VMIN] = 1;
		term.c_cc[VTIME] = 0;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VSTOP] = 0;
		tcsetattr(0, TCSADRAIN, &term);
	}

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* 20MHz Z80 - 20,000,000 tstates / second */
	/* 312500 tstates per RTC interrupt */
	while (!done) {
		int i;
		/* Run the CPU and uart until the next tick */
		for (i = 0; i < 10; i++) {
			if (!(rtc_ce & 1) && (rtc_status & 4))
				Z80INT(&cpu_z80, 0xFF);
			Z80ExecuteTStates(&cpu_z80, 31250);
			uart_event(&uart[0]);
		}
		rtc_status |= 4;
		/* Do 1/64th of a second of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
