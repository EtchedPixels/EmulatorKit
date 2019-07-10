/*
 *	Platform features
 *
 *	Z80 at 7.372MHz
 *	Zilog SIO/2 at 0x80-0x83
 *	Motorola 6850 repeats all over 0x40-0x7F (not recommended)
 *	IDE at 0x10-0x17 no high or control access
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at 0xC0
 *	16550A at 0xC8
 *
 *	Known bugs
 *	ROMWBW crashes in ACIA mode
 *	Not convinced we have all the INT clear cases right for SIO error
 *
 *	Add support for using real CF card
 *
 *	For the Easy-Z80 still need to update the CTC model
 *	TRG0/1 CTC0 and CTC1 are fed from UART_CLK (1.8432MHz)
 *	TRG2 is fed from CTC_CLK
 *	TO2 feeds TRG3
 *
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
#include "libz80/z80.h"
#include "ide.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t switchrom = 1;
static uint8_t cpuboard = 0;
static uint8_t have_ctc = 0;
static uint8_t port30 = 0;
static uint8_t port38 = 0;
static uint8_t rtc = 0;
static uint8_t fast = 0;
static uint8_t int_recalc = 0;
static uint8_t wiznet = 0;
static uint8_t cpld_serial = 0;
static uint8_t has_im2;
static uint8_t has_16x50;

static uint16_t tstate_steps = 369;	/* RC2014 speed */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

#define IRQ_SIOA	1
#define IRQ_SIOB	2
#define IRQ_CTC		3	/* 3 4 5 6 */

static Z80Context cpu_z80;

static nic_w5100_t *wiz;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_512	32
#define TRACE_RTC	64
#define TRACE_ACIA	128
#define TRACE_CTC	256
#define TRACE_CPLD	512
#define TRACE_IRQ	1024
#define TRACE_UART	2048

static int trace = 0;

static void reti_event(void);


/* FIXME: emulate paging off correctly, also be nice to emulate with less
   memory fitted */
static uint8_t mem_read0(uint16_t addr)
{
	if (bankenable) {
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

static void mem_write0(uint16_t addr, uint8_t val)
{
	if (bankenable) {
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

static uint8_t mem_read108(uint16_t addr)
{
	uint32_t aphys;
	if (addr < 0x8000 && !(port38 & 0x01))
		aphys = addr;
	else if (port38 & 0x80)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[aphys]);
	return ramrom[aphys];
}

static void mem_write108(uint16_t addr, uint8_t val)
{
	uint32_t aphys;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %04X = %02X\n", addr, val);
	if (addr < 0x8000 && !(port38 & 0x01)) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
		return;
	} else if (port38 & 0x80)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	ramrom[aphys] = val;
}

static uint8_t mem_read114(uint16_t addr)
{
	uint32_t aphys;
	if (addr < 0x8000 && !(port38 & 0x01))
		aphys = addr;
	else if (port30 & 0x01)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[aphys]);
	return ramrom[aphys];
}

static void mem_write114(uint16_t addr, uint8_t val)
{
	uint32_t aphys;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %04X = %02X\n", addr, val);
	if (addr < 0x8000 && !(port38 & 0x01)) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
		return;
	} else if (port30 & 0x01)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	ramrom[aphys] = val;
}

/* I think this right
   0: sets the lower bank to the lowest 32K of the 128K
   1: sets the lower bank to the 32K-64K range
   2: sets the lower bank to the 64K-96K range
   3: sets the lower bank to the 96K-128K range
   where the upper memory is always bank 1.
   
   Power on is 3, which is why the bootstrap lives in 3. */

static uint8_t mem_read64(uint16_t addr)
{
	uint8_t r;
	if (addr >= 0x8000)
		r = ramrom[addr];
	else
		r = ramrom[bankreg[0] * 0x8000 + addr];
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04x = %02X\n", addr, r);
	return r;
}

static void mem_write64(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04x = %02X\n", addr, val);
	if (addr >= 0x8000)
		ramrom[addr] = val;
	else
		ramrom[bankreg[0] * 0x8000 + addr] = val;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r;

	switch (cpuboard) {
	case 0:
		r = mem_read0(addr);
		break;
	case 1:
		r = mem_read108(addr);
		break;
	case 2:
		r = mem_read114(addr);
		break;
	case 3:
		r = mem_read64(addr);
		break;
	case 4:
		r = mem_read0(addr);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
	if (cpu_z80.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r == 0xCB) {
			rstate = 2;
			return r;
		}
		/* Look for ED with M1, followed directly by 4D and if so trigger
		   the interrupt chain */
		if (r == 0xED && rstate == 0) {
			rstate = 1;
			return r;
		}
	}
	if (r == 0x4D && rstate == 1)
		reti_event();
	rstate = 0;
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case 0:
		mem_write0(addr, val);
		break;
	case 1:
		mem_write108(addr, val);
		break;
	case 2:
		mem_write114(addr, val);
		break;
	case 3:
		mem_write64(addr, val);
		break;
	case 4:
		mem_write0(addr, val);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
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

static void recalc_interrupts(void)
{
	int_recalc = 1;
}

static uint8_t acia_status = 2;
static uint8_t acia_config;
static uint8_t acia_char;
static uint8_t acia;
static uint8_t acia_input;
static uint8_t acia_inint = 0;
static uint8_t acia_inreset;
static uint8_t acia_narrow;

static void acia_check_irq(void)
{
	if (acia_inint)
		Z80INT(&cpu_z80, 0xFF);	/* FIXME probably last data or bus noise */
}

static void acia_irq_compute(void)
{
	/* Recalculate the interrupt bit */
	acia_status &= 0x7F;
	if ((acia_status & 0x01) && (acia_config & 0x80))
		acia_status |= 0x80;
	if ((acia_status & 0x02) && (acia_config & 0x60) == 0x20)
		acia_status |= 0x80;
	/* Now see what should happen */
	if (!(acia_config & 0x80) || !(acia_status & 0x80)) {
		if (acia_inint && (trace & TRACE_ACIA))
			fprintf(stderr, "ACIA interrupt end.\n");
		acia_inint = 0;
		acia_status &= 0x7F;
		return;
	}
	if (acia_inint == 0 && (trace & TRACE_ACIA))
		fprintf(stderr, "ACIA interrupt.\n");
	acia_inint = 1;
	recalc_interrupts();
}

static void acia_receive(void)
{
	if (acia_inreset)
		return;
	/* Already a character waiting so set OVRN */
	if (acia_status & 1)
		acia_status |= 0x20;
	acia_char = next_char();
	if (trace & TRACE_ACIA)
		fprintf(stderr, "ACIA rx.\n");
	acia_status |= 0x01;	/* IRQ, and rx data full */
}

static void acia_transmit(void)
{
	if (!(acia_status & 2)) {
		if (trace & TRACE_ACIA)
			fprintf(stderr, "ACIA tx is clear.\n");
		acia_status |= 0x02;	/* IRQ, and tx data empty */
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
		if (acia_inreset)
			return 0;
		/* Reading the ACIA status has no effect on the bits */
		if (trace & TRACE_ACIA)
			fprintf(stderr, "acia_status %d\n", acia_status);
		return acia_status;
	case 1:
		/* Reading the ACIA character clears the receive ready
		   and also updates the error bits to match the new byte */
		/* Clear receive ready and rx overrun */
		acia_status &= ~0x21;
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
			acia_inreset = 1;
		else if (acia_inreset) {
			acia_inreset = 0;
			acia_status = 2;
		}
		acia_irq_compute();
		return;
	case 1:
		write(1, &val, 1);
		/* Clear TDRE - we now have a byte */
		acia_status &= ~0x02;
		acia_irq_compute();
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

static struct uart16x50 uart[1];

static void uart_init(struct uart16x50 *uptr)
{
    uptr->dlab = 0;
}

static void uart_check_irq(struct uart16x50 *uptr)
{
    if (uptr->irqline)
	    Z80INT(&cpu_z80, 0xFF);	/* actually undefined */
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
#if 0
    if (r & 1)
        uptr->lsr |= 0x01;	/* RX not empty */
#endif
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
            if (check_chario() & 1)
                return next_char();
            return 0x00;
        } else
            return uptr->ls;
        break;
    case 1:
        /* IER */
        if (uptr->dlab == 0)
            return uptr->ier;
        return uptr->ms;
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
        uptr->lsr &=0x90;
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
        uptr->msr &= 0x7F;
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


struct z80_sio_chan {
	uint8_t wr[8];
	uint8_t rr[3];
	uint8_t data[3];
	uint8_t dptr;
	uint8_t irq;
	uint8_t rxint;
	uint8_t txint;
	uint8_t intbits;
#define INT_TX	1
#define INT_RX	2
#define INT_ERR	4
	uint8_t pending;	/* Interrupt bits pending as an IRQ cause */
	uint8_t vector;		/* Vector pending to deliver */
};

static int sio2;
static int sio2_input;
static struct z80_sio_chan sio[2];

/*
 *	Interrupts. We don't handle IM2 yet.
 */

static void sio2_clear_int(struct z80_sio_chan *chan, uint8_t m)
{
	if (trace & TRACE_IRQ) {
		fprintf(stderr, "Clear intbits %d %x\n",
			(int)(chan - sio), m);
	}
	chan->intbits &= ~m;
	chan->pending &= ~m;
	/* Check me - does it auto clear down or do you have to reti it ? */
	if (!(sio->intbits | sio[1].intbits)) {
		sio->rr[1] &= ~0x02;
		chan->irq = 0;
	}
	recalc_interrupts();
}

static void sio2_raise_int(struct z80_sio_chan *chan, uint8_t m)
{
	uint8_t new = (chan->intbits ^ m) & m;
	chan->intbits |= m;
	if ((trace & TRACE_SIO) && new)
		fprintf(stderr, "SIO raise int %x new = %x\n", m, new);
	if (new) {
		if (!sio->irq) {
			chan->irq = 1;
			sio->rr[1] |= 0x02;
			recalc_interrupts();
		}
	}
}

static void sio2_reti(struct z80_sio_chan *chan)
{
	/* Recalculate the pending state and vectors */
	/* FIXME: what really goes here */
	sio->irq = 0;
	recalc_interrupts();
}

static int sio2_check_im2(struct z80_sio_chan *chan)
{
	uint8_t vector = sio[1].wr[2];
	/* See if we have an IRQ pending and if so deliver it and return 1 */
	if (chan->irq) {
		/* Do the vector calculation in the right place */
		/* FIXME: move this to other platforms */
		if (sio[1].wr[1] & 0x04) {
			/* This is a subset of the real options. FIXME: add
			   external status change */
			if (sio[1].wr[1] & 0x04) {
				vector &= 0xF1;
				if (chan == sio)
					vector |= 1 << 3;
				if (chan->intbits & INT_RX)
					vector |= 4;
				else if (chan->intbits & INT_ERR)
					vector |= 2;
			}
			if (trace & TRACE_SIO)
				fprintf(stderr, "SIO2 interrupt %02X\n", vector);
			chan->vector = vector;
		} else {
			vector &= 0xF7;
			if (chan != sio)
				vector |= 1 << 3;
			chan->vector = vector;
		}
		if (trace & (TRACE_IRQ|TRACE_SIO))
			fprintf(stderr, "New live interrupt pending is SIO (%d:%02X).\n",
				(int)(chan - sio), chan->vector);
		if (chan == sio)
			live_irq = IRQ_SIOA;
		else
			live_irq = IRQ_SIOB;
		Z80INT(&cpu_z80, chan->vector);
		return 1;
	}
	return 0;
}

/*
 *	The SIO replaces the last character in the FIFO on an
 *	overrun.
 */
static void sio2_queue(struct z80_sio_chan *chan, uint8_t c)
{
	if (trace & TRACE_SIO)
		fprintf(stderr, "SIO %d queue %d: ", (int) (chan - sio), c);
	/* Receive disabled */
	if (!(chan->wr[3] & 1)) {
		fprintf(stderr, "RX disabled.\n");
		return;
	}
	/* Overrun */
	if (chan->dptr == 2) {
		if (trace & TRACE_SIO)
			fprintf(stderr, "Overrun.\n");
		chan->data[2] = c;
		chan->rr[1] |= 0x20;	/* Overrun flagged */
		/* What are the rules for overrun delivery FIXME */
		sio2_raise_int(chan, INT_ERR);
	} else {
		/* FIFO add */
		if (trace & TRACE_SIO)
			fprintf(stderr, "Queued %d (mode %d)\n", chan->dptr, chan->wr[1] & 0x18);
		chan->data[chan->dptr++] = c;
		chan->rr[0] |= 1;
		switch (chan->wr[1] & 0x18) {
		case 0x00:
			break;
		case 0x08:
			if (chan->dptr == 1)
				sio2_raise_int(chan, INT_RX);
			break;
		case 0x10:
		case 0x18:
			sio2_raise_int(chan, INT_RX);
			break;
		}
	}
	/* Need to deal with interrupt results */
}

static void sio2_channel_timer(struct z80_sio_chan *chan, uint8_t ab)
{
	if (ab == 0) {
		int c = check_chario();

		if (sio2_input) {
			if (c & 1)
				sio2_queue(chan, next_char());
		}
		if (c & 2) {
			if (!(chan->rr[0] & 0x04)) {
				chan->rr[0] |= 0x04;
				if (chan->wr[1] & 0x02)
					sio2_raise_int(chan, INT_TX);
			}
		}
	} else {
		if (!(chan->rr[1] & 0x04)) {
			chan->rr[1] |= 0x04;
			if (chan->wr[1] & 0x02)
				sio2_raise_int(chan, INT_TX);
		}
	}
}

static void sio2_timer(void)
{
	sio2_channel_timer(sio, 0);
	sio2_channel_timer(sio + 1, 1);
}

static void sio2_channel_reset(struct z80_sio_chan *chan)
{
	chan->rr[0] = 0x2C;
	chan->rr[1] = 0x01;
	chan->rr[2] = 0;
	sio2_clear_int(chan, INT_RX | INT_TX | INT_ERR);
}

static void sio_reset(void)
{
	sio2_channel_reset(sio);
	sio2_channel_reset(sio + 1);
}

static uint8_t sio2_read(uint16_t addr)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio + 1 : sio;
	if (!(addr & 1)) {
		/* Control */
		uint8_t r = chan->wr[0] & 007;
		chan->wr[0] &= ~007;

		chan->rr[0] &= ~2;
		if (chan == sio && (sio[0].intbits | sio[1].intbits))
			chan->rr[0] |= 2;
		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c read reg %d = ", (addr & 2) ? 'b' : 'a', r);
		switch (r) {
		case 0:
		case 1:
			if (trace & TRACE_SIO)
				fprintf(stderr, "%02X\n", chan->rr[r]);
			return chan->rr[r];
		case 2:
			if (chan != sio) {
				if (trace & TRACE_SIO)
					fprintf(stderr, "%02X\n", chan->rr[2]);
				return chan->rr[2];
			}
		case 3:
			/* What does the hw report ?? */
			fprintf(stderr, "INVALID(0xFF)\n");
			return 0xFF;
		}
	} else {
		/* FIXME: irq handling */
		uint8_t c = chan->data[0];
		chan->data[0] = chan->data[1];
		chan->data[1] = chan->data[2];
		if (chan->dptr)
			chan->dptr--;
		if (chan->dptr == 0)
			chan->rr[0] &= 0xFE;	/* Clear RX pending */
		sio2_clear_int(chan, INT_RX);
		chan->rr[0] &= 0x3F;
		chan->rr[1] &= 0x3F;
		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c read data %d\n", (addr & 2) ? 'b' : 'a', c);
		if (chan->dptr && (chan->wr[1] & 0x10))
			sio2_raise_int(chan, INT_RX);
		return c;
	}
	return 0xFF;
}

static void sio2_write(uint16_t addr, uint8_t val)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio + 1 : sio;
	if (!(addr & 1)) {
		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c write reg %d with %02X\n", (addr & 2) ? 'b' : 'a', chan->wr[0] & 7, val);
		switch (chan->wr[0] & 007) {
		case 0:
			chan->wr[0] = val;
			/* FIXME: CRC reset bits ? */
			switch (val & 070) {
			case 000:	/* NULL */
				break;
			case 010:	/* Send Abort SDLC */
				/* SDLC specific no-op for async */
				break;
			case 020:	/* Reset external/status interrupts */
				sio2_clear_int(chan, INT_ERR);
				chan->rr[1] &= 0xCF;	/* Clear status bits on rr0 */
				break;
			case 030:	/* Channel reset */
				if (trace & TRACE_SIO)
					fprintf(stderr, "[channel reset]\n");
				sio2_channel_reset(chan);
				break;
			case 040:	/* Enable interrupt on next rx */
				chan->rxint = 1;
				break;
			case 050:	/* Reset transmitter interrupt pending */
				chan->txint = 0;
				sio2_clear_int(chan, INT_TX);
				break;
			case 060:	/* Reset the error latches */
				chan->rr[1] &= 0x8F;
				break;
			case 070:	/* Return from interrupt (channel A) */
				if (chan == sio) {
					sio->irq = 0;
					sio->rr[1] &= ~0x02;
					sio2_clear_int(sio, INT_RX | INT_TX | INT_ERR);
					sio2_clear_int(sio + 1, INT_RX | INT_TX | INT_ERR);
				}
				break;
			}
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			if (trace & TRACE_SIO)
				fprintf(stderr, "sio%c: wrote r%d to %02X\n",
					(addr & 2) ? 'b' : 'a', chan->wr[0] & 7, val);
			chan->wr[chan->wr[0] & 7] = val;
			chan->wr[0] &= ~007;
			break;
		}
		/* Control */
	} else {
		/* Strictly we should emulate this as two bytes, one going out and
		   the visible queue - FIXME */
		/* FIXME: irq handling */
		chan->rr[0] &= ~(1 << 2);	/* Transmit buffer no longer empty */
		chan->txint = 1;
		/* Should check chan->wr[5] & 8 */
		sio2_clear_int(chan, INT_TX);
		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c write data %d\n", (addr & 2) ? 'b' : 'a', val);
		write(1, &val, 1);
	}
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
 *	Z80 CTC
 */

struct z80_ctc {
	uint16_t count;
	uint16_t reload;
	uint8_t vector;
	uint8_t ctrl;
#define CTC_IRQ		0x80
#define CTC_COUNTER	0x40
#define CTC_PRESCALER	0x20
#define CTC_RISING	0x10
#define CTC_PULSE	0x08
#define CTC_TCONST	0x04
#define CTC_RESET	0x02
#define CTC_CONTROL	0x01
	uint8_t irq;		/* Only valid for channel 0, so we know
				   if we must wait for a RETI before doing
				   a further interrupt */
};

#define CTC_STOPPED(c)	(((c)->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET))

struct z80_ctc ctc[4];
uint8_t ctc_irqmask;

static void ctc_reset(struct z80_ctc *c)
{
	c->vector = 0;
	c->ctrl = CTC_RESET;
}

static void ctc_init(void)
{
	ctc_reset(ctc);
	ctc_reset(ctc + 1);
	ctc_reset(ctc + 2);
	ctc_reset(ctc + 3);
}

static void ctc_interrupt(struct z80_ctc *c)
{
	int i = c - ctc;
	if (c->ctrl & CTC_IRQ) {
		if (!(ctc_irqmask & (1 << i))) {
			ctc_irqmask |= 1 << i;
			recalc_interrupts();
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d wants to interrupt.\n", i);
		}
	}
}

static void ctc_reti(int ctcnum)
{
	if (ctc_irqmask & (1 << ctcnum)) {
		ctc_irqmask &= ~(1 << ctcnum);
		if (trace & TRACE_IRQ)
			fprintf(stderr, "Acked interrupt from CTC %d.\n", ctcnum);
	}
}

/* After a RETI or when idle compute the status of the interrupt line and
   if we are head of the chain this time then raise our interrupt */

static int ctc_check_im2(void)
{
	if (ctc_irqmask) {
		int i;
		for (i = 0; i < 4; i++) {	/* FIXME: correct order ? */
			if (ctc_irqmask & (1 << i)) {
				uint8_t vector = ctc[0].vector & 0xF8;
				vector += 2 * i;
				if (trace & TRACE_IRQ)
					fprintf(stderr, "New live interrupt is from CTC %d vector %x.\n", i, vector);
				live_irq = IRQ_CTC + i;
				Z80INT(&cpu_z80, vector);
				return 1;
			}
		}
	}
	return 0;
}

/* Model the chains between the CTC devices */
static void ctc_receive_pulse(int i);

static void ctc_pulse(int i)
{
	/* Model CTC 2 chained into CTC 3 */
	if (i == 2)
		ctc_receive_pulse(3);
}

/* We don't worry about edge directions just a logical pulse model */
static void ctc_receive_pulse(int i)
{
	struct z80_ctc *c = ctc + i;
	if (c->ctrl & CTC_COUNTER) {
		if (CTC_STOPPED(c))
			return;
		if (c->count >= 0x0100)
			c->count -= 0x100;	/* No scaling on pulses */
		if ((c->count & 0xFF00) == 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			c->count = c->reload << 8;
		}
	} else {
		if (c->ctrl & CTC_PULSE)
			c->ctrl &= ~CTC_PULSE;
	}
}

/* Model counters */
static void ctc_tick(unsigned int clocks)
{
	struct z80_ctc *c = ctc;
	int i;
	int n;
	int decby;

	for (i = 0; i < 4; i++, c++) {
		/* Waiting a value */
		if (CTC_STOPPED(c))
			continue;
		/* Pulse trigger mode */
		if (c->ctrl & CTC_COUNTER)
			continue;
		/* 256x downscaled */
		decby = clocks;
		/* 16x not 256x downscale - so increase by 16x */
		if (!(c->ctrl & CTC_PRESCALER))
			decby <<= 4;
		/* Now iterate over the events. We need to deal with wraps
		   because we might have something counters chained */
		n = c->count - decby;
		while (n < 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			if (c->reload == 0)
				n += 256 << 8;
			else
				n += c->reload << 8;
		}
		c->count = n;
	}
}

static void ctc_write(uint8_t channel, uint8_t val)
{
	struct z80_ctc *c = ctc + channel;
	if (c->ctrl & CTC_TCONST) {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d constant loaded with %02X\n", channel, val);
		c->reload = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET)) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		c->ctrl &= ~CTC_TCONST|CTC_RESET;
	} else if (val & CTC_CONTROL) {
		/* We don't yet model the weirdness around edge wanted
		   toggling and clock starts */
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d control loaded with %02X\n", channel, val);
		c->ctrl = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == CTC_RESET) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		/* Undocumented */
		if (!(c->ctrl & CTC_IRQ) && (ctc_irqmask & (1 << channel))) {
			ctc_irqmask &= ~(1 << channel);
			if (ctc_irqmask == 0) {
				if (trace & TRACE_IRQ)
					fprintf(stderr, "CTC %d irq reset.\n", channel);
				/* Is this all that is needed ?? */
			}
		}
	} else {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d vector loaded with %02X\n", channel, val);
		c->vector = val;
	}
}

static uint8_t ctc_read(uint8_t channel)
{
	uint8_t val = ctc[channel].count >> 8;
	if (trace & TRACE_CTC)
		fprintf(stderr, "CTC %d reads %02x\n", channel, val);
	return val;
}


/*
 *	Emulate the switchable ROM card. We switch between the ROM and
 *	two banks of RAM (any two will do providing it's not the ones we
 *	pretended the bank mapping used for the top 32K). You can't mix the
 *	512K ROM/RAM with this card anyway.
 */
static void toggle_rom(void)
{
	if (bankreg[0] == 0) {
		if (trace & TRACE_ROM)
			fprintf(stderr, "[ROM out]\n");
		bankreg[0] = 34;
		bankreg[1] = 35;
	} else {
		if (trace & TRACE_ROM)
			fprintf(stderr, "[ROM in]\n");
		bankreg[0] = 0;
		bankreg[1] = 1;
	}
}

/*
 *	Emulate the Z80SBC64 CPLD
 */
 
static uint8_t sbc64_cpld_status;
static uint8_t sbc64_cpld_char;

static void sbc64_cpld_timer(void)
{
	/* Don't allow overruns - hack for convenience when pasting hex files */
	if (!(sbc64_cpld_status & 1)) {
		if (check_chario() & 1) {
			sbc64_cpld_status |= 1;
			sbc64_cpld_char = next_char();
		}
	}
}

static uint8_t sbc64_cpld_uart_rx(void)
{
	sbc64_cpld_status &= ~1;
	if (trace & TRACE_CPLD)
		fprintf(stderr, "CPLD rx %02X.\n", sbc64_cpld_char);
	return sbc64_cpld_char;
}

static uint8_t sbc64_cpld_uart_status(void)
{
//	if (trace & TRACE_CPLD)
//		fprintf(stderr, "CPLD status %02X.\n", sbc64_cpld_status);
	return sbc64_cpld_status;
}

static void sbc64_cpld_uart_ctrl(uint8_t val)
{
	if (trace & TRACE_CPLD)
		fprintf(stderr, "CPLD control %02X.\n", val);
}

static void sbc64_cpld_uart_tx(uint8_t val)
{
	static uint16_t bits;
	static uint8_t bitcount;
	/* This is umm... fun. We should do a clock based analysis and
	   bit recovery. For the moment cheat to get it tested */
	val &= 1;
	if (bitcount == 0) {
		if (val & 1)
			return;
		/* Look mummy a start a bit */
		bitcount = 1;
		bits = 0;
		if (trace & TRACE_CPLD)
			fprintf(stderr, "[start]");
		return;
	}
	/* This works because all the existing code does one write per bit */
	if (bitcount == 9) {
		if (val & 1) {
			if (trace & TRACE_CPLD)
				fprintf(stderr, "[stop]");
			putchar(bits);
			fflush(stdout);
		} else	/* Framing error should be a stop bit */
			putchar('?');
		bitcount = 0;
		bits = 0;
		return;
	}
	bits >>= 1;
	bits |= val ? 0x80: 0x00;
	if (trace & TRACE_CPLD)
		fprintf(stderr, "[%d]", val);
	bitcount++;
}

static void sbc64_cpld_bankreg(uint8_t val)
{
	val &= 3;
	if (bankreg[0] != val) {
		if (trace & TRACE_CPLD)
			fprintf(stderr, "Bank set to %02X\n", val);
		bankreg[0] = val;
	}
}

static uint8_t io_read_2014(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		return acia_read(addr & 1);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		return acia_read(addr & 1);
	if ((addr >= 0x80 && addr <= 0x83) && sio2)
		return sio2_read(addr & 3);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0xC0 && rtc)
		return rtc_read();
	/* Scott Baker is 0x90-93, suggested defaults for the
	   Stephen Cousins boards at 0x88-0x8B. No doubt we'll get
	   an official CTC board at another address  */
	if (addr >= 0x88 && addr <= 0x8B && have_ctc)
		return ctc_read(addr & 3);
	if (addr >= 0xC8 && addr <= 0xD0 && has_16x50)
		return uart_read(&uart[0], addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_2014(uint16_t addr, uint8_t val, uint8_t known)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		acia_write(addr & 1, val);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(addr & 1, val);
	else if ((addr >= 0x80 && addr <= 0x83) && sio2)
		sio2_write(addr & 3, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
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
	} else if (addr == 0xC0 && rtc)
		rtc_write(val);
	else if (addr >= 0x88 && addr <= 0x8B && have_ctc)
		ctc_write(addr & 3, val);
	else if (addr >= 0xC8 && addr <= 0xCF && has_16x50)
		uart_write(&uart[0], addr & 7, val);
	else if (switchrom && addr == 0x38)
		toggle_rom();
	else if (addr == 0xFD) {
		trace &= 0xFF00;
		trace |= val;
		printf("trace set to %04X\n", trace);
	} else if (addr == 0xFE) {
		trace &= 0xFF;
		trace |= val << 8;
		printf("trace set to %d\n", trace);
	} else if (!known && (trace & TRACE_UNK))
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

static uint8_t io_read_4(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x83)
		return sio2_read((addr & 3) ^ 1);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0xC0 && rtc)
		return rtc_read();
	if (addr >= 0x88 && addr <= 0x8B)
		return ctc_read(addr & 3);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_4(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x83)
		sio2_write((addr & 3) ^ 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
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
	} else if (addr == 0xC0 && rtc)
		rtc_write(val);
	else if (addr >= 0x88 && addr <= 0x8B)
		ctc_write(addr & 3, val);
	else if (addr == 0xFC) {
		putchar(val);
		fflush(stdout);
	} else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

static void io_write_1(uint16_t addr, uint8_t val)
{
	if ((addr & 0xFF) == 0x38) {
		val &= 0x81;
		if (val != port38 && (trace & TRACE_ROM))
			fprintf(stderr, "Bank set to %02X\n", val);
		port38 = val;
		return;
	}
	io_write_2014(addr, val, 0);
}

static void io_write_3(uint16_t addr, uint8_t val)
{
	switch(addr & 0xFF) {
	case 0xf9:
		sbc64_cpld_uart_tx(val);
		break;
	case 0xf8:
		sbc64_cpld_uart_ctrl(val);
		break;
	case 0x1f:
		sbc64_cpld_bankreg(val);
		break;
	default:
		io_write_2014(addr, val, 0);
		break;
	}
}

static uint8_t io_read_2(uint16_t addr)
{
	switch (addr & 0xFC) {
	case 0x28:
		return 0x80;
	default:
		return io_read_2014(addr);
	}
}

static uint8_t io_read_3(uint16_t addr)
{
	switch(addr & 0xFF) {
	case 0xf9:
		return sbc64_cpld_uart_rx();
	case 0xf8:
		return sbc64_cpld_uart_status();
	default:
		return io_read_2014(addr);
	}
}

static void io_write_2(uint16_t addr, uint8_t val)
{
	uint16_t r = addr & 0xFC;	/* bits 0/1 not decoded */
	uint8_t known = 0;

	switch (r) {
	case 0x08:
		if (val & 1)
			printf("[LED off]\n");
		else
			printf("[LED on]\n");
		return;
	case 0x20:
		if (val & 1)
			printf("[RTS high]\n");
		else
			printf("[RTS low]\n");
		known = 1;
		break;
	case 0x28:
		known = 1;
		break;
	case 0x30:
		if (trace & TRACE_ROM)
			fprintf(stderr, "RAM Bank set to %02X\n", val);
		port30 = val;
		return;
	case 0x38:
		if (trace & TRACE_ROM)
			fprintf(stderr, "ROM Bank set to %02X\n", val);
		port38 = val;
		return;
	}
	io_write_2014(addr, val, known);
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case 0:
		io_write_2014(addr, val, 0);
		break;
	case 1:
		io_write_1(addr, val);
		break;
	case 2:
		io_write_2(addr, val);
		break;
	case 3:
		io_write_3(addr, val);
		break;
	case 4:
		io_write_4(addr, val);
		break;
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

static uint8_t io_read(int unused, uint16_t addr)
{
	switch (cpuboard) {
	case 0:
	case 1:
		return io_read_2014(addr);
	case 2:
		return io_read_2(addr);
	case 3:
		return io_read_3(addr);
	case 4:
		return io_read_4(addr);
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

static void poll_irq_event(void)
{
	if (has_im2) {
		acia_check_irq();
		uart_check_irq(&uart[0]);
		if (!live_irq) {
			!sio2_check_im2(sio) && !sio2_check_im2(sio + 1) &&
			!ctc_check_im2();
		}
	} else {
		acia_check_irq();
		uart_check_irq(&uart[0]);
		!sio2_check_im2(sio) && !sio2_check_im2(sio + 1);
		ctc_check_im2();
	}
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
	if (has_im2) {
		switch(live_irq) {
		case IRQ_SIOA:
			sio2_reti(sio);
			break;
		case IRQ_SIOB:
			sio2_reti(sio + 1);
			break;
		case IRQ_CTC:
		case IRQ_CTC + 1:
		case IRQ_CTC + 2:
		case IRQ_CTC + 3:
			ctc_reti(live_irq - IRQ_CTC);
			break;
		}
	} else {
		/* If IM2 is not wired then all the things respond at the same
		   time. I think they can also fight over the vector but ignore
		   that */
		if (sio2) {
			sio2_reti(sio);
			sio2_reti(sio + 1);
		}
		if (have_ctc) {
			ctc_reti(0);
			ctc_reti(1);
			ctc_reti(2);
			ctc_reti(3);
		}
	}
	live_irq = 0;
	poll_irq_event();
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
	fprintf(stderr, "rc2014: [-a] [-A] [-b] [-c] [-f] [-R] [-m mainboard] [-r rompath] [-e rombank] [-s] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	int rombank = 0;
	char *rompath = "rc2014.rom";
	char *idepath;
	int save = 0;

	while ((opt = getopt(argc, argv, "Aabcd:e:fi:m:pr:sRuw")) != -1) {
		switch (opt) {
		case 'a':
			acia = 1;
			acia_input = 1;
			acia_narrow = 0;
			sio2 = 0;
			break;
		case 'A':
			acia = 1;
			acia_narrow = 1;
			acia_input = 1;
			sio2 = 0;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 's':
			sio2 = 1;
			sio2_input = 1;
			acia = 0;
			break;
		case 'e':
			rombank = atoi(optarg);
			break;
		case 'b':
			bank512 = 1;
			switchrom = 0;
			rom = 0;
			break;
		case 'p':
			bankenable = 1;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
			break;
		case 'c':
			have_ctc = 1;
			break;
		case 'u':
			has_16x50 = 1;
			break;
		case 'm':
			/* Default Z80 board */
			if (strcmp(optarg, "z80") == 0)
				cpuboard = 0;
			else if (strcmp(optarg, "sc108") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = 1;
			} else if (strcmp(optarg, "sc114") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = 2;
			} else if (strcmp(optarg, "z80sbc64") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = 3;
				bankreg[0] = 3;
			} else if (strcmp(optarg, "z80mb64") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = 3;
				bankreg[0] = 3;
				/* Triple RC2014 rate */
				tstate_steps = 123;
			} else if (strcmp(optarg, "easyz80") == 0) {
				bank512 = 1;
				cpuboard = 4;
				switchrom = 0;
				rom = 0;
				acia = 0;
				have_ctc = 1;
				sio2 = 1;
				sio2_input = 1;
				has_im2 = 1;
				tstate_steps = 500;
			} else {
				fputs("rc2014: supported cpu types z80, easyz80, sc108, sc114, z80sbc64, z80mb64.\n",
						stderr);
				exit(EXIT_FAILURE);
			}
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

	if (cpuboard == 3) {
		acia_input = 0;
		sio2_input = 0;
		cpld_serial = 1;
	} else if (acia == 0 && sio2 == 0) {
		if (cpuboard != 3) {
			fprintf(stderr, "rc2014: no UART selected, defaulting to 68B50\n");
			acia = 1;
			acia_input = 1;
		}
	}
	if (rom == 0 && bank512 == 0) {
		fprintf(stderr, "rc2014: no ROM\n");
		exit(EXIT_FAILURE);
	}

	if (rom && cpuboard != 3) {
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
		if (read(fd, ramrom, 65536) < 8192) {
			fprintf(stderr, "rc2014: short rom '%s'.\n", rompath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}

	/* SBC64 has battery backed RAM and what really happens is that you
	   use the CPLD to load a loader and then to load ZMON which in turn
	   hides in bank 3. This is .. tedious .. with an emulator so we allow
	   you to load bank 3 with the CPLD loader (so you can play with it
	   and loading Zmon, or with Zmon loaded, or indeed anything else in
	   the reserved space notably SCMonitor).
	   
	   Quitting saves the memory state. If you screw it all up then use
	   the loader bin file instead to get going again.
	   
	   Mark states read only with chmod and it won't save back */

	if (cpuboard == 3) {
		int len;
		save = 1;
		fd = open(rompath, O_RDWR);
		if (fd == -1) {
			save = 0;
			fd = open(rompath, O_RDONLY);
			if (fd == -1) {
				perror(rompath);
				exit(EXIT_FAILURE);
			}
		}
		/* Could be a short bank 3 save for bootstrapping or a full
		   save from the emulator exit */
		len = read(fd, ramrom, 4 * 0x8000);
		if (len < 4 * 0x8000) {
			if (len < 255) {
				fprintf(stderr, "rc2014:short ram '%s'.\n", rompath);
				exit(EXIT_FAILURE);
			}
			memmove(ramrom + 3 * 0x8000, ramrom, 32768);
			printf("[loaded bank 3 only]\n");
		}
	}

	if (bank512) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom, 524288) != 524288) {
			fprintf(stderr, "rc2014: banked rom image should be 512K.\n");
			exit(EXIT_FAILURE);
		}
		bankenable = 1;
		close(fd);
	}

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

	if (sio2)
		sio_reset();
	if (have_ctc)
		ctc_init();
	if (has_16x50)
		uart_init(&uart[0]);

	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
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

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 100 times then poll the
	   slow stuff and nap for 5ms. */
	while (!done) {
		int i;
		/* 36400 T states for base RC2014 - varies for others */
		for (i = 0; i < 100; i++) {
			Z80ExecuteTStates(&cpu_z80, tstate_steps);
			if (acia)
				acia_timer();
			if (sio2)
				sio2_timer();
			if (has_16x50)
				uart_event(&uart[0]);
			if (cpld_serial)
				sbc64_cpld_timer();
			if (have_ctc)
				ctc_tick(tstate_steps);
		}
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending Z80 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
			if (!live_irq || !has_im2)
				poll_irq_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z80.IFF1|cpu_z80.IFF2))
				int_recalc = 0;
		}
	}
	if (cpuboard == 3 && save) {
		lseek(fd, 0L, SEEK_SET);
		if (write(fd, ramrom, 0x8000 * 4) != 0x8000 * 4) {
			fprintf(stderr, "rc2014: state save failed.\n");
			exit(1);
		}
		close(fd);
	}
	exit(0);
}
