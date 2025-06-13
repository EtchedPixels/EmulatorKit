/*
 *	Platform features
 *
 *	Z80 at 7.372MHz						DONE		SC516 / SC518/9
 *	IM2 interrupt chain					DONE		SC511
 *	Zilog SIO/2 at 0x00-0x03				DONE		SC511
 *	Zilog CTC at 0x08-0x0B					DONE		SC511
 *	Zilog PIO at 0x18-0x1B					MINIMAL		SC509
 *	IDE at 0x10-0x17 no high or control access		DONE		SC504
 *	Control registers for meory				DONE		SC516 / 8 / 9
 *
 *	Additional optional peripherals (own and rcbus)		ABSENT
 *
 *	Not supported: SC527. This has a bitbang serial output at 0x20/28 and jumper
 *	selectable ROM bank. Apart from the bitbang serial it's basically an SC516 to us
 *
 *	TODO:
 *	- debug interrupt blocking
 *	- Z80 PIO fill out
 *	- SC120 ACIA (note that this has serious issues with interrupt handling and will
 *	  randomly crash the machine if used with \INT in IM2 mode)
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
#include <sys/select.h>
#include "libz80/z80.h"
#include "z80dis.h"

#include "ide.h"
#include "sdcard.h"

static uint8_t rom[131072];
static uint8_t ram[131072];	/* We never use the banked 16K */

static unsigned sc519;

static uint8_t romsel;
static uint8_t ramsel;
static uint8_t romdis;
static uint8_t intdis;
static uint8_t fast;
static uint8_t int_recalc;

static uint8_t live_irq;

#define IRQ_SIOA	1
#define IRQ_SIOB	2
#define IRQ_CTC		3	/* 3 4 5 6 */
#define IRQ_PIO		7

static Z80Context cpu_z80;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_CPU	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_IRQ	32
#define TRACE_CTC	64
#define TRACE_SPI	128
#define TRACE_SD	256

static int trace = 0;

static struct ide_controller *ide0;
static struct ide_controller *ide1;
static struct sdcard *sdcard;

static void reti_event(void);

/* We deal with the differences in the ROM select in the
   I/O side not here */
static uint8_t *mem_addr(uint16_t addr, unsigned is_write)
{
	if (romdis || addr >= 0x8000)
		return ram + addr + 0x10000 * ramsel;
	if (is_write)
		return NULL;
	return rom + (addr & 0x7FFF) + 0x8000 * romsel;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r;

	r = *mem_addr(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, r);

	/* Look for ED with M1, followed directly by 4D and if so trigger
	   the interrupt chain */
	if (cpu_z80.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r== 0xCB) {
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
	if (r == 0x4D && rstate == 1) {
		if (trace & TRACE_IRQ)
			fprintf(stderr, "RETI seen.\n");
		reti_event();
	}
	rstate = 0;
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t *ptr = mem_addr(addr, 1);
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %04X = %02X\n", addr, val);
	if (ptr == NULL) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
		return;
	}
	*ptr = val;
}

static void recalc_interrupts(void)
{
	int_recalc = 1;
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = mem_read(0, addr);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return mem_read(1, addr);
}

static void z80_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z80.R1.br.A, cpu_z80.R1.br.F,
		cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL,
		cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
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

static struct z80_sio_chan sio[2];

/*
 *	Interrupts
 */

static void sio2_clear_int(struct z80_sio_chan *chan, uint8_t m)
{
	chan->intbits &= ~m;
	chan->pending &= ~m;

	if (!(sio->intbits | sio[1].intbits)) {
		sio->rr[1] &= ~0x02;
		chan->irq = 0;
	}
}

static void sio2_raise_int(struct z80_sio_chan *chan, uint8_t m)
{
	uint8_t new = (chan->intbits ^ m) & m;
	uint8_t vector;
	chan->intbits |= m;
	chan->pending |= new;
	if (trace & TRACE_SIO)
		fprintf(stderr, "SIO raise int %x new = %x\n", m, new);
	if (chan->pending) {
		if (!sio->irq) {
			chan->irq = 1;
			sio->rr[1] |= 0x02;
			vector = 0;
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
			if (trace & (TRACE_SIO | TRACE_IRQ))
				fprintf(stderr,
					"SIO2 interrupt %02X\n", vector);
			chan->vector = vector;
			recalc_interrupts();
		}
	}
}

static void sio2_reti(struct z80_sio_chan *chan)
{
	if (chan->irq)
		chan->irq = 0;
	/* Recalculate the pending state and vectors */
	sio2_raise_int(chan, 0);
	if (trace & (TRACE_IRQ|TRACE_SIO))
		fprintf(stderr, "Acked interrupt from SIO.\n");
}

static int sio2_check_im2(struct z80_sio_chan *chan)
{
	/* See if we have an IRQ pending and if so deliver it and return 1 */
	if (chan->irq) {
		/* FIXME: quick fix for now but the vector calculation should all be
		   done here it seems */
		if (sio[1].wr[1] & 0x04)
			chan->vector += (sio[1].wr[2] & 0xF1);
		else
			chan->vector += sio[1].wr[2];
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
		fprintf(stderr, "SIO %d queue %d: ",
			(int) (chan - sio), c);
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
			fprintf(stderr, "Queued %d (mode %d)\n",
				chan->dptr, chan->wr[1] & 0x18);
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
		if (c & 1)
			sio2_queue(chan, next_char());
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
	chan->rr[0] = 0x6C;
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
	struct z80_sio_chan *chan = (addr & 1) ? sio + 1 : sio;
	if (addr & 2) {
		/* Control */
		uint8_t r = chan->wr[0] & 007;
		chan->wr[0] &= ~007;

		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c read reg %d = ",
				(addr & 1) ? 'b' : 'a', r);
		switch (r) {
		case 0:
		case 1:
			if (trace & TRACE_SIO)
				fprintf(stderr, "%02X\n", chan->rr[r]);
			return chan->rr[r];
		case 2:
			if (chan != sio) {
				if (trace & TRACE_SIO)
					fprintf(stderr, "%02X\n",
						chan->rr[2]);
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
			fprintf(stderr, "sio%c read data %d\n",
				(addr & 1) ? 'b' : 'a', c);
		if (chan->dptr && (chan->wr[1] & 0x10))
			sio2_raise_int(chan, INT_RX);
		return c;
	}
	return 0xFF;
}

static void sio2_write(uint16_t addr, uint8_t val)
{
	struct z80_sio_chan *chan = (addr & 1) ? sio + 1 : sio;
	uint8_t r;
	if (addr & 2) {
		if (trace & TRACE_SIO)
			fprintf(stderr,
				"sio%c write reg %d with %02X\n",
				(addr & 1) ? 'b' : 'a',
				chan->wr[0] & 7, val);
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
					fprintf(stderr,
						"[channel reset]\n");
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
					sio2_clear_int(sio,
						       INT_RX |
						       INT_TX | INT_ERR);
					sio2_clear_int(sio + 1,
						       INT_RX |
						       INT_TX | INT_ERR);
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
			fprintf(stderr, "sio%c write data %d\n",
				(addr & 1) ? 'b' : 'a', val);
		write(1, &val, 1);
	}
}

static uint8_t my_ide_read(struct ide_controller *ide, uint16_t addr)
{
	return ide_read8(ide, addr);
}

static void my_ide_write(struct ide_controller *ide, uint16_t addr, uint8_t val)
{
	ide_write8(ide, addr, val);
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
	ctc_irqmask &= ~(1 << ctcnum);
	if (trace & TRACE_IRQ)
		fprintf(stderr, "Acked interrupt from CTC %d.\n", ctcnum);
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
		/* Check rule on resets */
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
				recalc_interrupts();
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

/* Software SPI test: one device for now */

static uint8_t spi_byte_sent(uint8_t val)
{
	uint8_t r = sd_spi_in(sdcard, val);
	if (trace & TRACE_SPI)
		fprintf(stderr,	"[SPI %02X:%02X]\n", val, r);
	fflush(stdout);
	return r;
}

static void bitbang_spi(uint8_t val)
{
	static uint8_t old = 0xFF;
	static uint8_t bits;
	static uint8_t bitct;
	static uint8_t rxbits = 0xFF;
	uint8_t delta = val ^ old;
	old = val;

	if (sdcard == NULL)
		return;

	if (val & 0x08) {		/* CS high - deselected */
		if ((trace & TRACE_SPI) && (delta & 0x08))
			fprintf(stderr,	"[Raised \\CS]\n");
		bits = 0;
		sd_spi_raise_cs(sdcard);
		return;
	}
	if (delta & 0x08) {
		if (trace & TRACE_SPI)
			fprintf(stderr, "[Lowered \\CS]\n");
		sd_spi_lower_cs(sdcard);
	}
	/* Capture clock edge */
	if (delta & 0x02) {		/* Clock edge */
		if (val & 0x02) {	/* Rising - capture in SPI0 */
			bits <<= 1;
			bits |= (val & 0x04) ? 1 : 0;
			bitct++;
			if (bitct == 8) {
				rxbits = spi_byte_sent(bits);
				bitct = 0;
			}
		} else {
			/* Falling edge */
			pio->in[1] = (rxbits & 0x80) ? 0x01 : 0x00;
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
	uint8_t pio_port = addr & 1;
	uint8_t pio_ctrl = addr & 2;

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
	uint8_t pio_port = addr & 1;
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

static void bitbang_serout(unsigned val)
{
	fprintf(stderr, "%c", "01"[val & 1]);
}

static void leds(unsigned port, unsigned bits)
{
	unsigned n = 7;
	printf("[%02X]: ", port & 0xFF);
	while(n) {
		if (bits & (1 << n))
			fputc('1', stdout);
		else
			fputc('0', stdout);
		n--;
	}
	printf("\n");
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x83)
		return sio2_read(addr & 3);
	if (addr >= 0x88 && addr <= 0x8B)
		return ctc_read(addr & 3);
	if (addr >= 0x90 && addr <= 0x97)
		return my_ide_read(ide0, addr & 7);
	if (addr >= 0x98 && addr <= 0x9B)
		return pio_read(addr & 3);
	if (addr >= 0xb0 && addr <= 0xB7)
		return my_ide_read(ide1, addr & 7);
	if (addr == 0x28) {
		/* TODO: receive data bit on 7 */
		return 0xFF;
	}
	if (addr == 0xA0) {
		/* GPIO in */
		return 0xFF;
	}
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0x78;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr == 0x08)
		leds(0x08, val);
	else if (addr == 0xA0)
		leds(0xA0, val);
	else if (addr >= 0x80 && addr <= 0x83)
		sio2_write(addr & 3, val);
	else if (addr >= 0x88 && addr <= 0x8B)
		ctc_write(addr & 3, val);
	else if (addr >= 0x90 && addr <= 0x97)
		my_ide_write(ide0, addr & 7, val);
	else if (addr >= 0x98 && addr <= 0x9B)
		pio_write(addr & 3, val);
	else if (addr >= 0xB0 && addr <= 0xB7)
		my_ide_write(ide1, addr & 7, val);
	else if (addr == 0x20 && sc519) {
		romsel &= 1;
		romsel |= (val & 2) >> 1;
	} else if (addr == 0x28) {
		if (sc519) {
			romsel &= 2;
			romsel |= (val & 1);
		} else
			bitbang_serout(val & 1);
	} else if (addr == 0x30)
		ramsel = val & 1;
	else if (addr == 0x38)
		romdis = val & 1;
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
}

/*
 *	We saw a reti. Ask the current source to clear down, then work out
 *	who delivers next. Also used when we need to check for new interrupts
 *	and there is no interrupt pending.
 */
static void reti_event(void)
{
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
/*	case IRQ_PIO:
		pio_reti();
		break; */
	default:
		break;
	}

	live_irq = 0;

	/* See who delivers next */
	if (!intdis && !sio2_check_im2(sio))
		if (!sio2_check_im2(sio + 1))
			ctc_check_im2();

	/* If nothing is pending we end up here and we continue with live_irq
	   clear. A call to recalc_interrupts will then trigger the interrupt
	   processing within the normal flow */
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
	fprintf(stderr,
		"z50bus-z80: [-x] [-f] [-b banks] [-r rompath] [-i idepath] [-s sdcard] [-d debug]\n");
	exit(EXIT_FAILURE);
}

static void load(const char *path)
{
	int fd = open(path, O_RDONLY);
	int len;
	if (fd == -1) {
		perror(path);
		exit(1);
	}
	len = read(fd, ram + 0x8000, 0x8000);
	close(fd);
	printf("Loaded %d bytes from %s at 0x8000\n", len, path);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "z50bus-z80.rom";
	char *idepath = NULL;
	char *idepath2 = NULL;
	char *sdpath = NULL;
	unsigned size;

	while ((opt = getopt(argc, argv, "r:i:d:fs:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			if (idepath) {
				if (idepath2) {
					fprintf(stderr, "z50bus-z80: too many CF images specified.\n");
					exit(1);
				}
				idepath2 = optarg;
			} else
				idepath = optarg;
			break;
		case 's':
			sdpath = optarg;
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
		load(argv[optind++]);

	if (optind < argc)
		usage();

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	size = read(fd, rom, 131072);
	if (size == 32768)
		sc519 = 0;
	else if (size == 131072)
		sc519 = 1;
	else {
		fprintf(stderr, "z50bus-z80: invalid ROM size '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	ide0 = ide_allocate("cf0");
	ide1 = ide_allocate("cf1");

	if (idepath) {
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		}
		if (ide_attach(ide0, 0, fd) == 0)
			ide_reset_begin(ide0);
		else
			exit(1);
	}

	if (idepath2) {
		fd = open(idepath2, O_RDWR);
		if (fd == -1) {
			perror(idepath2);
			exit(1);
		}
		if (ide_attach(ide1, 0, fd) == 0)
			ide_reset_begin(ide1);
		else
			exit(1);
	}

	if (sdpath) {
		sdcard = sd_create("sd0");
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
		if (trace & TRACE_SD)
			sd_trace(sdcard, 1);
	}

	sio_reset();
	ctc_init();
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
	cpu_z80.trace = z80_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 100 times then poll the
	   slow stuff and nap for 5ms. */
	while (1) {
		unsigned i;
		unsigned j;
		/* 36400 T states */
		for (i = 0; i < 100; i++) {
			Z80ExecuteTStates(&cpu_z80, 369);
			if (!fast)
				sio2_timer();
			ctc_tick(369);
			for (j = 0; j < 369 / 4; j++)
				ctc_receive_pulse(2);
		}
		/* Hack so with -f you can paste SCM downloads into a terminal window */
		if (fast)
			sio2_timer();
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending IRQ but we think there now
			   might be one we use the same logic as for reti */
			if (!live_irq)
				reti_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z80.IFF1|cpu_z80.IFF2))
				int_recalc = 0;
		}
	}
	exit(0);
}
