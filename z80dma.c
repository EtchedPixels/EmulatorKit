#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "z80dma.h"


#define NREG		28

struct z80dma {
	uint8_t reg[NREG];
	uint8_t rregmask;
	uint32_t wregmask;
	uint8_t rrcount;
	uint8_t wrcount;
	uint8_t forcerdy;
	uint8_t intpend;
	uint8_t intservice;
	uint8_t enabled;
	uint8_t trace;
	uint8_t idle;
};

#define	RR0		0
#define RR1		1
#define RR2		2
#define RR3		3
#define RR4		4
#define RR5		5
#define RR6		6

#define WR0		7
#define WR1		8
#define WR2		9
#define WR3		10
#define WR4		11
#define WR5		12
#define WR6		13

#define WR_A_L		14
#define WR_A_H		15
#define WR_LEN_L	16
#define WR_LEN_H	17

#define WR_TIMING_A	18
#define WR_TIMING_B	19

#define WR_MASK		20
#define WR_MATCH	21

#define WR_B_L		22
#define WR_B_H		23

#define WR_INTCTL	24
#define WR_PULSE	25
#define WR_INTVEC	26

#define WR_RRMASK	27

#define R_VECTOR	(1 << WR_INTVEC)
#define R_PULSE		(1 << WR_PULSE)
#define R_TIMING_A	(1 << WR_TIMING_A)
#define R_TIMING_B	(1 << WR_TIMING_B)
#define R_LEN_H		(1 << WR_LEN_H)
#define R_LEN_L		(1 << WR_LEN_L)
#define R_ADDR_A_H	(1 << WR_A_H)
#define R_ADDR_A_L	(1 << WR_A_L)
#define R_ADDR_MATCH	(1 << WR_MATCH)
#define R_ADDR_MASK	(1 << WR_MASK)
#define R_INTCTL	(1 << WR_INTCTL)
#define R_ADDR_B_H	(1 << WR_B_H)
#define R_ADDR_B_L	(1 << WR_B_L)
#define R_RRMASK	(1 << WR_RRMASK)

static uint8_t z80dma_rr(struct z80dma *dma)
{
	while(!(dma->rregmask & (1 << dma->rrcount)))
		dma->rrcount++;
	dma->rregmask &= ~(1 << dma->rrcount);
	return dma->reg[RR0 + dma->rrcount++];
}

static void z80dma_wr(struct z80dma *dma, uint8_t val)
{
	while(!(dma->wregmask & (1 << dma->wrcount)))
		dma->wrcount++;
	dma->wregmask &= ~(1 << dma->wrcount);
	/* Special case interrupt mask triggers other write regs */
	if (dma->wrcount == WR_INTCTL) {
		if (val & 0x10)
			dma->wregmask |= R_VECTOR;
		if (val & 0x08)
			dma->wregmask |= R_PULSE;
	}
	dma->reg[dma->wrcount++] = val;
}

void z80dma_reset(struct z80dma *dma)
{
	memset(dma, 0, sizeof(struct z80dma));
	/* TODO */
}

static void z80dma_command(struct z80dma *dma)
{
	switch(dma->reg[WR6]) {
	case 0xC3:
		z80dma_reset(dma);
		break;
	case 0xC7:
		/* Sets WR1 back to Z80 timing */
		dma->reg[WR_TIMING_A] = 2;
		break;
	case 0xCB:
		dma->reg[WR_TIMING_B] = 2;
		break;
	case 0xCF:
		dma->reg[RR0] = 0;
		dma->reg[RR1] = 0;
		dma->reg[RR2] = 0;
		dma->reg[RR3] = dma->reg[WR_A_L];
		dma->reg[RR4] = dma->reg[WR_A_H];
		/* B is weird see data book */
		/* Inactive rule todo */
		dma->forcerdy = 0;
		break;
	case 0xD3:
		/* Inactive rule todo */
		dma->reg[RR1] = 0;
		dma->reg[RR2] = 0;
		break;
	case 0xAF:
		dma->reg[WR3] &= ~(1 << 5);
		break;
	case 0xAB:
		dma->reg[WR3] |= (1 << 5);
		break;
	case 0xA3:
		dma->forcerdy = 0;
		dma->intpend = 0;
		dma->intservice = 0;
		dma->reg[WR3] |= (1 << 5);
	case 0xB7:
		dma->enabled = 2;		/* Enable after reti */
		break;
	case 0xBF:
		dma->rregmask = RR0;
		break;
	case 0x8B:
		/* Reinit status: TODO */
		/* DMA must be disabled first */
		break;
	case 0xBB:
		dma->wregmask = R_RRMASK;
		break;
	case 0xA7:
		dma->rrcount = 0;
		dma->rregmask = dma->reg[WR_RRMASK];
		break;
	case 0xB3:
		dma->forcerdy = 1;
		break;
	case 0x87:
		/* Review RETI rule */
		dma->enabled = 1;
		break;
	case 0x83:
		dma->enabled = 0;
		break;
	
	}
}
	
uint8_t z80dma_read(struct z80dma *dma)
{
	if (dma->rregmask)
		return z80dma_rr(dma);
	/* ?? */
	return 0xFF;
}

void z80dma_write(struct z80dma *dma, uint8_t val)
{
	/* If we are mid operation write the next expected reg */
	if (dma->wregmask) {
		z80dma_wr(dma, val);
		return;
	}
	dma->wrcount = 0;
	/* Use bits to classify the op and begin a new one */
	if ((val & 0x87) == 4) {
		/* 0XXXX100 : WR 1 */
		dma->reg[WR1] = val;
		if (val & 0x40)
			dma->wregmask |= R_TIMING_A;
		return;
	}
	if ((val & 0x87) == 0) {
		/* 0XXXX000 : WR2 */
		dma->reg[WR2] = val;
		if (val & 0x40)
			dma->wregmask |= R_TIMING_B;
		return;
	}
	if ((val & 0x80) == 0) {
		/* 0XXXXXXNN : WR 0 */
		dma->reg[WR0] = val;
		if (val & 0x40)
			dma->wregmask |= R_LEN_H;
		if (val & 0x20)
			dma->wregmask |= R_LEN_L;
		if (val & 0x10)
			dma->wregmask |= R_ADDR_A_H;
		if (val & 0x08)
			dma->wregmask |= R_ADDR_A_L;
		return;
	}
	dma->reg[WR3 + (val & 3)] = val;
	switch(val & 0x03) {
	case 0:
		/* WR 3 */
		if (val & 0x10)
			dma->wregmask |= R_ADDR_MATCH;
		if (val & 0x08)
			dma->wregmask |= R_ADDR_MASK;
		break;
	case 1:	/* WR 4 */
		if (val & 0x10)
			dma->wregmask |= R_INTCTL;
		if (val & 0x08)
			dma->wregmask |= R_ADDR_B_H;
		if (val & 0x04)
			dma->wregmask |= R_ADDR_B_L;
		/* Int mask triggers its own .. we will deal with that
		   special case in z80dma_wr() */
		break;
	case 2: /* WR 5 */
		break;
	case 3:
		/* WR 6: commands */
		z80dma_command(dma);
		break;
	}
}

static uint8_t z80_dma_one_cycle(struct z80dma *dma)
{
	uint16_t addr_a, addr_b;
	int port_a, port_b;
	uint8_t byte;

	if (!dma->enabled)
		return 0;
	/* Ok what are we doing ? */
	/* For now just model simple block transfers */

	addr_a = dma->reg[RR3] | (dma->reg[RR4] << 8);
	port_a = dma->reg[WR1] & 0x08;

	/* Weird rules about register B */
	if (dma->reg[WR2] & 0x20) 
		addr_b = dma->reg[WR_B_L] | (dma->reg[WR_B_H] << 8);
	else {
		if ((dma->reg[RR1] | dma->reg[RR2]) == 0) {
			dma->reg[RR5] = dma->reg[WR_B_L];
			dma->reg[RR6] = dma->reg[WR_B_H];
		}
		addr_b = dma->reg[RR5] | (dma->reg[RR6] << 8);
	}
	port_b = dma->reg[WR2] & 0x08;

	/* FIXME: add match/mask to this loop */

	if (dma->reg[WR0] & 4) {
		/* A->B */
		if (port_a)
			byte = io_read(0, addr_a);
		else
			byte = mem_read(0, addr_a);
		if (port_b)
			io_write(0, addr_b, byte);
		else
			mem_write(0, addr_b, byte);
	} else {
		if (port_b)
			byte = io_read(0, addr_b);
		else
			byte = mem_read(0, addr_b);
		if (port_a)
			io_write(0, addr_a, byte);
		else
			mem_write(0, addr_a, byte);
	}

	/* Adjust addresses and counters */
	dma->reg[RR1]++;
	if (dma->reg[RR1] == 0)
		dma->reg[RR2]++;
	if (!(dma->reg[WR1] & 0x20)) {
		if (dma->reg[WR1] & 0x10) {
			dma->reg[RR3]++;
			if (dma->reg[RR3] == 0)
				dma->reg[RR4]++;
		} else {
			dma->reg[RR3]--;
			if (dma->reg[RR3] == 255)
				dma->reg[RR4]--;
		}
	}
	if (!(dma->reg[WR2] & 0x20)) {
		if (dma->reg[WR2] & 0x10) {
			dma->reg[RR5]++;
			if (dma->reg[RR5] == 0)
				dma->reg[RR6]++;
		} else {
			dma->reg[RR5]--;
			if (dma->reg[RR5] == 255)
				dma->reg[RR6]--;
		}
	}
	if (dma->reg[RR1] == dma->reg[WR_LEN_L] &&
	    dma->reg[RR2] == dma->reg[WR_LEN_H]) {
		/* Completed */
		dma->enabled = 0;
		dma->forcerdy = 0;
		dma->reg[RR0] |= 1;
		/* TODO: interrupt emulation */
	}
	return 2;	/* 2 tstates per simple bus hog */
}

static uint8_t z80_dma_calc_idle(struct z80dma *dma)
{
	uint8_t idle;
	idle = dma->reg[WR_TIMING_A] & 3;
	idle += dma->reg[WR_TIMING_B] & 3;
	/* values are 0 1 or 2 for each corresponding to 2 1 0 waits or
	   50%, 75%, 100% DMA cycle */
	return 4 - idle;
}

/* This is the key routine for processing DMA. It's called repeatedly and
   decides whether to consume cycles or give them to the CPU */
static uint8_t z80_dma_do_run(struct z80dma *dma)
{
	/* If we model ready lines we will check here and no not ready
	   report 0 */
	if (dma->idle) {
		dma->idle--;
		return 0;
	}
	dma->idle = z80_dma_calc_idle(dma);
	return z80_dma_one_cycle(dma);
}

/* Simulate the given number of clocks of DMA and return the number we didn't
   use that can therefore be CPU given. We don't simulate at the clock level
   as for RC2014 it doesn't matter if we block it up a shade */

int z80_dma_run(struct z80dma *dma, int cycles)
{
	int spare = 0;

	if (!dma->enabled)
		return cycles;

	while(cycles) {
		int n = z80_dma_do_run(dma);
		if (n == 0) {
			/* CPU time */
			spare++;
			cycles--;
		} else
			cycles -= n;
	}
	return spare;
}


struct z80dma *z80dma_create(void)
{
	struct z80dma *dma = malloc(sizeof(struct z80dma));
	if (dma == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	z80dma_reset(dma);
	return dma;
}

void z80dma_free(struct z80dma *dma)
{
	free(dma);
}

void z80dma_trace(struct z80dma *dma, int on)
{
	dma->trace = on;
}
