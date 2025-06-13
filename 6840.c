#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "6840.h"

/*
 *	Motorola 6840 PTM
 */

struct ptm_timer {
	uint16_t timer;
	uint16_t wlatch;
	uint8_t ctrl;
	int output;
	int event;
};

struct m6840 {
	/* These are 1 based so we don't use entry 0. This makes the code
	   easier to follow as the numbering matches the data sheet */
	struct ptm_timer timer[4];
	uint8_t sr;
	uint8_t msb;
	uint8_t lsb;
	uint8_t prescale;

	unsigned int trace;
	unsigned int lastout;
};

static void m6840_calc_irq(struct m6840 *ptm)
{
	int irq = 0;
	/* Turn our internal events for overflow into status bits */
	if (ptm->timer[1].event) {
		ptm->timer[1].event = 0;
		ptm->sr |= 1;
	}
	if (ptm->timer[2].event) {
		ptm->timer[2].event = 0;
		ptm->sr |= 2;
	}
	if (ptm->timer[3].event) {
		ptm->timer[3].event = 0;
		ptm->sr |= 4;
	}
	/* Check status versus masks and set the SR IRQ bit accordingly */
	if ((ptm->sr & 1) && (ptm->timer[1].ctrl & 0x40))
		irq = 0x80;
	if ((ptm->sr & 2) && (ptm->timer[2].ctrl & 0x40))
		irq = 0x80;
	if ((ptm->sr & 4) && (ptm->timer[3].ctrl & 0x40))
		irq = 0x80;
	ptm->sr &= 0x7F;
	ptm->sr |= irq;
}

int m6840_irq_pending(struct m6840 *ptm)
{
	return ptm->sr & 0x80;
}

/*
 *	Model the O1/O2/O3 pins
 */
static void m6840_calc_outputs(struct m6840 *ptm)
{
	unsigned int output = 0;
	int i;
	struct ptm_timer *p = &ptm->timer[1];
	for (i = 0; i <= 2; i++) {
		if (p->output && (p->ctrl & 0x80))
			output |= (1 << i);
		p++;
	}
	if (output != ptm->lastout) {
		ptm->lastout = output;
		m6840_output_change(ptm, output);
	}
}

/* Count a timer in 16 or 8x8 bit mode */
static void m6840_timer_count(struct ptm_timer *p, int restart)
{
	if (!(p->ctrl & 0x04)) {
		/* The check occurs before the count down */
		if (p->timer == 0) {
			p->event = 1;
			p->output ^= 1;
			if (p->event && restart)
				p->timer = p->wlatch;
		}
		p->timer--;
	} else {
		if ((p->timer & 0xFF) != 0)
			p->timer--;
		else {
			p->timer &= 0xFF00;
			if (p->timer) {
				p->timer -= 0x0100;
				p->timer |= p->wlatch & 0xFF;
			} else {
				p->event = 1;
				if (restart)
					p->timer = p->wlatch;
			}
		}
		if ((p->timer & 0xFF00) == 0)
			p->output = 1;
		else
			p->output = 0;
	}
	if (p->event && restart)
		p->timer = p->wlatch;
}

/*
 *	Handle a timer being clocked by something
 */
static void m6840_timer_clock(struct ptm_timer *p)
{
	switch((p->ctrl >> 3) & 7) {
		case 0:	/* Continuous */
			m6840_timer_count(p, 1);
			break;
		case 1:	/* Frequency compare (not supported yet) */
			break;
		case 2:	/* Continuous - not reset by write to latches */
			m6840_timer_count(p, 1);
			break;
		case 3:	/* Pulse width compare (not supported yet) */
			break;
		case 4:	/* One shot, reset by write to latches */
			m6840_timer_count(p, 0);
			break;
		case 5:	/* Frequency comparison (not supported yet) */
			break;
		case 6:	/* One shot, reset by gate/reset only */
			m6840_timer_count(p, 0);
		case 7:	/* Pulse width compare (not supported yet) */
			break;
	}
}

/* Perform an event tick on a timer */
static void m6840_event_tick(struct ptm_timer *p)
{
	/* Internal clock ? */
	if (p->ctrl & 2)
		return;
	m6840_timer_clock(p);
}

/* Perform internal tick on a timer */
static void m6840_timer_tick(struct ptm_timer *p)
{
	/* External clock */
	if (!(p->ctrl & 2))
		return;
	m6840_timer_clock(p);
}

/* Runs for every E clock */
void m6840_tick(struct m6840 *ptm, int tstates)
{
	while(tstates--) {
		m6840_timer_tick(&ptm->timer[1]);
		m6840_timer_tick(&ptm->timer[2]);
		m6840_timer_tick(&ptm->timer[3]);
	}
	m6840_calc_irq(ptm);
	m6840_calc_outputs(ptm);
}

/* External clock event */
void m6840_external_clock(struct m6840 *ptm, int timer)
{
	/* Timer 3 has an external pre-scaler option */
	if (timer == 3 && (ptm->timer[3].ctrl & 0x01)) {
		ptm->prescale++;
		ptm->prescale &= 7;
		if (ptm->prescale)
			return;
	}
	m6840_event_tick(&ptm->timer[timer]);
	m6840_calc_irq(ptm);
	m6840_calc_outputs(ptm);
}

/* High to low transition on gate */
void m6840_external_gate(struct m6840 *ptm, int gate)
{
	if (ptm->timer[gate].ctrl & 8) {
		ptm->timer[gate].timer = ptm->timer[gate].wlatch;
		/* IRQ clear ? */
	}
}

static void m6840_soft_reset(struct m6840 *ptm)
{
	ptm->timer[0].timer = ptm->timer[0].wlatch;
	ptm->timer[1].timer = ptm->timer[0].wlatch;
	ptm->timer[2].timer = ptm->timer[0].wlatch;
	m6840_calc_irq(ptm);
	if (ptm->trace)
		fprintf(stderr, "[PTM] Reset.\n");
}

void m6840_reset(struct m6840 *ptm)
{
	ptm->timer[0].wlatch = 0xFFFF;
	ptm->timer[1].wlatch = 0xFFFF;
	ptm->timer[2].wlatch = 0xFFFF;
	m6840_soft_reset(ptm);
	ptm->lastout = 0x100;		/* Impossible value to force update */
}

uint8_t m6840_read(struct m6840 *ptm, uint8_t addr)
{
	struct ptm_timer *p;
	addr &= 7;
	if (addr == 0)
		return 0xFF;		/* Probably tri-stated */
	if (addr == 1) {
		if (ptm->trace)
			fprintf(stderr, "[PTM]: Read status register %02X\n", ptm->sr);
		return ptm->sr;
	}
	if (addr & 1)
		return ptm->lsb;
	addr >>= 1;
	p = &ptm->timer[addr];
	ptm->lsb = p->timer;
	ptm->sr &= ~(1 << (addr - 1));	/* And clear the interrupt */
	m6840_calc_irq(ptm);
	if (ptm->trace)
		fprintf(stderr, "[PTM] Read timer %d IRQ now %02X\n", addr, ptm->sr);
	return p->timer >> 8;
}

void m6840_write(struct m6840 *ptm, uint8_t addr, uint8_t val)
{
	struct ptm_timer *p;
	addr &= 7;
	if (addr > 1) {
		if ((addr & 1) == 0)
			ptm->msb = val;
		else {
			addr >>= 1;
			p = &ptm->timer[addr];
			p->wlatch = (ptm->msb << 8) | val;
			/* Writing the timer also clears the interrupt if CR3/4 are 0 */
			if (ptm->trace)
				fprintf(stderr, "[PTM] Timer %d set to %d\n", addr, p->wlatch);
			if ((p->ctrl & 0x18) == 0x00) {
				p->timer = p->wlatch;
				p->output = 0;
				ptm->sr &= ~(1 << addr);
				m6840_calc_irq(ptm);
				m6840_calc_outputs(ptm);
			}
		}
		return;
	}
	if (addr == 0) {
		if (ptm->timer[2].ctrl & 0x1)
			addr = 1;
		else
			addr = 3;
	} else
		addr = 2;
	if (ptm->trace)
		fprintf(stderr, "[PTM] Control %d set to %02X\n", addr, val);
	ptm->timer[addr].ctrl = val;
	/* Effects of control changes */
	if (addr == 1 && (val & 1))
		m6840_soft_reset(ptm);
	m6840_calc_irq(ptm);
}

void m6840_trace(struct m6840 *ptm, int onoff)
{
	ptm->trace = onoff;
}

struct m6840 *m6840_create(void)
{
	struct m6840 *ptm = malloc(sizeof(struct m6840));
	memset(ptm, 0x00, sizeof(struct m6840));
	m6840_reset(ptm);
	return ptm;
}

void m6840_free(struct m6840 *ptm)
{
	free(ptm);
}
