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
 *	Not convinced we have all the INT clear cases right for SIO error
 *
 *	Add support for using real CF card
 *
 *	For the Easy-Z80 still need to update the CTC model
 *	TRG0/1 CTC0 and CTC1 are fed from UART_CLK (1.8432MHz)
 *	TRG2 is fed from CTC_CLK
 *	TO2 feeds TRG3
 *
 *	The SC121 just an initial sketch for playing with IM2 and the
 *	relevant peripherals. I'll align it properly with the real thing as more
 *	info appears.
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
#include "system.h"
#include "libz80/z80.h"

#include "acia.h"
#include "ide.h"
#include "rtc_bitbang.h"
#include "w5100.h"
#include "z80dma.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t switchrom = 1;

#define CPUBOARD_Z80		0
#define CPUBOARD_SC108		1
#define CPUBOARD_SC114		2
#define CPUBOARD_Z80SBC64	3
#define CPUBOARD_EASYZ80	4
#define CPUBOARD_SC121		5
#define CPUBOARD_MICRO80	6

static uint8_t cpuboard = CPUBOARD_Z80;

static uint8_t have_ctc = 0;
static uint8_t port30 = 0;
static uint8_t port38 = 0;
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
#define TRACE_Z84C15	4096
#define TRACE_IDE	8192
#define TRACE_SPI	16384
#define TRACE_SD	32768

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

struct z84c15 {
	uint8_t scrp;
	uint8_t wcr;
	uint8_t mwbr;
	uint8_t csbr;
	uint8_t mcr;
	uint8_t intpr;
};

struct z84c15 z84c15;

static void z84c15_init(void)
{
	z84c15.scrp = 0;
	z84c15.wcr = 0;		/* Really it's 0xFF for 15 instructions then 0 */
	z84c15.mwbr = 0xF0;
	z84c15.csbr = 0x0F;
	z84c15.mcr = 0x01;
	z84c15.intpr = 0;
}

/*
 *	The Z84C15 CS lines as wired for the Micro80
 */

static uint8_t *mmu_micro80_z84c15(uint16_t addr, int write)
{
	uint8_t cs0 = 0, cs1 = 0;
	uint8_t page = addr >> 12;
	if (page <= (z84c15.csbr & 0x0F))
		cs0 = 1;
	else if (page <= (z84c15.csbr >> 4))
		cs1 = 1;
	if (!(z84c15.mcr & 0x01))
		cs0 = 0;
	if (!(z84c15.mcr & 0x02))
		cs1 = 0;
	/* Depending upon final flash wiring. PIO might control
	   this and it might be 32K */
	/* CS0 low selects ROM always */
	if (trace & TRACE_MEM) {
		if (cs0)
			fprintf(stderr, "R");
		if (cs1)
			fprintf(stderr, "L");
		else
			fprintf(stderr, "H");
	}
	if (cs0) {
		if (write)
			return NULL;
		else
			return &ramrom[(addr & 0x3FFF)];
	}
	/* CS1 low forces A16 low */
	if (cs1)
		return &ramrom[0x20000 + addr];
	return &ramrom[0x30000 + addr];
}

static uint8_t mem_read_micro80(uint16_t addr)
{
	uint8_t val = *mmu_micro80_z84c15(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04x = %02X\n", addr, val);
	return val;
}

static void mem_write_micro80(uint16_t addr, uint8_t val)
{
	uint8_t *p = mmu_micro80_z84c15(addr, 1);
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04x = %02X\n", addr, val);
	if (p == NULL)
		fprintf(stderr, "%04x: write to ROM of %02X attempted.\n", addr, val);
	else
		*p = val;
}

uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r;

	switch (cpuboard) {
	case CPUBOARD_Z80:
		r = mem_read0(addr);
		break;
	case CPUBOARD_SC108:
		r = mem_read108(addr);
		break;
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		r = mem_read114(addr);
		break;
	case CPUBOARD_Z80SBC64:
		r = mem_read64(addr);
		break;
	case CPUBOARD_EASYZ80:
		r = mem_read0(addr);
		break;
	case CPUBOARD_MICRO80:
		r = mem_read_micro80(addr);
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

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z80:
		mem_write0(addr, val);
		break;
	case CPUBOARD_SC108:
		mem_write108(addr, val);
		break;
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		mem_write114(addr, val);
		break;
	case CPUBOARD_Z80SBC64:
		mem_write64(addr, val);
		break;
	case CPUBOARD_EASYZ80:
		mem_write0(addr, val);
		break;
	case CPUBOARD_MICRO80:
		mem_write_micro80(addr, val);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
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
	int_recalc = 1;
}

struct acia *acia;
static uint8_t acia_narrow;


static void acia_check_irq(struct acia *acia)
{
	if (acia_irq_pending(acia))
		Z80INT(&cpu_z80, 0xFF);	/* FIXME probably last data or bus noise */
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
		if (!(chan->rr[0] & 0x04)) {
			chan->rr[0] |= 0x04;
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
	uint8_t r;
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
			r = chan->wr[0] & 7;
			if (trace & TRACE_SIO)
				fprintf(stderr, "sio%c: wrote r%d to %02X\n",
					(addr & 2) ? 'b' : 'a', r, val);
			chan->wr[r] = val;
			if (chan != sio && r == 2)
				chan->rr[2] = val;
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
		if (chan == sio)
			write(1, &val, 1);
		else {
//			write(1, "\033[1m;", 5);
			write(1, &val,1);
//			write(1, "\033[0m;", 5);
		}
	}
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	uint8_t r =  ide_read8(ide0, addr);
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide read %d = %02X\n", addr, r);
	return r;
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide write %d = %02X\n", addr, val);
	ide_write8(ide0, addr, val);
}

struct rtc *rtc;

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
	if (cpuboard != CPUBOARD_SC121) {
		/* Model CTC 2 chained into CTC 3 */
		if (i == 2)
			ctc_receive_pulse(3);
	}
	/* The SC121 has 0-2 for SIO baud and only 3 for a timer */
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
				if (live_irq == IRQ_CTC + channel)
					live_irq = 0;
			}
		}
	} else {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d vector loaded with %02X\n", channel, val);
		/* Only works on channel 0 */
		if (channel == 0)
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

static int sd_mode = 0;
static int sd_cmdp = 0;
static int sd_ext = 0;
static uint8_t sd_cmd[6];
static uint8_t sd_in[520];
static int sd_inlen, sd_inp;
static uint8_t sd_out[520];
static int sd_outlen, sd_outp;
static int sd_fd = -1;
static off_t sd_lba;
static const uint8_t sd_csd[17] = {

	0xFE,		/* Sync byte before CSD */
	/* Taken from a Toshiba 64MB card c/o softgun */
	0x00, 0x2D, 0x00, 0x32,
	0x13, 0x59, 0x83, 0xB1,
	0xF6, 0xD9, 0xCF, 0x80,
	0x16, 0x40, 0x00, 0x00
};

static uint8_t sd_process_command(void)
{
	if (sd_ext) {
		sd_ext = 0;
		switch(sd_cmd[0]) {
		default:
			return 0xFF;
		}
	}
	if (trace & TRACE_SD)
		fprintf(stderr, "Command received %x\n", sd_cmd[0]);
	switch(sd_cmd[0]) {
	case 0x40+0:		/* CMD 0 */
		return 0x01;	/* Just respond 0x01 */
	case 0x40+1:		/* CMD 1 - leave idle */
		return 0x00;	/* Immediately indicate we did */
	case 0x40+9:		/* CMD 9 - read the CSD */
		memcpy(sd_out,sd_csd, 17);
		sd_outlen = 17;
		sd_outp = 0;
		sd_mode = 2;
		return 0x00;
	case 0x40+16:		/* CMD 16 - set block size */
		/* Should check data is 512 !! FIXME */
		return 0x00;	/* Sure */
	case 0x40+17:		/* Read */
		sd_outlen = 514;
		sd_outp = 0;
		/* Sync mark then data */
		sd_out[0] = 0xFF;
		sd_out[1] = 0xFE;
		sd_lba = sd_cmd[4] + 256 * sd_cmd[3] + 65536 * sd_cmd[2] +
			16777216 * sd_cmd[1];
		if (trace & TRACE_SD)
			fprintf(stderr, "Read LBA %lx\n", sd_lba);
		if (lseek(sd_fd, sd_lba, SEEK_SET) < 0 || read(sd_fd, sd_out + 2, 512) != 512) {
			if (trace & TRACE_SD)
				fprintf(stderr, "Read LBA failed.\n");
			return 0x01;
		}
		sd_mode = 2;
		/* Result */
		return 0x00;
	case 0x40+24:		/* Write */
		/* Will send us FE data FF FF */
		if (trace & TRACE_SD)
			fprintf(stderr, "Write LBA %lx\n", sd_lba);
		sd_inlen = 514;	/* Data FF FF */
		sd_lba = sd_cmd[4] + 256 * sd_cmd[3] + 65536 * sd_cmd[2] +
			16777216 * sd_cmd[1];
		sd_inp = 0;
		sd_mode = 4;	/* Send a pad then go to mode 3 */
		return 0x00;	/* The expected OK */
	case 0x40+55:
		sd_ext = 1;
		return 0x01;
	default:
		return 0x7F;
	}
}

static uint8_t sd_process_data(void)
{
	switch(sd_cmd[0]) {
	case 0x40+24:		/* Write */
		sd_mode = 0;
		if (lseek(sd_fd, sd_lba, SEEK_SET) < 0 ||
			write(sd_fd, sd_in, 512) != 512) {
			if (trace & TRACE_SD)
				fprintf(stderr, "Write failed.\n");
			return 0x1E;	/* Need to look up real values */
		}
		return 0x05;	/* Indicate it worked */
	default:
		sd_mode = 0;
		return 0xFF;
	}
}

static uint8_t sd_card_byte(uint8_t in)
{
	/* No card present */
	if (sd_fd == -1)
		return 0xFF;

	if (sd_mode == 0) {
		if (in != 0xFF) {
			sd_mode = 1;	/* Command wait */
			sd_cmdp = 1;
			sd_cmd[0] = in;
		}
		return 0xFF;
	}
	if (sd_mode == 1) {
		sd_cmd[sd_cmdp++] = in;
		if (sd_cmdp == 6) {	/* Command complete */
			sd_cmdp = 0;
			sd_mode = 0;
			/* Reply with either a stuff byte (CMD12) or a
			   status */
			return sd_process_command();
		}
		/* Keep talking */
		return 0xFF;
	}
	/* Writing out the response */
	if (sd_mode == 2) {
		if (sd_outp + 1 == sd_outlen)
			sd_mode = 0;
		return sd_out[sd_outp++];
	}
	/* Commands that need input blocks first */
	if (sd_mode == 3) {
		sd_in[sd_inp++] = in;
		if (sd_inp == sd_inlen)
			return sd_process_data();
		/* Keep sending */
		return 0xFF;
	}
	/* Sync up before data flow starts */
	if (sd_mode == 4) {
		/* Sync */
		if (in == 0xFE)
			sd_mode = 3;
		return 0xFF;
	}
	return 0xFF;
}

struct z80_pio {
	uint8_t data[2];
	uint8_t mask[2];
	uint8_t mode[2];
	uint8_t intmask[2];
	uint8_t icw[2];
	uint8_t mpend[2];
	uint8_t irq[2];
	uint8_t vector[2];
	uint8_t in[2];
};

static struct z80_pio pio[1];

static uint8_t pio_cs;

/* Software SPI test: one device for now */

static uint8_t spi_byte_sent(uint8_t val)
{
	uint8_t r = sd_card_byte(val);
	if (trace & TRACE_SPI)
		fprintf(stderr,	"[SPI %02X:%02X]\n", val, r);
	return r;
}

/* Bit 2: CLK, 1: MOSI, 0: MISO */
static void bitbang_spi(uint8_t val)
{
	static uint8_t old = 0xFF;
	static uint8_t oldcs = 1;
	static uint8_t bits;
	static uint8_t bitct;
	static uint8_t rxbits = 0xFF;
	uint8_t delta = old ^ val;

	old = val;

	if ((pio_cs & 0x03) == 0x01) {		/* CS high - deselected */
		if ((trace & TRACE_SPI) && !oldcs)
			fprintf(stderr,	"[Raised \\CS]\n");
		bits = 0;
		sd_mode = 0;	/* FIXME: layering */
		oldcs = 1;
		return;
	}
	if ((trace & TRACE_SPI) && oldcs)
		fprintf(stderr, "[Lowered \\CS]\n");
	oldcs = 0;
	/* Capture clock edge */
	if (delta & 0x04) {		/* Clock edge */
		if (val & 0x04) {	/* Rising - capture in SPI0 */
			bits <<= 1;
			bits |= (val & 0x02) ? 1 : 0;
			bitct++;
			if (bitct == 8) {
				rxbits = spi_byte_sent(bits);
				bitct = 0;
			}
		} else {
			/* Falling edge */
			pio->in[1] &= 0xFE;
			pio->in[1] |= (rxbits & 0x80) ? 0x01 : 0x00;
			rxbits <<= 1;
			rxbits |= 0x01;
		}
	}
}

/* Bus emulation helpers */

void pio_data_write(struct z80_pio *pio, uint8_t port, uint8_t val)
{
	if (port == 1)
		bitbang_spi(val);
	else if (port == 2)
		pio_cs = val & 7;
}

void pio_strobe(struct z80_pio *pio, uint8_t port)
{
}

uint8_t pio_data_read(struct z80_pio *pio, uint8_t port)
{
	return pio->in[port];
}

static void pio_recalc(void)
{
	/* For now we don't model interrupts at all */
}

/* Simple Z80 PIO model. We don't yet deal with the fancy bidirectional mode
   or the strobes in mode 0-2. We don't do interrupt mask triggers on mode 3 */

/* TODO: interrupts, strobes */

static void pio_write(uint8_t addr, uint8_t val)
{
	uint8_t pio_port = (addr & 2) >> 1;
	uint8_t pio_ctrl = addr & 1;

	if (pio_ctrl) {
		if (pio->icw[pio_port] & 1) {
			pio->intmask[pio_port] = val;
			pio->icw[pio_port] &= ~1;
			pio_recalc();
			return;
		}
		if (pio->mpend[pio_port]) {
			pio->mask[pio_port] = val;
			pio_recalc();
			pio->mpend[pio_port] = 0;
			return;
		}
		if (!(val & 1)) {
			pio->vector[pio_port] = val;
			return;
		}
		if ((val & 0x0F) == 0x0F) {
			pio->mode[pio_port] = val >> 6;
			if (pio->mode[pio_port] == 3)
				pio->mpend[pio_port] = 1;
			pio_recalc();
			return;
		}
		if ((val & 0x0F) == 0x07) {
			pio->icw[pio_port] = val >> 4;
			return;
		}
		return;
	} else {
		pio->data[pio_port] = val;
		switch(pio->mode[pio_port]) {
		case 0:
		case 2:	/* Not really emulated */
			pio_data_write(pio, pio_port, val);
			pio_strobe(pio, pio_port);
			break;
		case 1:
			break;
		case 3:
			/* Force input lines to floating high */
			val |= pio->mask[pio_port];
			pio_data_write(pio, pio_port, val);
			break;
		}
	}
}

static uint8_t pio_read(uint8_t addr)
{
	uint8_t pio_port = (addr & 2) >> 1;
	uint8_t val;
	uint8_t rx;

	/* Output lines */
	val = pio->data[pio_port];
	rx = pio_data_read(pio, pio_port);

	switch(pio->mode[pio_port]) {
	case 0:
		/* Write only */
		break;
	case 1:
		/* Read only */
		val = rx;
		break;
	case 2:
		/* Bidirectional (not really emulated) */
	case 3:
		/* Control mode */
		val &= ~pio->mask[pio_port];
		val |= rx & pio->mask[pio_port];
		break;
	}
	return val;
}

static void pio_reset(void)
{
	/* Input mode */
	pio->mask[0] = 0xFF;
	pio->mask[1] = 0xFF;
	/* Mode 1 */
	pio->mode[0] = 1;
	pio->mode[1] = 1;
	/* No output data value */
	pio->data[0] = 0;
	pio->data[1] = 0;
	/* Nothing pending */
	pio->mpend[0] = 0;
	pio->mpend[1] = 0;
	/* Clear icw */
	pio->icw[0] = 0;
	pio->icw[1] = 0;
	/* No interrupt */
	pio->irq[0] = 0;
	pio->irq[1] = 0;
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

static uint8_t z84c15_read(uint8_t port)
{
	switch(port) {
	case 0xEE:
		return z84c15.scrp;
	case 0xEF:
		switch(z84c15.scrp) {
		case 0:
			return z84c15.wcr;
		case 1:
			return z84c15.mwbr;
		case 2:
			return z84c15.csbr;
		case 3:
			return z84c15.mcr;
		default:
			fprintf(stderr, "Read invalid SCRP  %d\n", z84c15.scrp);
			return 0xFF;
		}
		break;
	/* Watchdog: not yet emulated */
	case 0xF0:
	case 0xF1:
		return 0xFF;
	}
	return 0xFF;
}

static void z84c15_write(uint8_t port, uint8_t val)
{
	if (trace & TRACE_Z84C15)
		fprintf(stderr, "z84c15: write %02X <- %02X\n",
			port, val);
	switch(port) {
	case 0xEE:
		z84c15.scrp = val;
		break;
	case 0xEF:
		switch(z84c15.scrp) {
		case 0:
			z84c15.wcr = val;
			break;
		case 1:
			z84c15.mwbr = val;
			break;
		case 2:
			z84c15.csbr = val;
			break;
		case 3:
			z84c15.mcr = val;
			break;
		default:
			fprintf(stderr, "Read invalid SCRP  %d\n", z84c15.scrp);
		}
		break;
	/* Watchdog: not yet emulated */
	case 0xF0:
	case 0xF1:
		return;
	case 0xF4:
		z84c15.intpr = val;
		break;
	}
}

static uint8_t io_read_2014(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if ((addr >= 0xA0 && addr <= 0xA7) && acia && acia_narrow == 1)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow == 2)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0x83) && sio2)
		return sio2_read(addr & 3);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0xC0 && rtc)
		return rtc_read(rtc);
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
	if ((addr >= 0xA0 && addr <= 0xA7) && acia && acia_narrow == 1)
		acia_write(acia, addr & 1, val);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow == 2)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(acia, addr & 1, val);
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
		rtc_write(rtc, val);
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
		return rtc_read(rtc);
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
		rtc_write(rtc, val);
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

static uint8_t io_read_micro80(uint16_t addr)
{
	uint8_t r = addr & 0xFF;
	if (r >= 0x10 && r <= 0x13)
		return ctc_read(addr & 3);
	else if (r >= 0x18 && r <= 0x1B)
		return sio2_read((r & 3) ^ 1);
	else if (r >= 0x1C && r <= 0x1F)
		return pio_read(r & 3);
	else if (r >= 0xEE && r <= 0xF1)
		return z84c15_read(r);
	else if (r >= 0x90 && r <= 0x97)
		return my_ide_read(r & 7);
	else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_micro80(uint16_t addr, uint8_t val)
{
	uint16_t r = addr & 0xFF;
	if (r >= 0x10 && r <= 0x13)
		ctc_write(addr & 3, val);
	else if (r >= 0x18 && r <= 0x1B)
		sio2_write((r & 3) ^ 1, val);
	else if (r >= 0x1C && r <= 0x1F)
		pio_write(r & 3, val);
	else if ((r >= 0xEE && r <= 0xF1) || r == 0xF4)
		z84c15_write(r, val);
	else if (r >= 0x90 && r <= 0x97)
		my_ide_write(r & 0x07, val);
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z80:
		io_write_2014(addr, val, 0);
		break;
	case CPUBOARD_SC108:
		io_write_1(addr, val);
		break;
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		io_write_2(addr, val);
		break;
	case CPUBOARD_Z80SBC64:
		io_write_3(addr, val);
		break;
	case CPUBOARD_EASYZ80:
		io_write_4(addr, val);
		break;
	case CPUBOARD_MICRO80:
		io_write_micro80(addr, val);
		break;
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

uint8_t io_read(int unused, uint16_t addr)
{
	switch (cpuboard) {
	case CPUBOARD_Z80:
	case CPUBOARD_SC108:
		return io_read_2014(addr);
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		return io_read_2(addr);
	case CPUBOARD_Z80SBC64:
		return io_read_3(addr);
	case CPUBOARD_EASYZ80:
		return io_read_4(addr);
	case CPUBOARD_MICRO80:
		return io_read_micro80(addr);
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

static void poll_irq_event(void)
{
	if (has_im2) {
		if (acia)
			acia_check_irq(acia);
		uart_check_irq(&uart[0]);
		if (!live_irq) {
			!sio2_check_im2(sio) && !sio2_check_im2(sio + 1) &&
			!ctc_check_im2();
		}
	} else {
		if (acia)
			acia_check_irq(acia);
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
	char *sdpath = NULL;
	char *idepath = NULL;
	int save = 0;
	int has_acia = 0;
	int indev;

#define INDEV_ACIA	1
#define INDEV_SIO	2
#define INDEV_CPLD	3
#define INDEV_16C550A	4

	uint8_t *p = ramrom;
	while (p < ramrom + sizeof(ramrom))
		*p++= rand();

	while ((opt = getopt(argc, argv, "Aabcd:e:fi:m:pr:sRuw8")) != -1) {
		switch (opt) {
		case 'a':
			has_acia = 1;
			indev = INDEV_ACIA;
			acia_narrow = 0;
			sio2 = 0;
			break;
		case 'A':
			has_acia = 1;
			acia_narrow = 1;
			indev = INDEV_ACIA;
			sio2_input = 0;
			break;
		case '8':
			has_acia = 1;
			acia_narrow = 2;
			indev = INDEV_ACIA;
			sio2 = 0;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 's':
			sio2 = 1;
			sio2_input = 1;
			indev = INDEV_SIO;
			if (!acia_narrow)
				has_acia = 0;
			break;
		case 'S':
			sdpath = optarg;
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
				cpuboard = CPUBOARD_Z80;
			else if (strcmp(optarg, "sc108") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_SC108;
			} else if (strcmp(optarg, "sc114") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_SC114;
			} else if (strcmp(optarg, "z80sbc64") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_Z80SBC64;
				bankreg[0] = 3;
			} else if (strcmp(optarg, "z80mb64") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_Z80SBC64;
				bankreg[0] = 3;
				/* Triple RC2014 rate */
				tstate_steps = 369 * 3;
			} else if (strcmp(optarg, "easyz80") == 0) {
				bank512 = 1;
				cpuboard = CPUBOARD_EASYZ80;
				switchrom = 0;
				rom = 0;
				has_acia = 0;
				have_ctc = 1;
				sio2 = 1;
				sio2_input = 1;
				has_im2 = 1;
				tstate_steps = 500;
			} else if (strcmp(optarg, "sc121") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_SC121;
				sio2 = 1;
				sio2_input = 1;
				have_ctc = 1;
				rom = 0;
				has_acia = 0;
				has_im2 = 1;
				/* FIXME: SC122 is four ports */
			} else if (strcmp(optarg, "micro80") == 0) {
				cpuboard = CPUBOARD_MICRO80;
				have_ctc = 1;
				sio2 = 1;
				sio2_input = 1;
				has_im2 = 1;
				has_acia = 0;
				rom = 1;
				switchrom = 0;
				tstate_steps = 800;	/* 16MHz */
			} else {
				fputs("rc2014: supported cpu types z80, easyz80, sc108, sc114, sc121, z80sbc64, z80mb64.\n",
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
			rtc = rtc_create();
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

	if (cpuboard == CPUBOARD_Z80SBC64) {
		cpld_serial = 1;
		indev = INDEV_CPLD;
	} else if (acia == 0 && sio2 == 0) {
		if (cpuboard != 3) {
			fprintf(stderr, "rc2014: no UART selected, defaulting to 68B50\n");
			has_acia = 1;
			indev = INDEV_ACIA;
		}
	}
	if (rom == 0 && bank512 == 0) {
		fprintf(stderr, "rc2014: no ROM\n");
		exit(EXIT_FAILURE);
	}

	if (rom && cpuboard != CPUBOARD_Z80SBC64) {
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

	if (cpuboard == CPUBOARD_Z80SBC64) {
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

	if (cpuboard == CPUBOARD_MICRO80)
		z84c15_init();

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

	if (sdpath) {
		sd_fd = open(sdpath, O_RDWR);
		if (sd_fd == -1) {
			perror(sdpath);
			exit(1);
		}
	}

	if (has_acia) {
		acia = acia_create();
		if (trace & TRACE_ACIA)
			acia_trace(acia, 1);
	}
	if (rtc && (trace & TRACE_RTC))
		rtc_trace(rtc, 1);
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



	switch(indev) {
	case INDEV_ACIA:
		acia_set_input(acia, 1);
		break;
	case INDEV_SIO:
		sio2_input = 1;
		break;
	case INDEV_CPLD:
		break;
	default:
		fprintf(stderr, "Invalid input device %d.\n", indev);
	}

	pio_reset();

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
				acia_timer(acia);
			if (sio2)
				sio2_timer();
			if (has_16x50)
				uart_event(&uart[0]);
			if (cpld_serial)
				sbc64_cpld_timer();
			if (have_ctc) {
				if (cpuboard != CPUBOARD_MICRO80)
					ctc_tick(tstate_steps);
				else	/* Micro80 it's not off the CPU clock */
					ctc_tick(184);
			}
			if (cpuboard == CPUBOARD_EASYZ80) {
				/* Feed the uart clock into the CTC */
				int c;
				/* 10Mhz so calculate for 500 tstates.
				   CTC 2 runs at half uart clock */
				for (c = 0; c < 46; c++) {
					ctc_receive_pulse(0);
					ctc_receive_pulse(1);
					ctc_receive_pulse(2);
					ctc_receive_pulse(0);
					ctc_receive_pulse(1);
				}
			}
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
