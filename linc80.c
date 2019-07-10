/*
 *	Platform features
 *
 *	Z80 at 7.372MHz						DONE
 *	IM2 interrupt chain					DONE
 *	Zilog SIO/2 at 0x00-0x03				DONE
 *	Zilog CTC at 0x08-0x0B					DONE
 *	Zilog PIO at 0x18-0x1B					MINIMAL
 *	IDE at 0x10-0x17 no high or control access		DONE
 *	Control register at 0x38-0x3F				DONE
 *
 *	Additional optional peripherals (own and RC2014)	ABSENT
 *
 *	TODO:
 *	- debug interrupt blocking
 *	- Z80 PIO
 *	? SD card on Z80 PIO bitbang
 *
 *	Currently we model
 *
 *	- The Linc80 as it arrives
 *
 *	- The Linc80 with 32K banking (lift pin 9 of the 74LSO4 (IN1D) and
 *	  wire it directly to pin 6 (IN1C output or !BANK0) - or indeed just
 *	  with that 32K removed and a 512K SRAM wired for the top 32K with
 *	  A18/17/16/15 wired to CFG6, BKS 2-0 giving 512K usable memory
 *	  (16 x 32K banks).
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
#include "libz80/z80.h"
#include "ide.h"

static uint8_t rom[65536];
static uint8_t ram[65536];	/* We never use the banked 16K */
static uint8_t altram[16][32768];	/* 16K for Linc80, 32 for Linc80X */

static uint8_t romsel;
static uint8_t ramsel;
static uint8_t romdis;
static uint8_t intdis;
static uint8_t fast;
static uint8_t int_recalc;
static uint8_t linc80x;
static uint16_t banktop = 0xBFFF;
static uint16_t bankmask = 0x3FFF;
static uint8_t rsmask;

static uint8_t live_irq;

#define IRQ_SIOA	1
#define IRQ_SIOB	2
#define IRQ_CTC		3	/* 3 4 5 6 */
#define IRQ_PIO		7

static Z80Context cpu_z80;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_PAGE	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_IRQ	32
#define TRACE_CTC	64
#define TRACE_SPI	128
#define TRACE_SD	256

static int trace = 0;


static void reti_event(void);

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r;

	if (addr < 0x4000 && !romdis)
		r = rom[addr + 0x4000 * romsel];
	else if (addr >= 0x8000 && addr <= banktop) {
		/* Simulate tight decode providing crap for absent banks */
		if (ramsel > rsmask) {	/* This works as mask is power of 2 - 1 */
			if (trace & TRACE_MEM)
				fprintf(stderr, "[Read from invalid bank %d]\n", ramsel);
			r = rand();
		} else
			r = altram[ramsel & rsmask][addr & bankmask];
	} else
		r = ram[addr];
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
#if 0
	if (cpu_z80.PC >= 0x0C00 && cpu_z80.PC < 0x8000 && romdis && ramsel == 1 &&
		addr > 0x8000)
		printf("PC=%04X Wrote oddly to %04X with %02X\n",
			cpu_z80.PC, addr, val);
#endif
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %04X = %02X\n", addr, val);
	if (addr < 0x4000) {
		if (!romdis) {
			if (trace & TRACE_MEM) {
				fprintf(stderr, "[Discarded: ROM]\n");
				return;
			}
		}
		ram[addr] = val;
		return;
	}
	if (addr >= 0x8000 && addr <= banktop) {
		if (ramsel > rsmask) {
			if (trace & TRACE_MEM)
				fprintf(stderr, "[Write to invalid bank %d]\n", ramsel);
		} else
			altram[ramsel & rsmask][addr & bankmask] = val;
	}
	else
		ram[addr] = val;
}

static void recalc_interrupts(void)
{
	int_recalc = 1;
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
	/* The monitor comes up on the B channel so use B */
	if (ab == 1) {
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
			fprintf(stderr, "sio%c write data %d\n",
				(addr & 1) ? 'b' : 'a', val);
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

/* Software SPI test: one device for now */

static uint8_t spi_byte_sent(uint8_t val)
{
	uint8_t r = sd_card_byte(val);
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

	if (val & 0x08) {		/* CS high - deselected */
		if ((trace & TRACE_SPI) && (delta & 0x08))
			fprintf(stderr,	"[Raised \\CS]\n");
		bits = 0;
		sd_mode = 0;	/* FIXME: layering */
		return;
	}
	if ((trace & TRACE_SPI) && (delta & 0x08))
		fprintf(stderr, "[Lowered \\CS]\n");
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
	
static void memory_control(uint8_t val)
{
	uint8_t oldint = intdis;
	romdis = val & 1;
	intdis = val & 0x80;
	/* IRQ unmasked - recalculate state */
	if (oldint && !intdis)
		recalc_interrupts();
	if (!linc80x && (val & 0x40))
		fprintf(stderr, "[Warning: wrote reserved 0x38 bit as 1]\n");
	ramsel = (val >> 1) & 0x07;
	romsel = (val >> 4) & 0x03;
	if (linc80x) {
		if (val & 0x40)
			ramsel |= 0x8;
	}
	if (trace & TRACE_PAGE) {
		fprintf(stderr, "memory control: romdis %d intdis %d ramsel %d romsel %d.\n",
			romdis, intdis, ramsel, romsel);
		if (ramsel > rsmask)
			fprintf(stderr, "[Warning: selected invalid ram bank]\n");
	}
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x00 && addr <= 0x07)
		return sio2_read(addr & 3);
	if (addr >= 0x08 && addr <= 0x0F)
		return ctc_read(addr & 3);
	if (addr >= 0x10 && addr <= 0x17)
		return my_ide_read(addr & 7);
	if (addr >= 0x18 && addr <= 0x1F)
		return pio_read(addr & 3);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr >= 0x00 && addr <= 0x07)
		sio2_write(addr & 3, val);
	else if (addr >= 0x08 && addr <= 0x0B)
		ctc_write(addr & 3, val);
	else if (addr >= 0x10 && addr <= 0x17)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x18 && addr <= 0x1F)
		pio_write(addr & 3, val);
	else if (addr >= 0x38 && addr <= 0x3F)
		memory_control(val);
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
	!intdis && !sio2_check_im2(sio) && !sio2_check_im2(sio + 1)
		&& !ctc_check_im2();

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
		"linc80: [-x] [-f] [-r rompath] [-i idepath] [-s sdcard] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "linc80.rom";
	char *idepath = "linc80.ide";
	char *sdpath = NULL;
	int banks = 1;

	while ((opt = getopt(argc, argv, "r:i:d:fxb:s:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'i':
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
		case 'x':
			linc80x = 1;
			banktop = 0xFFFF;
			bankmask = 0x7FFF;
			break;
		case 'b':
			banks = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (banks == 1)
		rsmask = 0;
	else if (banks == 2)
		rsmask = 1;
	else if (banks == 4)
		rsmask = 3;
	else if (banks == 8)
		rsmask = 7;
	else if (banks == 16 && linc80x)
		rsmask = 15;
	else if (banks == 32 && linc80x)
		rsmask = 31;
	else {
		fprintf(stderr, "linc80: invalid number of banks.\n");
		exit(1);
	}

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, rom, 65536) < 8192) {
		fprintf(stderr, "linc80: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	ide0 = ide_allocate("cf");
	if (ide0) {
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		}
		if (ide_attach(ide0, 0, fd) == 0) {
			ide = 1;
			ide_reset_begin(ide0);
		} else
			exit(1);
	}

	if (sdpath) {
		sd_fd = open(sdpath, O_RDWR);
		if (sd_fd == -1) {
			perror(sdpath);
			exit(1);
		}
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

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 100 times then poll the
	   slow stuff and nap for 5ms. */
	while (1) {
		int i;
		/* 36400 T states */
		for (i = 0; i < 100; i++) {
			Z80ExecuteTStates(&cpu_z80, 369);
			sio2_timer();
			ctc_tick(364);
		}
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
