#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "serialdevice.h"
#include "system.h"
#include "z80sio.h"

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
	uint8_t vector;		/* Vector pending to deliver */
	unsigned trace;
	struct serial_device *dev;
	struct z80_sio *sio;
	char unit;
};

struct z80_sio {
	struct z80_sio_chan chan[2];
	struct z80_sio_chan *irqchan;	/* Pointer to channel currently running the live interrupt */
	uint8_t vector;			/* Pending vector if channel interrupting */
};

static void sio_clear_int(struct z80_sio_chan *chan, uint8_t m)
{
	struct z80_sio *sio = chan->sio;
	chan->intbits &= ~m;
	/* Check me - does it auto clear down or do you have to reti it ? */
	if (!(sio->chan[0].intbits | sio->chan[1].intbits)) {
		sio->chan[0].rr[1] &= ~0x02;
		chan->irq = 0;
		if (sio->irqchan == chan)
			sio->irqchan = NULL;
	}
}

static void sio_raise_int(struct z80_sio_chan *chan, uint8_t m, unsigned recalc)
{
	struct z80_sio *sio = chan->sio;
	uint8_t new = (chan->intbits ^ m) & m;
	uint8_t vector;
	chan->intbits |= m;
	if ((chan->trace) && new)
		fprintf(stderr, "SIO raise int %x new = %x\n", m, new);
	if ((new || recalc) && chan->intbits) {
		if (!chan->irq) {
			chan->irq = 1;
			sio->chan[0].rr[1] |= 0x02;
			vector = 0; /* sio[1].wr[2] is added when delivered */
			/* This is a subset of the real options. FIXME: add
			   external status change */
			if (chan->sio->chan[1].wr[1] & 0x04) {
				vector &= 0xF1;
				if (chan == sio->chan)
					vector |= 1 << 3;
				if (chan->intbits & INT_RX)
					vector |= 4;
				else if (chan->intbits & INT_ERR)
					vector |= 2;
			}
			if (chan->trace)
				fprintf(stderr, "sio%c interrupt %02X\n", chan->unit, vector);
			chan->vector = vector;
		}
	}
}

static void sio_recalc_interrupts(struct z80_sio *sio)
{
	sio_raise_int(sio->chan, 0, 1);
	sio_raise_int(sio->chan + 1, 0, 1);
}

static int sio_check_im2_chan(struct z80_sio_chan *chan)
{
	struct z80_sio *sio = chan->sio;
	uint8_t vector = sio->chan[1].wr[2];	/* B R2 */
	/* One is in progress still */
	if (sio->irqchan)
		return -1;
	/* See if we have an IRQ pending and if so deliver it and return 1 */
	if (chan->irq) {
		/* The vector seems to take the port B register value when it is deliverable */
		if (sio->chan[1].wr[1] & 0x04) {
			vector &= 0xF1;
			/* TODO: external status change */
			if (chan->unit == 0)
				vector |= 8;
			if (chan->intbits & INT_RX)
				vector |= 4;
			if (chan->intbits & INT_ERR)
				vector |= 2;
			if (chan->trace)
				fprintf(stderr, "SIO2 interrupt %02X\n", vector);
			chan->vector = vector;
		} else {
			chan->vector = vector;
		}

		if (chan->trace)
			fprintf(stderr, "New live interrupt pending is sio%c (%02X).\n",
				chan->unit, chan->vector);
		/* The act of delivering a tx int clears it as it is generated on going empty not
		   when empty. Be careful of the order as this will clear chan->irq and irqchan
		   potentially */
		sio_clear_int(chan, INT_TX);
		sio->vector = chan->vector;
		sio->irqchan = chan;
		return chan->vector;
	}
	return -1;
}


/*
 *	The SIO replaces the last character in the FIFO on an
 *	overrun.
 */
static void sio_queue(struct z80_sio_chan *chan, uint8_t c)
{
	if (chan->trace)
		fprintf(stderr, "SIO %c queue %d: ",
			chan->unit, c);
	/* Receive disabled */
	if (!(chan->wr[3] & 1)) {
		if (chan->trace)
			fprintf(stderr, "RX disabled.\n");
		return;
	}
	/* Overrun */
	if (chan->dptr == 2) {
		if (chan->trace)
			fprintf(stderr, "Overrun.\n");
		chan->data[2] = c;
		chan->rr[1] |= 0x20;	/* Overrun flagged */
		/* What are the rules for overrun delivery FIXME */
		sio_raise_int(chan, INT_ERR, 0);
	} else {
		/* FIFO add */
		if (chan->trace)
			fprintf(stderr, "Queued %d (mode %d)\n",
				chan->dptr, chan->wr[1] & 0x18);
		chan->data[chan->dptr++] = c;
		chan->rr[0] |= 1;
		switch (chan->wr[1] & 0x18) {
		case 0x00:
			break;
		case 0x08:
			if (chan->dptr == 1)
				sio_raise_int(chan, INT_RX, 0);
			break;
		case 0x10:
		case 0x18:
			sio_raise_int(chan, INT_RX, 0);
			break;
		}
	}
	/* Need to deal with interrupt results */
}

static void sio_channel_timer(struct z80_sio_chan *chan, uint8_t ab)
{
	int c = chan->dev->ready(chan->dev);
	if (c & 1)
		sio_queue(chan, chan->dev->get(chan->dev));
	if (c & 2) {
		if (!(chan->rr[0] & 0x04)) {
			chan->rr[0] |= 0x04;
			if (chan->wr[1] & 0x02)
				sio_raise_int(chan, INT_TX, 0);
		}
	}
}

static void sio_channel_reset(struct z80_sio_chan *chan)
{
	chan->rr[0] = 0x2C;
	chan->rr[1] = 0x01;
	chan->rr[2] = 0;
	sio_clear_int(chan, INT_RX | INT_TX | INT_ERR);
}

uint8_t sio_read(struct z80_sio *sio, uint8_t addr)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio->chan + 1: sio->chan;
	if (addr & 1) {
		/* Control */
		uint8_t r = chan->wr[0] & 007;
		chan->wr[0] &= ~007;

		chan->rr[0] &= ~2;
		if (chan == sio->chan && (chan->sio->chan[0].intbits | chan->sio->chan[1].intbits))
			chan->rr[0] |= 2;

		if (chan->trace)
			fprintf(stderr, "sio%c read reg %d = ",
				(addr & 2) ? 'b' : 'a', r);
		switch (r) {
		case 0:
		case 1:
			if (chan->trace)
				fprintf(stderr, "%02X\n", chan->rr[r]);
			return chan->rr[r];
		case 2:
			if (chan != sio->chan) {
				if (chan->trace)
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
		uint8_t c = chan->data[0];
		chan->data[0] = chan->data[1];
		chan->data[1] = chan->data[2];
		if (chan->dptr)
			chan->dptr--;
		if (chan->dptr == 0)
			chan->rr[0] &= 0xFE;	/* Clear RX pending */
		sio_clear_int(chan, INT_RX);
		chan->rr[0] &= 0x3F;
		chan->rr[1] &= 0x3F;
		if (chan->trace)
			fprintf(stderr, "sio%c read data %d\n",
				(addr & 2) ? 'b' : 'a', c);
		if (chan->dptr && (chan->wr[1] & 0x10))
			sio_raise_int(chan, INT_RX, 0);
		return c;
	}
	return 0xFF;
}

void sio_write(struct z80_sio *sio, uint8_t addr, uint8_t val)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio->chan + 1 : sio->chan;
	uint8_t r;
	if (addr & 1) {
		if (chan->trace)
			fprintf(stderr,
				"sio%c write reg %d with %02X\n",
				(addr & 2) ? 'b' : 'a',
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
				sio_clear_int(chan, INT_ERR);
				chan->rr[1] &= 0xCF;	/* Clear status bits on rr0 */
				if (chan->trace)
					fprintf(stderr,
						"[extint reset]\n");
				break;
			case 030:	/* Channel reset */
				if (chan->trace)
					fprintf(stderr,
						"[channel reset]\n");
				sio_channel_reset(chan);
				break;
			case 040:	/* Enable interrupt on next rx */
				chan->rxint = 1;
				break;
			case 050:	/* Reset transmitter interrupt pending */
				chan->txint = 0;
				sio_clear_int(chan, INT_TX);
				break;
			case 060:	/* Reset the error latches */
				chan->rr[1] &= 0x8F;
				break;
			case 070:	/* Return from interrupt (channel A) */
				if (chan == sio->chan) {
					sio->chan[0].irq = 0;
					sio->chan[0].rr[1] &= ~0x02;
					sio_clear_int(sio->chan,
						       INT_RX |
						       INT_TX | INT_ERR);
					sio_clear_int(sio->chan + 1,
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
			if (chan->trace)
				fprintf(stderr, "sio%c: wrote r%d to %02X\n",
					(addr & 2) ? 'b' : 'a', r, val);
			chan->wr[r] = val;
			if (chan != sio->chan && r == 2)
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
		sio_clear_int(chan, INT_TX);
		if (chan->trace)
			fprintf(stderr, "sio%c write data %d\n",
				(addr & 2) ? 'b' : 'a', val);
		chan->dev->put(chan->dev, val);
	}
}

void sio_set_dcd(struct z80_sio *sio, unsigned chan, unsigned onoff)
{
	if (onoff)
		sio->chan[chan].rr[0] &= ~0x08;
	else
		sio->chan[chan].rr[0] |= 0x08;
	if (sio->chan[chan].wr[1] & 1)
		sio_raise_int(sio->chan + chan, INT_ERR, 0);	/* External/status */
}

void sio_timer(struct z80_sio *sio)
{
	sio_channel_timer(sio->chan, 0);
	sio_channel_timer(sio->chan + 1, 1);
}

void sio_reset(struct z80_sio *sio)
{
	sio_channel_reset(sio->chan);
	sio_channel_reset(sio->chan + 1);
	sio->irqchan = NULL;
}

void sio_reti(struct z80_sio *sio)
{
	struct z80_sio_chan *chan = sio->irqchan;
	/* Recalculate the pending state and vectors */
	sio->irqchan = NULL;
	if (chan)
		chan->irq = 0;
}

struct z80_sio *sio_create(void)
{
	struct z80_sio *sio = malloc(sizeof(struct z80_sio));
	if (sio == NULL) {
		fprintf(stderr, "sio: out of memory.\n");
		exit(1);
	}
	memset(sio, 0, sizeof(*sio));
	sio->chan[0].unit = 'a';
	sio->chan[1].unit = 'b';
	sio->chan[0].sio = sio;
	sio->chan[1].sio = sio;
	sio_reset(sio);
	return sio;
}

void sio_destroy(struct z80_sio *sio)
{
	free(sio);
}

void sio_trace(struct z80_sio *sio, unsigned chan, unsigned trace)
{
	sio->chan[chan].trace = trace;
}

void sio_attach(struct z80_sio *sio, unsigned chan, struct serial_device *dev)
{
	sio->chan[chan].dev = dev;
}

int sio_check_im2(struct z80_sio *sio)
{
	int r;
	sio_recalc_interrupts(sio);
	r = sio_check_im2_chan(sio->chan);
	if (r != -1)
		return r;
	return sio_check_im2_chan(sio->chan + 1);
}

uint8_t sio_get_wr(struct z80_sio *sio, unsigned chan, unsigned r)
{
	return sio->chan[chan].wr[r];
}
