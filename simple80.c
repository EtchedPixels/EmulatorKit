/*
 *	Simple80: Bill Shen's glueless Z80 board
 *
 *	Platform features
 *
 *	Z80 at 7.372MHz
 *	Zilog SIO/2 at 0x0XXXXXXX
 *	IDE CF at 0x10XXXXXX
 *
 *	I/O is mapped as
 *	0 SIO A data
 *	1 SIO A control
 *	2 SIO B data
 *	3 SIO B ctrl
 *
 *	The SIO controls the RAM and ROM mappings. The ROM maps the entire
 *	space when mapped but writes affect the underlying RAM. Only a write
 *	to the underlying RAM of the value in ROM is permitted when ROM is
 *	paged in.
 *
 *	Also emulated: RTC card at 0xC0, CTC at 0xD0
 *
 *	The following variants are emulated
 *	-b: Simple80 as built with R16 wrongly wired to VCC
 *	-1: Simple80 with RTSB wired to A16 and DTRB to CS2
 *		(emulates wiring to allow 512K RAM but 128K fitted)
 *	-5: Simple80 with 512 RAM via RTSB/DTRB/RTSA
 *
 *	Not yet emulated
 *	- SPI hack
 *	- Timer hack
 *	- SIO relocation hack
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

static uint8_t ram[512 * 1024];
static uint8_t rom[65536];

static uint8_t romen = 1;
static uint8_t fast = 0;
static uint8_t banknum = 1;
static uint8_t ramen = 0;	/* For read, write is always enabled */
static uint8_t ramen2 = 1;	/* CS2 - pulled up on unmodified board */
static uint8_t ram128 = 0;
static uint8_t ram512 = 0;
static uint8_t boardmod = 0;	/* Unmodified */

static unsigned int r16bug = 0;

static Z80Context cpu_z80;
static uint8_t int_recalc = 0;

/* IRQ source that is live */
static uint8_t live_irq;

#define IRQ_SIOA	1
#define IRQ_SIOB	2
#define IRQ_CTC		3	/* 3,4,5,6 */

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_RTC	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_BANK	32
#define TRACE_IRQ	64
#define TRACE_CTC	128

static int trace = 0;

static void reti_event(void);

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate;
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	if (romen) {
		r = rom[addr];
		if (r != ram[addr + 65536 * banknum] && ramen && ramen2)
			fprintf(stderr, "[PC = %04X: RHAZARD %04X %02x %02X]\n",
				cpu_z80.PC, addr, rom[addr], ram[addr + 65536 * banknum]);
	} else if (ramen && ramen2) {
		r = ram[addr + 65536 * banknum];
	} else {
		fprintf(stderr, "[PC = %04X: NO DATA %04X]\n",
				cpu_z80.PC, addr);
		r = 0xFF;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);

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
	if (r == 0x4D && rstate == 1)
		reti_event();
	rstate = 0;
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	if (romen && val != rom[addr]) {
		fprintf(stderr, "[PC = %04X: WHAZARD %04X %02x %02X]\n",
			cpu_z80.PC, addr, rom[addr], val);
	}
	ram[addr + 65536 * banknum] = val;
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

static void recalc_interrupts(void)
{
	int_recalc = 1;
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
	uint8_t vector;
	chan->intbits |= m;
	if ((trace & TRACE_SIO) && new)
		fprintf(stderr, "SIO raise int %x new = %x\n", m, new);
	if (new) {
		if (!sio->irq) {
			chan->irq = 1;
			sio->rr[1] |= 0x02;
			vector = 0; /* sio[1].wr[2]; */
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
	if (addr & 1) {
		/* Control */
		uint8_t r = chan->wr[0] & 007;
		chan->wr[0] &= ~007;

		chan->rr[0] &= ~2;
		if (chan == sio && (sio[0].intbits | sio[1].intbits))
			chan->rr[0] |= 2;

		if (trace & TRACE_SIO)
			fprintf(stderr, "sio%c read reg %d = ",
				(addr & 2) ? 'b' : 'a', r);
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
				(addr & 2) ? 'b' : 'a', c);
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
	if (addr & 1) {
		if (trace & TRACE_SIO)
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
				sio2_clear_int(chan, INT_ERR);
				chan->rr[1] &= 0xCF;	/* Clear status bits on rr0 */
				if (trace & TRACE_SIO)
					fprintf(stderr,
						"[extint reset]\n");
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
			if (chan == sio) {
				romen = (val & 0x40) ? 0 : 1;
				if (trace & TRACE_BANK)
					fprintf(stderr, "[ROMen = %d.]\n", romen);
			} else if (!r16bug && !boardmod) {
				banknum = (val & 0x40) ? 0 : 1;
				if (trace & TRACE_BANK)
					fprintf(stderr, "[RAM A16 = %d.]\n", banknum);
			}
			/* Fall through */
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
			if (r == 5 && chan == sio) {
				/* Setting DTRA high sets the pin low which turns on
				   the RAM */
				ramen = (val & 0x80) ? 1 : 0;
				if (trace & TRACE_BANK)
					fprintf(stderr, "[RAMen = %d.]\n", ramen);
			}
			if (boardmod) {
				/* Modified board */
				if (r == 5 && chan == sio && ram512) {
					banknum &= ~0x04;
					/* RTSA is A18 */
					banknum |= (val & 0x02) ? 0x00 : 0x04;
					if (trace & TRACE_BANK)
						fprintf(stderr, "[banknum = %d.]\n", banknum);
				}
				/* DTRB is A17, RTSB is A16 */
				if (r == 5 && chan == sio + 1) {
					banknum &= ~0x03;
					/* DTRB is A17 */
					banknum |= (val & 0x80) ? 0x00 : 0x02;
					/* RTSB is A16 */
					banknum |= (val & 0x02) ? 0x00 : 0x01;
					/* If the RAM chip is a 128K RAM then A18
					   is not connected and A17 actually drives
					   CS1 which needs to be high */
					if (ram128) {
						/* DTRB high so !DTRB pin low */
						if (banknum & 0x02)
							ramen2 = 0;
						else
							ramen2 = 1;
						banknum &= 1;
					}
					if (trace & TRACE_BANK)
						fprintf(stderr, "[banknum = %d, RAMen2 = %d.]\n", banknum, ramen2);
				}
			}
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
				(addr & 2) ? 'b' : 'a', val);
		write(1, &val, 1);
	}
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	if (ide0 == NULL)
		return 0xFF;
	return ide_read8(ide0, addr);
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (ide0)
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


static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (!(addr & 0x80))
		return sio2_read(addr & 3);
	if (ide && !(addr & 0x40))
		return my_ide_read(addr & 7);
	if ((addr & 0xF0) == 0xC0)
		return rtc_read();
	if ((addr & 0xF0) == 0xD0)
		return ctc_read(addr & 3);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (!(addr & 0x80))
		sio2_write(addr & 3, val);
	else if (ide && !(addr & 0x40))
		my_ide_write(addr & 7, val);
	else if ((addr & 0xF0) == 0xC0)
		rtc_write(val);
	else if ((addr & 0xF0) == 0xD0)
		ctc_write(addr & 3, val);
	else if (addr == 0xFD)
		trace = val;
	else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
}

static void poll_irq_event(void)
{
	if (!sio2_check_im2(sio))
		sio2_check_im2(sio + 1);
	ctc_check_im2();
}

static void reti_event(void)
{
	sio2_reti(sio);
	sio2_reti(sio + 1);
	ctc_reti(0);
	ctc_reti(1);
	ctc_reti(2);
	ctc_reti(3);
	live_irq = 0;
	poll_irq_event();
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
	fprintf(stderr, "simple80: [-f] [-t] [-i path] [-r path] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "simple80.rom";
	char *idepath = "simple80.cf";

	while ((opt = getopt(argc, argv, "d:i:r:ftb15")) != -1) {
		switch (opt) {
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
		case 'b':
			r16bug = 1;
			break;
		case '1':
			/* Modified board, 128K RAM */
			ram128 = 1;
			ram512 = 0;
			boardmod = 1;
			break;
		case '5':
			/* Modified board, 512K RAM */
			ram512 = 1;
			ram128 = 0;
			boardmod = 1;
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
	l = read(fd, rom, 65536);
	if (l < 65536) {
		fprintf(stderr, "simple80: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	ide0 = ide_allocate("cf");
	if (ide0) {
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			ide = 0;
		} else if (ide_attach(ide0, 0, fd) == 0) {
			ide = 1;
			ide_reset_begin(ide0);
		}
	}

	sio_reset();
	ctc_init();

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

	while (!done) {
		int l;
		for (l = 0; l < 10; l++) {
			int i;
			/* 36400 T states */
			for (i = 0; i < 100; i++) {
				Z80ExecuteTStates(&cpu_z80, 364);
				sio2_timer();
				ctc_tick(364);
			}
			/* Do 5ms of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
			if (int_recalc) {
				/* If there is no pending Z80 vector IRQ but we think
				   there now might be one we use the same logic as for
				   reti */
				poll_irq_event();
				/* Clear this after because reti_event may set the
				   flags to indicate there is more happening. We will
				   pick up the next state changes on the reti if so */
				if (!(cpu_z80.IFF1|cpu_z80.IFF2))
					int_recalc = 0;
			}
		}
	}
	exit(0);
}
