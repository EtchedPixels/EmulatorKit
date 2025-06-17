#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "6821.h"

struct m6821 {
	uint8_t pra;
	uint8_t prb;
	uint8_t ddra;
	uint8_t ddrb;
	uint8_t cra;
	uint8_t crb;

	uint8_t ctrl;
	uint8_t ctrl_last;

	uint8_t irq;

	int trace;
};

/*
 *	Basic 6821 emulation. Don't really have amything to plug it into
 *	to test it yet but will need it for the 6840/6821 card for the 6809
 *	system.
 */

static void m6821_recalc_irq(struct m6821 *pia)
{
	pia->irq = 0;
	if (pia->cra & 0xC0)
		pia->irq = M6821_IRQA;
	if (pia->crb & 0xC0)
		pia->irq |= M6821_IRQB;
}

/*
 *	Handle control changes. We do the forcewd on/off cases here but
 *	the strobes and the like elsewhere.
 */
void m6821_calc_cr(struct m6821 *pia)
{
	switch((pia->cra >> 3) & 7) {
	case 0:	/* IRQ control */
	case 1:
	case 2:
	case 3:
		break;
	case 4:	/* strobes */
	case 5:
		break;
	case 6:	/* force state */
		pia->ctrl &= ~M6821_CA2;
		break;
	case 7:
		pia->ctrl |= M6821_CA2;
		break;
	}
	switch((pia->crb >> 3) & 7) {
	case 0:	/* IRQ control */
	case 1:
	case 2:
	case 3:
		break;
	case 4:	/* strobes */
	case 5:
		break;
	case 6:	/* force state */
		pia->ctrl &= ~M6821_CB2;
		break;
	case 7:
		pia->ctrl |= M6821_CB2;
		break;
	}

	pia->ctrl |= pia->ctrl_last;
	if (pia->ctrl) {
		m6821_ctrl_change(pia, pia->ctrl);
		pia->ctrl_last = pia->ctrl;
	}
}

/*
 *	Read from the MC6821. The address decoding is a bit convoluted
 */
uint8_t m6821_read(struct m6821 *pia, uint8_t addr)
{
	addr &= 3;
	switch(addr) {
	case 0:
		if (pia->cra & 0x04) {
			uint8_t r = pia->pra & pia->ddra;
			r |= ~pia->ddra & m6821_input(pia, 0);
			if ((pia->cra & 0x30) == 0x20) {
				/* Read strobe and read low until ack */
				if (pia->crb & 0x08)
					m6821_strobe(pia, M6821_CA2);
				else {
					if ((pia->ctrl & M6821_CA2)) {
						pia->ctrl &= ~M6821_CA2;
						m6821_calc_cr(pia);
					}
				}
			}
			pia->cra &= ~0xC0;
			m6821_recalc_irq(pia);
			return r;
		}
		return pia->ddra;
	case 1:
		return pia->cra;
	case 2:
		if (pia->crb & 4) {
			uint8_t r = pia->prb & pia->ddrb;
			r |= ~pia->ddrb & m6821_input(pia, 1);
			pia->crb &= ~0xC0;
			m6821_recalc_irq(pia);
			return r;
		}
		return pia->ddrb;
	case 3:
		return pia->crb;
	}
	/* Not reachable - fix gcc warning */
	return 0xFF;
}

/*
 *	Write to the M6821. Same decoding. Port B has write features
 *	port A read features.
 */
void m6821_write(struct m6821 *pia, uint8_t addr, uint8_t val)
{
	addr &= 3;
	switch(addr) {
	case 0:
		if (pia->cra & 0x04) {
			pia->pra = val;
			m6821_output(pia, 0, val & pia->ddra);
		} else {
			pia->ddra = val;
			m6821_output(pia, 0, pia->pra & pia->ddra);
		}
		break;
	case 1:
		pia->cra = val;
		break;
	case 2:
		if (pia->crb & 4) {
			pia->prb = val;
			m6821_output(pia, 1, val & pia->ddrb);
			if ((pia->crb & 0x30) == 0x20) {
				/* Write strobe and write low until ack */
				if (pia->crb & 0x08)
					m6821_strobe(pia, M6821_CB2);
				else {
					if ((pia->ctrl & M6821_CB2)) {
						pia->ctrl &= ~M6821_CB2;
						m6821_calc_cr(pia);
					}
				}
			}
		} else {
			pia->ddrb = val;
			m6821_output(pia, 1, pia->prb & pia->ddrb);
		}
		break;
	case 3:
		pia->crb = val;
		break;
	}
	m6821_calc_cr(pia);
}

/*
 *	An external control pin change. Probably the most complicated part
 *	of the entire thing.
 */
void m6821_set_control(struct m6821 *pia, int cline, int onoff)
{
	if (!!(pia->ctrl & cline) == !!onoff)
		return;
	switch(cline) {
	case M6821_CA1:
		/* CA1 as interrupt */
		if (pia->cra & 1) {
			if (onoff && (pia->cra & 2))
				pia->cra |= 0x80;
			if (!onoff && !(pia->cra & 2))
				pia->cra |= 0x80;
		}
		/* CA1 configured as read ack */
		if ((pia->crb & 0x38) == 0x28) {
			if (onoff && (pia->crb & 2)) {
				pia->ctrl |= M6821_CB2;
				m6821_calc_cr(pia);
			}
			if (!onoff && !(pia->crb & 2)) {
				pia->ctrl |= M6821_CB2;
				m6821_calc_cr(pia);
			}
			return;
		}
	case M6821_CB1:
		/* CB1 as interrupt */
		if (pia->cra & 2) {
			if (onoff && (pia->crb & 2))
				pia->crb |= 0x80;
			if (!onoff && !(pia->crb & 2))
				pia->crb |= 0x80;
		}
		/* CB1 configured as write ack */
		if ((pia->crb & 0x38) == 0x28) {
			if (onoff && (pia->crb & 2)) {
				pia->ctrl |= M6821_CB2;
				m6821_calc_cr(pia);
			}
			if (!onoff && !(pia->crb & 2)) {
				pia->ctrl |= M6821_CB2;
				m6821_calc_cr(pia);
			}
		}
		return;
	case M6821_CA2:
		if (pia->cra & 0x20) {
			/* Not an input */
			return;
		} else {
			/* CA2 as interrupt input */
			switch (((pia->cra & 0x38) >> 3) & 3) {
			case 0:	/* disabled irq generation */
			case 1:
				return;
			case 2: /* high to low */
				if (onoff == 0)
					pia->cra |= 0x40;
				m6821_recalc_irq(pia);
				return;
			case 3: /* low to high */
				if (onoff == 1)
					pia->cra |= 0x40;
				m6821_recalc_irq(pia);
				return;
			}
		}
		break;
	case M6821_CB2:
		if (pia->crb & 0x20) {
			/* Not an input */
			return;
		} else {
			/* CB2 as interrupt input */
			switch (((pia->crb & 0x38) >> 3) & 3) {
			case 0:	/* disabled irq generation */
			case 1:
				return;
			case 2: /* high to low */
				if (onoff == 0)
					pia->crb |= 0x40;
				m6821_recalc_irq(pia);
				return;
			case 3: /* low to high */
				if (onoff == 1)
					pia->crb |= 0x40;
				m6821_recalc_irq(pia);
				return;
			}
		}
		break;
	}
}

void m6821_reset(struct m6821 *pia)
{
	memset(pia, 0, sizeof(struct m6821));
	/* CA1/CA2 will be on if we ever model them */
}

struct m6821 *m6821_create(void)
{
	struct m6821 *pia = malloc(sizeof(struct m6821));
	if (pia == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	m6821_reset(pia);
	return pia;
}

void m6821_free(struct m6821 *pia)
{
	free(pia);
}

int m6821_irq_pending(struct m6821 *pia)
{
	return pia->irq;
}

void m6821_trace(struct m6821 *pia, int onoff)
{
	pia->trace = onoff;
}
