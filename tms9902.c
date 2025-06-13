/*
 *	A fairly standard UART but on a bit addressed bus
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "system.h"

#include "serialdevice.h"
#include "tms9902.h"

struct tms9902 {
	uint32_t cru_wr;
#define RESET		31
#define DSCENB		21
#define TIMENB		20
#define XBIENB		19
#define RIENB		18
#define BRKON		17
#define RTSON		16
#define TSTMD		15
#define LDCTRL		14
#define LDIR		13
#define LRDR		12
#define LXDR		11

	uint32_t cru_rd;
#define INT		31
#define FLAG		30
#define DSCH		29
#define CTS		28
#define DSR		27
#define RTS		26
#define TIMELP		25
#define TIMERR		24
#define XSRE		23
#define XBRE		22
#define RBRL		21
#define DSCINT		20
#define TIMINT		19
/* Unused: 0 */
#define XBINT		17
#define RBINT		16
#define RIN		15
#define RSBD		14
#define RFBD		13
#define RFER		12
#define ROVER		11
#define RPER		10
#define RCVERR		9
/* Unused: 0 */
/* Then the char */

	uint16_t ctrl;
	uint16_t interval;
	uint16_t rdrr;
	uint16_t tdrr;
	uint16_t tdr;

	unsigned trace;

	struct serial_device *dev;
};


/*
 *	Update all the flags and receive anything pending
 */
static void tms9902_recalc(struct tms9902 *tms)
{
	int n = tms->dev->ready(tms->dev);
	if (n & 1) {
		if (!(tms->cru_rd & (1 << RBRL))) {
			tms->cru_rd |= (1 << RBRL);
			tms->cru_rd &= ~0xFF;
			tms->cru_rd |= tms->dev->get(tms->dev);
		}
	}
	if (n & 2) {
		tms->cru_rd |= (1 << XBRE);
	}
	tms->cru_rd &=
		~((1 << FLAG) | (1 << INT) | (1 << DSCINT) | (1 << XBINT) |
		  (1 << RBINT));

	if ((tms->cru_rd & (1 << DSCH)) && (tms->cru_wr & (1 << DSCENB)))
		tms->cru_rd |= (1 << DSCINT);
	if ((tms->cru_rd & (1 << XBRE)) && (tms->cru_wr & (1 << XBIENB)))
		tms->cru_rd |= (1 << XBINT);
	if ((tms->cru_rd & (1 << RBRL)) && (tms->cru_wr & (1 << RIENB)))
		tms->cru_rd |= (1 << RBINT);
	if ((tms->cru_rd & (1 << TIMELP)) && (tms->cru_wr & (1 << TIMENB)))
		tms->cru_rd |= (1 << TIMINT);
	if (tms->
	    cru_wr & ((1 << LDCTRL) | (1 << LDIR) | (1 << LRDR) |
		      (1 << LXDR) | (1 << BRKON)))
		tms->cru_rd |= (1 << FLAG);
	if (tms->
	    cru_rd & ((1 << DSCINT) | (1 << TIMINT) | (1 << XBINT) |
		      (1 << RBINT)))
		tms->cru_rd |= (1 << INT);
}

void tms9902_reset(struct tms9902 *tms)
{
	/* Doesn't matter which value is written */
	tms->cru_wr = LDCTRL | LDIR | LRDR | LXDR;
	tms->cru_rd = 0;
}

void tms9902_cru_write(struct tms9902 *tms, uint16_t addr, uint8_t bit)
{
	uint16_t *r;

	tms->cru_wr &= ~(1 << addr);
	tms->cru_wr |= (bit << addr);

	if (addr == RESET) {
		tms9902_reset(tms);
	}
	if (addr == RIENB)
		tms->cru_rd &= ~(1 << RBRL);
	if (addr <= LXDR) {
		/* Writing a register */
		if (tms->cru_wr & (1 << LDCTRL)) {
			r = &tms->ctrl;
			if (addr == 7)
				tms->cru_wr &= (1 << LDCTRL);
		} else if (tms->cru_wr & (1 << LDIR)) {
			if (addr == 7)
				tms->cru_wr &= (1 << LDIR);
			r = &tms->interval;
		} else if (tms->cru_wr & (1 << LRDR)) {
			if (addr == 10)
				tms->cru_wr &= (1 << LRDR);
			r = &tms->rdrr;
		} else if (tms->cru_wr & (1 << LXDR)) {
			if (addr == 10)
				tms->cru_wr &= (1 << LXDR);
			r = &tms->tdrr;
		} else {
			r = &tms->tdr;
			if (addr == 7) {
				uint8_t c = tms->tdr;
				/* Not strictly correct - should check BRK etc and delay */
				tms->dev->put(tms->dev, c);
				tms->cru_rd &= ~XBRE;
			}
		}
		*r &= ~(1 << addr);
		*r |= (bit << addr);
		/* Special case - writing RDR when XDR also set */
		if (r == &tms->rdrr && tms->cru_wr & (1 << LXDR)) {
			tms->tdrr &= ~(1 << addr);
			tms->tdrr |= (bit << addr);
		}
	}
	tms9902_recalc(tms);
}

uint8_t tms9902_cru_read(struct tms9902 *tms, uint16_t addr)
{
	uint8_t bit = tms->cru_rd & (1 << addr);
	return bit;
}

void tms9902_event(struct tms9902 *tms)
{
	tms9902_recalc(tms);
}

void tms9902_attach(struct tms9902 *tms, struct serial_device *dev)
{
	tms->dev = dev;
}

void tms9902_trace(struct tms9902 *tms, int onoff)
{
	tms->trace = onoff;
}

uint8_t tms9902_irq_pending(struct tms9902 *tms)
{
	return !!(tms->cru_rd & INT);
}

struct tms9902 *tms9902_create(void)
{
	struct tms9902 *tms = malloc(sizeof(struct tms9902));
	if (tms == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(tms, 0, sizeof(*tms));
	tms9902_reset(tms);
	return tms;
}

void tms9902_free(struct tms9902 *tms)
{
	free(tms);
}
