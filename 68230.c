#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "68230.h"

/*
 *	Motorola 68230 P/IT
 *
 *	Fairly minimal model for now. We just emulate mode 0 1x - bit I/O without
 *	extra buffering.
 *
 *	TODO: interrupts on timer.
 */


struct m68230 {
	uint8_t reg[32];
	uint8_t pre;
	unsigned trace;
};

#define PGCR	0
#define PSRR	1
#define PADDR	2
#define PBDDR	3
#define PCDDR	4
#define PIVR	5
#define PACR	6
#define PBCR	7
#define PADR	8
#define PBDR	9
#define PAAR	10
#define PBAR	11
#define PCDR	12
#define PSR	13

#define TCR	16
#define TIVR	17

#define CPR(x)	(19 + (x))
#define CNTR(x)	(23 + (x))
#define TSR	26

void m68230_write(struct m68230 *pit, unsigned addr, uint8_t val)
{
	addr &= 0x1F;

	if (pit->trace)
		fprintf(stderr, "pit: W %02X <- %02X\n", addr, val);
	/* CNTR is not writeable, and we keep the working value in here (or will do) */
	if (addr >= CNTR(0) && addr <= CNTR(3))
		return;

	if (addr == PGCR) {
		if ((val & 0xC0) != 0)
			fprintf(stderr, "Only mode 0 emulated\n");
	}
	if (addr == PACR) {
		if ((val & 0xC0) != 0x40)
			fprintf(stderr, "Only submode 01 is emulated.\n");
	}
	if (addr == PBCR) {
		if ((val & 0xC0) != 0x40)
			fprintf(stderr, "Only submode 01 is emulated.\n");
	}
	/* Tell the emulation but also store in the registers */
	/* TODO: should we pass the masks and report mask changes ? */
	if (addr == PADR || addr == PBDR)
		m68230_write_port(pit, addr - PADR, val);
	if (addr == PCDR)
		m68230_write_port(pit, addr - PCDR, val);
	/* TCR bit 3 is fixed at 0 */
	if (addr == TCR)
		val &= 0xF7;
	/* Any write to the TSR resets it */
	if (addr == TSR)
		val = 0;
	pit->reg[addr] = val;
}

uint8_t m68230_read(struct m68230 *pit, unsigned addr)
{
	addr &= 0x1F;
	/* Sample the I/O lines. We just emulate mode 0 sub 1 for now */
	if (addr == PADR || addr == PBDR) {
		uint8_t r = m68230_read_port(pit, addr - PADR);
		uint8_t d = pit->reg[addr - PADR + PADDR];
		r &= ~d;
		r |= pit->reg[addr] & d;
		return r;
	}
	if (addr == PCDR) {
		uint8_t r = m68230_read_port(pit, 2);
		uint8_t d = pit->reg[PCDDR];
		r &= ~d;
		r |= pit->reg[addr] & d;
		return r;
	}

	/* Locked as 0 so a 32bit MOV works for the 24 bit value */
	if (addr == CNTR(-1) || addr == CPR(-1))
		return 0;

	/* For mode 0 sub 1 this is no different to DATA, for the others
	   it doesn't trigger any additional mode related magic */
	if (addr == PAAR || addr == PBAR) {
		uint8_t r = m68230_read_port(pit, addr - PAAR);
		uint8_t d = pit->reg[addr - PAAR + PADDR];
		r &= ~d;
		r |= pit->reg[addr] & d;
		return r;
	}
	return pit->reg[addr];
}

static void m68230_tick_once(struct m68230 *pit)
{
	/* We assume this will be called a lot for small values or we could optimize a lot */
	if (pit->reg[CNTR(2)] == 0x00) {
		if(pit->reg[CNTR(1)] == 0x00) {
			if (pit->reg[CNTR(0)] == 0x00) {
				pit->reg[TSR] = 1;
				pit->reg[CNTR(0)] = pit->reg[CPR(0)];
				pit->reg[CNTR(1)] = pit->reg[CPR(1)];
				pit->reg[CNTR(2)] = pit->reg[CPR(2)];
				return;
			}
			pit->reg[CNTR(0)]--;
		}
		pit->reg[CNTR(1)]--;
	}
	pit->reg[CNTR(2)]--;
}

void m68230_tick(struct m68230 *pit, unsigned cycles)
{
	if ((pit->reg[TCR] & 1) == 0)
		return;
	switch(pit->reg[TCR & 0x06]) {
	case 0:
		/* CLK and prescaler are used */
		cycles += pit->pre;
		pit->pre = cycles & 0x1F;
		cycles >>= 5;
		while(cycles) {
			pit->pre++;
			if (pit->pre == 0x20) {
				pit->pre = 0;
				m68230_tick_once(pit);
			}
		}
		return;
	case 2:
		/* CLK and prescalar are used, PC2/TIN determines on off state - not modelled */
		return;
	case 4:
		/* PC2/TIN is timer and prescaled, not modelled */
		return;
	case 6:
		/* PC2/TIN is timer - not modelled */
		return;
	}
}

void m68230_reset(struct m68230 *pit)
{
	pit->reg[PIVR] = 0x0F;
	pit->reg[TIVR] = 0x0F;
}

struct m68230 *m68230_create(void)
{
	struct m68230 *pit = malloc(sizeof(struct m68230));
	if (pit == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(pit, 0, sizeof(*pit));
	m68230_reset(pit);
	return pit;
}

void m68230_free(struct m68230 *m)
{
	free(m);
}

void m68230_trace(struct m68230 *pit, unsigned t)
{
	pit->trace = !!t;
}

uint8_t m68230_port_irq_pending(struct m68230 *pit)
{
	return 0;	/* For now */
}

uint8_t m68230_port_vector(struct m68230 *pit)
{
	return 0;
}

uint8_t m68230_timer_irq_pending(struct m68230 *pit)
{
	return pit->reg[TSR];
}

uint8_t m68230_timer_vector(struct m68230 *pit)
{
	return pit->reg[TIVR];
}
