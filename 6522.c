#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "6522.h"

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

	unsigned int trace;
};

static void via_recalc_irq(struct via6522 *via)
{
	int irq = (via->ier & via->ifr) & 0x7F;
	if (irq)
		via->ifr |= 0x80;
	else
		via->ifr &= 0x7F;
	/* We interrupt if ier and ifr are set */
	/* Note: the pin is inverted but we model irq state not the pin! */
	if ((via->trace) && irq != via->irq)
		fprintf(stderr, "[VIA IRQ now %02X.]\n", irq);
	via->irq = irq;
}

static void via_recalc_all(struct via6522 *via)
{
	via_recalc_outputs(via);
	via_recalc_irq(via);
}

/* Perform time related processing for the VIA */
void via_tick(struct via6522 *via, unsigned int clocks)
{
	/* This isn't quite right but it's near enough for the moment */
	if (via->t1) {
		if (clocks >= via->t1) {
			if (via->trace)
				fprintf(stderr,"[VIA T1 expire.].\n");
			via->ifr |= 0x40;
			via_recalc_irq(via);
			/* +1 or + 2 ?? */
			if (via->acr & 0x40)
				via->t1 = via->t1l + 1;
			else
				via->t1 = 0;
		}
		else
			via->t1 -= clocks;
	}

	if (via->t2 && !(via->acr & 0x20)) {
		if (clocks >= via->t2) {
			via->ifr |= 0x20;
			via_recalc_irq(via);
			via->t2 = 0;
			if (via->trace)
				fprintf(stderr,"[VIA T2 expire.].\n");
		}
		via->t2 -= clocks;
	}
}

uint8_t via_read(struct via6522 *via, uint8_t addr)
{
	uint8_t r;
	if (via->trace)
		fprintf(stderr, "[VIA read %d: ", addr);
	switch(addr) {
		case 0:
			r = via->irb & ~via->ddrb;
			r |= via->orb & via->ddrb;
			via_handshake_b(via);
			break;
		case 1:
			r = via->ira & ~via->ddra;
			r |= via->ora & via->ddra;
			via_handshake_a(via);
			break;
		case 2:
			r = via->ddrb;
			break;
		case 3:
			r = via->ddra;
			break;
		case 4:
			via->ifr &= ~0x40;	/* T1 timeout */
			via_recalc_irq(via);
			r = via->t1;
			break;
		case 5:
			r = via->t1 >> 8;
			break;
		case 6:
			r = via->t1l;
			break;
		case 7:
			r = via->t1l >> 8;
			break;
		case 8:
			via->ifr &= ~0x20;	/* T2 timeout */
			via_recalc_irq(via);
			r = via->t2;
			break;
		case 9:
			r = via->t2 >> 8;
			break;
		case 10:
			r = via->sr;
			break;
		case 11:
			r = via->acr;
			break;
		case 12:
			r = via->pcr;
			break;
		case 13:
			r = via->ifr;
			break;
		case 14:
			r = via->ier;
			break;
		default:
		case 15:
			r =  via->ira;
			break;
	}
	if (via->trace)
		fprintf(stderr, "%02X.]\n", r);
	return r;
}

void via_write(struct via6522 *via, uint8_t addr, uint8_t val)
{
	if (via->trace)
		fprintf(stderr, "[VIA write %d: %02X.]\n", addr, val);
	switch(addr) {
		case 0:
			via->orb = val;
			via_recalc_outputs(via);
			via_handshake_b(via);
			break;
		case 1:
			via->ora = val;
			via_recalc_outputs(via);
			break;
		case 2:
			via->ddrb = val;
			via_recalc_all(via);
			break;
		case 3:
			via->ddra = val;
			via_recalc_all(via);
			break;
		case 4:
		case 6:
			via->t1l &= 0xFF00;
			via->t1l |= val;
			break;
		case 5:
			via->t1l &= 0xFF;
			via->t1l |= val << 8;
			via->t1 = via->t1l;
			via->ifr &= ~0x40;	/* T1 timeout */
			via_recalc_irq(via);
			if (via->trace)
				fprintf(stderr, "[VIA T1 begin %04X.]\n", via->t1);
			break;
		case 7:
			via->t1l &= 0xFF;
			via->t1l |= val << 8;
			break;
		case 8:
			via->t2l = val;
			break;
		case 9:
			via->t2 = val << 8;
			via->t2 |= via->t2l;
			via->ifr &= ~0x20;	/* T2 timeout */
			via_recalc_irq(via);
			break;
		case 10:
			via->sr = val;
			break;
		case 11:
			via->acr = val;
			break;
		case 12:
			via->pcr = val;
			break;
		case 13:
			via->ifr &= ~val;
			if (via->ifr & 0x7F)
				via->ifr |= 0x80;
			via_recalc_irq(via);
			break;
		case 14:
			if (val & 0x80)
				via->ier |= val;
			else
				via->ier &= ~val;
			via->ier &= 0x7F;
			via_recalc_irq(via);
			break;
		case 15:
			via->ora = val;
			break;
	}
}

int via_irq_pending(struct via6522 *via)
{
	return via->irq;
}

void via_trace(struct via6522 *via, int onoff)
{
	via->trace = onoff;
}

void via_free(struct via6522 *via)
{
	free(via);
}

struct via6522 *via_create(void)
{
	struct via6522 *v = malloc(sizeof(struct via6522));
	if (v == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(v, 0, sizeof(struct via6522));
	return v;
}

uint8_t via_get_direction_a(struct via6522 *via)
{
	return via->ddra;
}

uint8_t via_get_port_a(struct via6522 *via)
{
	return via->ora & via->ddra;
}

void via_set_port_a(struct via6522 *via, uint8_t val)
{
	via->ira = val;
}

uint8_t via_get_direction_b(struct via6522 *via)
{
	return via->ddrb;
}

uint8_t via_get_port_b(struct via6522 *via)
{
	return via->orb & via->ddrb;
}

void via_set_port_b(struct via6522 *via, uint8_t val)
{
	via->irb = val;
}
