/*
 *	Platform features
 *
 *	65C816 processor card for RC2014 with 16bit addressing and I/O at $FExx 
 *	Motorola 68B50
 *	IDE at 0x10-0x17 no high or control access
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at 0x0C
 *	16550A at 0xC0
 *	Minimal 6522VIA emulation
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
#include "ide.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t rtc = 0;
static uint8_t fast = 0;
static uint8_t wiznet = 0;
static uint8_t iopage = 0xFE;

static uint16_t tstate_steps = 200;

/* Who is pulling on the interrupt line */

#define IRQ_ACIA	1
#define IRQ_16550A	2
#define IRQ_VIA		3

static nic_w5100_t *wiz;

/* static volatile int done; */

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

	if (select(2, &i, NULL, NULL, &tv) == -1) {
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

static uint8_t acia_status = 2;
static uint8_t acia_config;
static uint8_t acia_char;
static uint8_t acia;
static uint8_t acia_input;
static uint8_t acia_inint = 0;
static uint8_t acia_narrow;

static void acia_irq_compute(void)
{
	if (!acia_inint && acia_config && acia_status & 0x80) {
		if (trace & TRACE_ACIA)
			fprintf(stderr, "ACIA interrupt.\n");
		acia_inint = 1;
		CPU_addIRQ(IRQ_ACIA);
	}
	if (acia_inint) {
		CPU_clearIRQ(IRQ_ACIA);
		acia_inint = 0;
	}
}

static void acia_receive(void)
{
	uint8_t old_status = acia_status;
	acia_status = old_status & 0x02;
	if (old_status & 1)
		acia_status |= 0x20;
	acia_char = next_char();
	if (trace & TRACE_ACIA)
		fprintf(stderr, "ACIA rx.\n");
	acia_status |= 0x81;	/* IRQ, and rx data full */
}

static void acia_transmit(void)
{
	if (!(acia_status & 2)) {
		if (trace & TRACE_ACIA)
			fprintf(stderr, "ACIA tx is clear.\n");
		acia_status |= 0x82;	/* IRQ, and tx data empty */
	}
}

static void acia_timer(void)
{
	int s = check_chario();
	if ((s & 1) && acia_input)
		acia_receive();
	if (s & 2)
		acia_transmit();
	if (s)
		acia_irq_compute();
}

static uint8_t acia_read(uint8_t addr)
{
	if (trace & TRACE_ACIA)
		fprintf(stderr, "acia_read %d ", addr);
	switch (addr) {
	case 0:
		/* bits 7: irq pending, 6 parity error, 5 rx over
		 * 4 framing error, 3 cts, 2 dcd, 1 tx empty, 0 rx full.
		 * Bits are set on char arrival and cleared on next not by
		 * user
		 */
		acia_status &= ~0x80;
		acia_irq_compute();
		if (trace & TRACE_ACIA)
			fprintf(stderr, "acia_status %d\n", acia_status);
		return acia_status;
	case 1:
		acia_status &= ~0x81;	/* No IRQ, rx empty */
		acia_irq_compute();
		if (trace & TRACE_ACIA)
			fprintf(stderr, "acia_char %d\n", acia_char);
		return acia_char;
	default:
		fprintf(stderr, "acia: bad addr.\n");
		exit(1);
	}
}

static void acia_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_ACIA)
		fprintf(stderr, "acia_write %d %d\n", addr, val);
	switch (addr) {
	case 0:
		/* bit 7 enables interrupts, bits 5-6 are tx control
		   bits 2-4 select the word size and 0-1 counter divider
		   except 11 in them means reset */
		acia_config = val;
		if ((acia_config & 3) == 3)
			acia_status = 2;
		acia_irq_compute();
		return;
	case 1:
		write(1, &val, 1);
		/* Clear any existing int state and tx empty */
		acia_status &= ~0x82;
		break;
	}
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
        CPU_clearIRQ(IRQ_16550A);
        return;
    }
    /* Ok so we have an event, do we need to waggle the line */
    if (uptr->irqline)
        return;
    uptr->irqline = uptr->irq;
    CPU_addIRQ(IRQ_16550A);
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
        /* receive buffer */
        if (uptr == &uart && uptr->dlab == 0) {
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
static uint8_t rtcw;
static uint8_t rtcst;
static uint16_t rtcr;
static uint8_t rtccnt;
static uint8_t rtcstate;
static uint8_t rtcreg;
static uint8_t rtcram[32];
static uint8_t rtcwp = 0x80;
static uint8_t rtc24 = 1;
static uint8_t rtcbp = 0;
static uint8_t rtcbc = 0;
static struct tm *rtc_tm;

static uint8_t rtc_read(void)
{
	if (rtcst & 0x30)
		return (rtcr & 0x01) ? 1 : 0;
	return 0xFF;
}

static uint16_t rtcregread(uint8_t reg)
{
	uint8_t val, v;

	switch (reg) {
	case 0:
		val = (rtc_tm->tm_sec % 10) + ((rtc_tm->tm_sec / 10) << 4);
		break;
	case 1:
		val = (rtc_tm->tm_min % 10) + ((rtc_tm->tm_min / 10) << 4);
		break;
	case 2:
		v = rtc_tm->tm_hour;
		if (!rtc24) {
			v %= 12;
			v++;
		}
		val = (v % 10) + ((v / 10) << 4);
		if (!rtc24) {
			if (rtc_tm->tm_hour > 11)
				val |= 0x20;
			val |= 0x80;
		}
		break;
	case 3:
		val = (rtc_tm->tm_mday % 10) + ((rtc_tm->tm_mday / 10) << 4);
		break;
	case 4:
		val = ((rtc_tm->tm_mon + 1) % 10) + (((rtc_tm->tm_mon + 1) / 10) << 4);
		break;
	case 5:
		val = rtc_tm->tm_wday + 1;
		break;
	case 6:
		v = rtc_tm->tm_year % 100;
		val = (v % 10) + ((v / 10) << 4);
		break;
	case 7:
		val = rtcwp ? 0x80 : 0x00;
		break;
	case 8:
		val = 0;
		break;
	default:
		val = 0xFF;
		/* Check */
		break;
	}
	if (trace & TRACE_RTC)
		fprintf(stderr, "RTCreg %d = %02X\n", reg, val);
	return val;
}

static void rtcop(void)
{
	if (trace & TRACE_RTC)
		fprintf(stderr, "rtcbyte %02X\n", rtcw);
	/* The emulated task asked us to write a byte, and has now provided
	   the data byte to go with it */
	if (rtcstate == 2) {
		if (!rtcwp) {
			if (trace & TRACE_RTC)
				fprintf(stderr, "RTC write %d as %d\n", rtcreg, rtcw);
			/* We did a real write! */
			/* Not yet tackled burst mode */
			if (rtcreg != 0x3F && (rtcreg & 0x20))	/* NVRAM */
				rtcram[rtcreg & 0x1F] = rtcw;
			else if (rtcreg == 2)
				rtc24 = rtcw & 0x80;
			else if (rtcreg == 7)
				rtcwp = rtcw & 0x80;
		}
		/* For now don't emulate writes to the time */
		rtcstate = 0;
	}
	/* Check for broken requests */
	if (!(rtcw & 0x80)) {
		if (trace & TRACE_RTC)
			fprintf(stderr, "rtcw makes no sense %d\n", rtcw);
		rtcstate = 0;
		rtcr = 0x1FF;
		return;
	}
	/* Clock burst ? : for now we only emulate time burst */
	if (rtcw == 0xBF) {
		rtcstate = 3;
		rtcbp = 0;
		rtcbc = 0;
		rtcr = rtcregread(rtcbp++) << 1;
		if (trace & TRACE_RTC)
			fprintf(stderr, "rtc command BF: burst clock read.\n");
		return;
	}
	/* A write request */
	if (!(rtcw & 0x01)) {
		if (trace & TRACE_RTC)
			fprintf(stderr, "rtc write request, waiting byte 2.\n");
		rtcstate = 2;
		rtcreg = (rtcw >> 1) & 0x3F;
		rtcr = 0x1FF;
		return;
	}
	/* A read request */
	rtcstate = 1;
	if (rtcw & 0x40) {
		/* RAM */
		if (rtcw != 0xFE)
			rtcr = rtcram[(rtcw >> 1) & 0x1F] << 1;
		if (trace & TRACE_RTC)
			fprintf(stderr, "RTC RAM read %d, ready to clock out %d.\n", (rtcw >> 1) & 0xFF, rtcr);
		return;
	}
	/* Register read */
	rtcr = rtcregread((rtcw >> 1) & 0x1F) << 1;
	if (trace & TRACE_RTC)
		fprintf(stderr, "RTC read of time register %d is %d\n", (rtcw >> 1) & 0x1F, rtcr);
}

static void rtc_write(uint8_t val)
{
	uint8_t changed = val ^ rtcst;
	uint8_t is_read;
	/* Direction */
	if ((trace & TRACE_RTC) && (changed & 0x20))
		fprintf(stderr, "RTC direction now %s.\n", (val & 0x20) ? "read" : "write");
	is_read = val & 0x20;
	/* Clock */
	if (changed & 0x40) {
		/* The rising edge samples, the falling edge clocks receive */
		if (trace & TRACE_RTC)
			fprintf(stderr, "RTC clock low.\n");
		if (!(val & 0x40)) {
			rtcr >>= 1;
			/* Burst read of time */
			rtcbc++;
			if (rtcbc == 8 && rtcbp) {
				rtcr = rtcregread(rtcbp++) << 1;
				rtcbc = 0;
			}
			if (trace & TRACE_RTC)
				fprintf(stderr, "rtcr now %02X\n", rtcr);
		} else {
			if (trace & TRACE_RTC)
				fprintf(stderr, "RTC clock high.\n");
			rtcw >>= 1;
			if ((val & 0x30) == 0x10)
				rtcw |= val & 0x80;
			else
				rtcw |= 0xFF;
			rtccnt++;
			if (trace & TRACE_RTC)
				fprintf(stderr, "rtcw now %02x (%d)\n", rtcw, rtccnt);
			if (rtccnt == 8 && !is_read)
				rtcop();
		}
	}
	/* CE */
	if (changed & 0x10) {
		if (rtcst & 0x10) {
			if (trace & TRACE_RTC)
				fprintf(stderr, "RTC CE dropped.\n");
			rtccnt = 0;
			rtcr = 0;
			rtcw = 0;
			rtcstate = 0;
		} else {
			/* Latch imaginary registers on rising edge */
			time_t t = time(NULL);
			rtc_tm = localtime(&t);
			if (trace & TRACE_RTC)
				fprintf(stderr, "RTC CE raised and latched time.\n");
		}
	}
	rtcst = val;
}

/*
 *	Minimal beginnings of 6522 VIA emulation
 */

struct via6522 {
	uint8_t irq;
	uint8_t acr;
	uint8_t ifr;
	uint8_t ier;
	uint8_t pcr;
	uint8_t sr;
	uint8_t ora;
	uint8_t orb;
	uint8_t ira;
	uint8_t irb;
	uint8_t ddra;
	uint8_t ddrb;
	uint16_t t1;
	uint16_t t1l;
	uint16_t t2;
	uint8_t t2l;
	/* Pin states rather than registers */
	uint8_t ca;
	uint8_t cb;
};

struct via6522 via;

static void via_recalc_irq(void)
{
	int irq = (via.ier & via.ifr) & 0x7F;
	if (irq)
		via.ifr |= 0x80;
	else
		via.ifr &= 0x7F;
	/* We interrupt if ier and ifr are set */
	/* Note: the pin is inverted but we model irq state not the pin! */
	if ((trace & TRACE_VIA) && irq != via.irq)
		fprintf(stderr, "[VIA IRQ now %02X.]\n", irq);
	via.irq = irq;

	if (via.irq)
		CPU_addIRQ(IRQ_VIA);
	else
		CPU_clearIRQ(IRQ_VIA);
}

static void via_handshake_a(void)
{
}

static void via_handshake_b(void)
{
}

static void via_recalc_outputs(void)
{
}

static void via_recalc_inputs(void)
{
}

static void via_recalc_all(void)
{
	via_recalc_outputs();
	via_recalc_inputs();
	via_recalc_irq();
}

/* Perform time related processing for the VIA */
void via_tick(int clocks)
{
	/* This isn't quite right but it's near enough for the moment */
	if (via.t1) {
		if (clocks >= via.t1) {
			if (trace & TRACE_VIA)
				fprintf(stderr,"[VIA T1 expire.].\n");
			via.ifr |= 0x40;
			via_recalc_irq();
			/* +1 or + 2 ?? */
			if (via.acr & 0x40)
				via.t1 = via.t1l + 1;
			else
				via.t1 = 0;
		}
		else
			via.t1 -= clocks;
	}

	if (via.t2 && !(via.acr & 0x20)) {
		if (clocks >= via.t2) {
			via.ifr |= 0x20;
			via_recalc_irq();
			via.t2 = 0;
			if (trace & TRACE_VIA)
				fprintf(stderr,"[VIA T2 expire.].\n");
		}
		via.t2 -= clocks;
	}
}

uint8_t via_read(uint8_t addr)
{
	uint8_t r;
	if (trace & TRACE_VIA)
		fprintf(stderr, "[VIA read %d: ", addr);
	switch(addr) {
		case 0:
			r = via.irb & ~via.ddrb;
			r |= via.orb & via.ddrb;
			via_handshake_b();
			break;
		case 1:
			r = via.ira & ~via.ddra;
			r |= via.ora & via.ddra;
			via_handshake_a();
			break;
		case 2:
			r = via.ddrb;
			break;
		case 3:
			r = via.ddra;
			break;
		case 4:
			via.ifr &= ~0x40;	/* T1 timeout */
			via_recalc_irq();
			r = via.t1;
			break;
		case 5:
			r = via.t1 >> 8;
			break;
		case 6:
			r = via.t1l;
			break;
		case 7:
			r = via.t1l >> 8;
			break;
		case 8:
			via.ifr &= ~0x20;	/* T2 timeout */
			via_recalc_irq();
			r = via.t2;
			break;
		case 9:
			r = via.t2 >> 8;
			break;
		case 10:
			r = via.sr;
			break;
		case 11:
			r = via.acr;
			break;
		case 12:
			r = via.pcr;
			break;
		case 13:
			r = via.ifr;
			break;
		case 14:
			r = via.ier;
			break;
		default:
		case 15:
			r =  via.ira;
			break;
	}
	if (trace & TRACE_VIA)
		fprintf(stderr, "%02X.]\n", r);
	return r;
}

void via_write(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_VIA)
		fprintf(stderr, "[VIA write %d: %02X.]\n ", addr, val);
	switch(addr) {
		case 0:
			via.orb = val;
			via_recalc_outputs();
			via_handshake_b();
			break;
		case 1:
			via.ora = val;
			via_recalc_outputs();
			break;
		case 2:
			via.ddrb = val;
			via_recalc_all();
			break;
		case 3:
			via.ddra = val;
			via_recalc_all();
			break;
		case 4:
		case 6:
			via.t1l &= 0xFF00;
			via.t1l |= val;
			break;
		case 5:
			via.t1l &= 0xFF;
			via.t1l |= val << 8;
			via.t1 = via.t1l;
			via.ifr &= ~0x40;	/* T1 timeout */
			via_recalc_irq();
			if (trace & TRACE_VIA)
				fprintf(stderr, "[VIA T1 begin %04X.]\n", via.t1);
			break;
		case 7:
			via.t1l &= 0xFF;
			via.t1l |= val << 8;
			break;
		case 8:
			via.t2l = val;
			break;
		case 9:
			via.t2 = val << 8;
			via.t2 |= via.t2l;
			via.ifr &= ~0x20;	/* T2 timeout */
			via_recalc_irq();
			break;
		case 10:
			via.sr = val;
			break;
		case 11:
			via.acr = val;
			break;
		case 12:
			via.pcr = val;
			break;
		case 13:
			via.ifr &= ~val;
			if (via.ifr & 0x7F)
				via.ifr |= 0x80;
			via_recalc_irq();
			break;
		case 14:
			if (val & 0x80)
				via.ier |= val;
			else
				via.ier &= ~val;
			via.ier &= 0x7F;
			via_recalc_irq();
			break;
		case 15:
			via.ora = val;
			break;
	}
}



uint8_t mmio_read_65c816(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		return acia_read(addr & 1);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		return acia_read(addr & 1);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr >= 0x60 && addr <= 0x6F)
		return via_read(addr & 0x0F);
	if (addr == 0x0C && rtc)
		return rtc_read();
	if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		return uart_read(&uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void mmio_write_65c816(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		acia_write(addr & 1, val);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(addr & 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr >= 0x60 && addr <= 0x6F)
		via_write(addr & 0x0F, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (addr >= 0x78 && addr <= 0x7B) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(stderr, "Bank %d set to %d\n", addr & 3, val);
	} else if (addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_512)
			fprintf(stderr, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0x0C && rtc)
		rtc_write(val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		uart_write(&uart, addr & 0x0F, val);
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

/* FIXME: emulate paging off correctly, also be nice to emulate with less
   memory fitted */
uint8_t do_65c816_read(uint16_t addr)
{
	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(stderr, "R %04X[%02X] = %02X\n", addr, (unsigned int) bankreg[bank], (unsigned int) ramrom[(bankreg[bank] << 14) + (addr & 0x3FFF)]);
		addr &= 0x3FFF;
		return ramrom[(bankreg[bank] << 14) + addr];
	}
	/* When banking is off the entire 64K is occupied by repeats of ROM 0 */
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr & 0x3FFF];
}

uint8_t read65c816(uint32_t addr, uint8_t debug)
{
	uint8_t r;

	addr &= 0xFFFF;
	
	if (addr >> 8 == iopage) {
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

	if (addr >> 8 == iopage) {
		mmio_write_65c816(addr, val);
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
		acia_timer();
	if (uart_16550a)
		uart_event(&uart);
	via_tick(100);
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
	fprintf(stderr, "rc2014: [-1] [-A] [-a] [-c] [-f] [-R] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int opt;
	int fd;
	char *rompath = "rc2014-65c816.rom";
	char *idepath;

	while ((opt = getopt(argc, argv, "1Aad:fi:r:Rw")) != -1) {
		switch (opt) {
		case '1':
			uart_16550a = 1;
			acia = 0;
			break;
		case 'a':
			acia = 1;
			acia_input = 1;
			acia_narrow = 0;
			uart_16550a = 0;
			break;
		case 'A':
			acia = 1;
			acia_narrow = 1;
			acia_input = 1;
			uart_16550a = 0;
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

	if (acia == 0 && uart_16550a == 0) {
		fprintf(stderr, "rc2014: no UART selected, defaulting to 16550A\n");
		uart_16550a = 1;
	}

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ramrom, 524288) != 524288) {
		fprintf(stderr, "rc2014: banked rom image should be 512K.\n");
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

	if (uart_16550a)
		uart_init(&uart);

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
