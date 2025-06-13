/*
 *	1802 CPU emulation. Loosely based upon 1802UNO
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "1802.h"

static int condition(struct cp1802 *cpu, int cc)
{
	int r;
	switch(cc & 7) {
	case 0:		/* Unconditional */
		r = 1;
		break;
	case 1:		/* BQ */
		r = cpu->q;
		break;
	case 2:		/* BZ */
		r = !cpu->d;
		break;
	case 3:		/* BDF */
		r = cpu->df;
		break;
	case 4:		/* B on EF */
		r = cp1802_ef(cpu) & 1;
		break;
	case 5:
		r = cp1802_ef(cpu) & 2;
		break;
	case 6:
		r = cp1802_ef(cpu) & 4;
		break;
	case 7:
		r = cp1802_ef(cpu) & 8;
	}
	if (cc & 8)
		r = !r;
	return r;
}

static int skip(struct cp1802 *cpu, int cc)
{
	int r;
	/* NOP is special - there isn't a skip on !ie */
	if (cc == 4)
		return 0;
	switch(cc & 3) {
	case 0:
		r = !cpu->ie;
	case 1:
		r = !cpu->q;
		break;
	case 2:
		r = !cpu->d;
		break;
	case 3:
		r = !cpu->df;
		break;
	}
	if (cc & 8)
		r = !r;
	return r;
}

/* TODO: identify how the 1804/1805 processes invalid BCD */
static uint8_t tobcd(uint8_t r)
{
	return ((r / 10) << 4) + (r % 10);
}

static uint8_t frombcd(uint8_t r)
{
	return ((r & 0xF0) >> 4) * 10 + (r & 0x0F);
}

static void decimal_add(struct cp1802 *cpu, uint8_t r, uint8_t c)
{
	uint8_t res = frombcd(cpu->d) + frombcd(r) + c;
	if (res > 99)
		cpu->df = 1;
	else
		cpu->df = 0;
	cpu->d = tobcd(res);
}

static void decimal_sub(struct cp1802 *cpu, uint8_t r, uint8_t c)
{
	/* Subtraction is borrow based not carry based but the caller handles
	   that */
	int res = frombcd(cpu->d) - frombcd(r) - c;
	if (res < 0)
		cpu->df = 0;
	else
		cpu->df = 1;
	cpu->d = tobcd(res & 0xFF);
}

static uint16_t cp1802_read16(struct cp1802 *cpu, uint16_t a)
{
	return (cp1802_read(cpu, a) << 8) | cp1802_read(cpu, a + 1);
}

static void cp1802_write16(struct cp1802 *cpu, uint16_t a, uint16_t d)
{
	cp1802_write(cpu, a, d >> 8);
	cp1802_write(cpu, a + 1, d);
}

/*
   TODO
   - cycle counts
   - counter emulation
   - correct etq clearing and other counter detail
 */

/* The 1805 has the 1804 instructions and extends them with some decimal
   mode arithmetic, a 16bit decrement and branch non zero for loops, and
   a special instruction for some IRQ style stacking/saving */

/*
 *	Process the 1804/5/6 counter. Simulates one machine cycle
 */

static void counter_decrement(struct cp1802 *cpu)
{
	if (cpu->ct_stop)
		return;
	cpu->ct_val--;
	if (cpu->ct_val)
		return;
	/* Do zero count processing */
	cpu->ct_val = cpu->ct_count;
	cpu->ct_int = 1;
	if (cpu->ct_etq)
		cpu->q ^= 1;
}

static void counter_cycle(struct cp1802 *cpu)
{
	uint8_t d;
	if (cpu->ct_stop)
		return;
	switch(cpu->ct_mode) {
	case 0:	/* Not set */
		break;
	case 1:	/* SPM */
		if (cp1802_ef(cpu) & (1 << cpu->ct_ef))
			cpu->ct_stop = 1;
		break;
	case 2:	/* SCM */
		d = cp1802_ef(cpu);
		/* Not a high -> low transition */
		if ((d & (1 << cpu->ct_ef)) || !(cpu->oldef & (1 << cpu->ct_ef))) {
			cpu->oldef = d;
			return;
		}
		/* High to low - so we count */
		break;
	case 3:	/* STM */
		cpu->ct_step++;
		if (cpu->ct_step != 32)
			return;
	}
	if (cpu->ct_stop)
		return;
	/* Do a counter event */
	counter_decrement(cpu);
}

static void execute_1805(struct cp1802 *cpu)
{
	uint8_t opcode = cp1802_read(cpu, cpu->r[cpu->p]++);
	uint8_t reg = opcode & 0x0F;
	uint16_t tmp;

	cpu->mcycles ++;

	switch(opcode & 0xF0) {
	case 0:		/* Counter */
		switch(reg) {
		case 0:	/* STPC */
			cpu->ct_mode = 0;
			cpu->ct_stop = 1;
			cpu->ct_step = 0;
			cpu->ct_etq = 0;
			break;
		case 1:	/* DTC */
			counter_decrement(cpu);
			break;
		case 2:	/* SPM2 */
		case 3:	/* SCM2 */
		case 4: /* SPM1 */
		case 5:	/* SCM1 */
			cpu->ct_mode = 1 + (reg & 1);
			cpu->ct_ef = (reg & 2) >> 1;
			cpu->ct_stop = 0;
			break;
		case 6:	/* LDC */
			cpu->ct_count = cpu->d;
			if (cpu->ct_stop) {
				cpu->ct_val = cpu->d;
				cpu->ct_int = 0;
				cpu->ct_etq = 0;
			}
			break;
		case 7:	/* STM */
			cpu->ct_mode = 3;
			cpu->ct_stop = 0;
			break;
		case 8: /* GEC */
			cpu->d = cpu->ct_val;
			break;
		case 9:	/* ETQ */
			cpu->ct_etq = 1;
			break;
		case 10: /* XIE */
		case 11: /* XID */
			cpu->xie = !(reg & 1);
			break;
		case 12: /* CIE */
		case 13: /* CID */
			cpu->cie = !(reg & 1);
			break;
		}
		break;
	case 0x20: 	/* DBNZ */
		tmp = cp1802_read16(cpu, cpu->r[cpu->p]);
		if (--cpu->r[reg])
			cpu->r[cpu->p] += 2;
		else
			cpu->r[cpu->p] = tmp;
		break;
	case 0x30:	/* BCI and BXI */
		tmp = cp1802_read(cpu, cpu->r[cpu->p]);
		switch(reg) {
		case 14: /* BCI */
			if (cpu->ct_int) {
				cpu->r[cpu->p] &= 0xFF00;
				cpu->r[cpu->p] |= tmp;
				cpu->ct_int = 0;
				cpu->ct_etq = 0;
			} else {
				cpu->r[cpu->p]++;
			}
			break;
		case 15: /* BXI */
			if (cpu->ipend) {
				cpu->r[cpu->p] &= 0xFF00;
				cpu->r[cpu->p] |= tmp;
			} else {
				cpu->r[cpu->p]++;
			}
			break;
		}
		break;
	case 0x60:	/* RLXA */
		cpu->r[reg] = cp1802_read16(cpu, cpu->r[cpu->x]);
		cpu->r[cpu->x] += 2;
		cpu->mcycles += 2;
		break;
	case 0x70:	/* Decimal arithmetic with carry */
		cpu->mcycles++;
		switch(reg) {
		case 4:	/* DADC */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]);
			decimal_add(cpu, tmp, cpu->df);
			break;
		case 6:	/* DSAV:  Weird one buried in the decimals */
			cpu->t = cp1802_read(cpu, --cpu->r[cpu->x]);
			cpu->d = cp1802_read(cpu, --cpu->r[cpu->x]) >> 1;
			cpu->d |= cpu->df ? 0x80 : 0;
			cp1802_write(cpu, cpu->r[cpu->x], cpu->d);
			break;
		case 7:	/* DSMB */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]);
			decimal_sub(cpu, tmp, 1 - cpu->df);
			break;
		case 12: /* DACI */
			tmp = cp1802_read(cpu, cpu->r[cpu->p]++);
			decimal_add(cpu, tmp, cpu->df);
			break;
		case 15: /* DSBI */
			tmp = cp1802_read(cpu, cpu->r[cpu->p]++);
			decimal_sub(cpu, tmp, 1 - cpu->df);
			break;
		}
		break;
	case 0x80:	/* SCAL */
		cp1802_write16(cpu, cpu->r[cpu->x], cpu->r[reg]);
		cpu->r[reg] = cpu->r[cpu->p];
		cpu->r[cpu->p] = cp1802_read16(cpu, cpu->r[cpu->p]);
		cpu->mcycles += 7;
		/* FIXME: what does T end up holding */
		break;
	case 0x90:	/* SRET */
		cpu->r[cpu->p] = cpu->r[reg];
		cpu->r[cpu->x] += 2;
		tmp = cp1802_read16(cpu, cpu->r[cpu->x]);
		cpu->r[reg] = tmp;
		cpu->mcycles += 5;
		break;
	case 0xA0:	/* RSXD */
		cp1802_write16(cpu, cpu->r[cpu->x], cpu->r[reg]);
		cpu->r[cpu->x] += 2;
		cpu->mcycles += 2;
		break;
	case 0xB0:	/* RNX */
		cpu->r[cpu->x] = cpu->r[reg];
		cpu->mcycles++;
		break;
	case 0xC0:	/* RLDI */
		tmp = cp1802_read16(cpu, cpu->r[cpu->p]);
		cpu->r[cpu->p] += 2;
		cpu->r[reg] = tmp;
		cpu->mcycles += 2;
		break;
	case 0xF0:	/* Decimal arithmetic without carry */
		cpu->mcycles++;
		switch(reg) {
		case 4:	/* DADD */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]);
			decimal_add(cpu, tmp, 0);
			break;
		case 7:	/* DSM */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]);
			decimal_sub(cpu, tmp, 1);
			break;
		case 12: /* DADI */
			tmp = cp1802_read(cpu, cpu->r[cpu->p]++);
			decimal_add(cpu, tmp, 0);
			break;
		case 15: /* DSMI */
			tmp = cp1802_read(cpu, cpu->r[cpu->p]++);
			decimal_sub(cpu, tmp, 1);
			break;
		}
		break;
	}
}


/* The 1804 has the 1802 instructions and adds an extended set of
   instructions prefixed with 68. These fix some of the big gaps in the
   1802 for register loading/saving, call/return etc and also add some
   directly instruction interfaced counter controls */
static void execute_1804(struct cp1802 *cpu)
{
	uint8_t opcode = cp1802_read(cpu, cpu->r[cpu->p]++);
	uint8_t reg = opcode & 0x0F;
	uint16_t tmp;


	cpu->mcycles++;

	switch(opcode & 0xF0) {
	case 0:		/* Counter */
		switch(reg) {
		case 0:	/* STPC */
			cpu->ct_mode = 0;
			cpu->ct_stop = 1;
			cpu->ct_etq = 0;
			break;
		case 1:	/* DTC */
			counter_cycle(cpu);
			break;
		case 2:	/* SPM2 */
		case 3:	/* SCM2 */
		case 4: /* SPM1 */
		case 5:	/* SCM1 */
			cpu->ct_mode = 1 + (reg & 1);
			cpu->ct_ef = (reg & 2) >> 1;
			cpu->ct_stop = 0;
			break;
		case 6:	/* LDC */
			/* Double check what happens if running when we do this ? */
			if (cpu->ct_stop) {
				cpu->ct_count = cpu->d;
				cpu->ct_val = cpu->d;
			}
			cpu->ct_int = 0;
			cpu->ct_mode = 0;
			break;
		case 7:	/* STM */
			cpu->ct_mode = 3;
			cpu->ct_stop = 0;
			break;
		case 8: /* GEC */
			cpu->d = cpu->ct_val;
			break;
		case 9:	/* ETQ */
			cpu->ct_etq = 1;
			break;
		case 10: /* XIE */
		case 11: /* XID */
			cpu->xie = !(reg & 1);
			break;
		case 12: /* CIE */
		case 13: /* CID */
			cpu->cie = !(reg & 1);
			break;
		}
		break;
	case 0x30:	/* BCI and BXI */
		tmp = cp1802_read(cpu, cpu->r[cpu->p]);
		switch(reg) {
		case 14: /* BCI */
			if (cpu->ct_int) {
				cpu->r[cpu->p] &= 0xFF00;
				cpu->r[cpu->p] |= tmp;
				cpu->ct_int = 0;
				cpu->ct_etq = 0;
			} else {
				cpu->r[cpu->p]++;
			}
			break;
		case 15: /* BXI */
			if (cpu->ipend) {
				cpu->r[cpu->p] &= 0xFF00;
				cpu->r[cpu->p] |= tmp;
			} else {
				cpu->r[cpu->p]++;
			}
			break;
		}
		break;
	case 0x60:	/* RLXA */
		cpu->r[reg] = cp1802_read16(cpu, cpu->r[cpu->x]);
		cpu->r[cpu->x] += 2;
		cpu->mcycles += 2;
		break;
	case 0x80:	/* SCAL */
		cp1802_write16(cpu, cpu->r[cpu->x], cpu->r[reg]);
		cpu->r[reg] = cpu->r[cpu->p];
		cpu->r[cpu->p] = cp1802_read16(cpu, cpu->r[cpu->p]);
		cpu->mcycles += 7;
		/* FIXME: what does T end up holding */
		break;
	case 0x90:	/* SRET */
		cpu->r[cpu->p] = cpu->r[reg];
		cpu->r[cpu->x] += 2;
		tmp = cp1802_read16(cpu, cpu->r[cpu->x]);
		cpu->r[reg] = tmp;
		cpu->mcycles += 5;
		break;
	case 0xA0:	/* RSXD */
		cp1802_write16(cpu, cpu->r[cpu->x], cpu->r[reg]);
		cpu->r[cpu->x] += 2;
		cpu->mcycles += 2;
		break;
	case 0xB0:	/* RNX */
		cpu->r[cpu->x] = cpu->r[reg];
		cpu->mcycles++;
		break;
	case 0xC0:	/* RLDI */
		tmp = cp1802_read16(cpu, cpu->r[cpu->p]);
		cpu->r[cpu->p] += 2;
		cpu->r[reg] = tmp;
		cpu->mcycles++;
		break;
	}
}

static void execute_op(struct cp1802 *cpu)
{
	uint8_t opcode = cp1802_read(cpu, cpu->r[cpu->p]++);
	uint8_t reg = opcode & 0x0F;
	uint16_t tmp;

	cpu->mcycles += 2;

	switch(opcode >> 4) {
	case 0:
		/* IDL etc */
		if (reg)
			cpu->d = cp1802_read(cpu, cpu->r[reg]);
		else {
			/* idle until interrupt or dma */
			if (!cpu->event)
				cpu->r[cpu->p]--;
			cpu->event = 0;
		}
		break;
	case 1:	/* INC */
		cpu->r[reg]++;
		break;
	case 2:	/* DEC */
		cpu->r[reg]--;
		break;
	case 3:
		/* Branches */
		/* See page 31: the address of the immediate byte determines
		   the page to which a branch takes place */
		tmp = cp1802_read(cpu, cpu->r[cpu->p]);
		if (condition(cpu, reg)) {
			cpu->r[cpu->p] &= 0xFF00;
			cpu->r[cpu->p] |= tmp;
		} else
			cpu->r[cpu->p]++;
		break;
	case 4:	/* LDA */
		cpu->d = cp1802_read(cpu, cpu->r[reg]++);
		break;
	case 5:	/* STR */
		cp1802_write(cpu, cpu->r[reg], cpu->d);
		break;
	case 6:	/* INP/OUP/IRX */
		/* 68 is a prefix on later processors for extended instructions */
		if (reg == 8) {
			if (cpu->type == 1804) {
				execute_1804(cpu);
				return;
			}
			if (cpu->type >= 1805) {
				execute_1805(cpu);
				return;
			}
		}
		/* I/O is a bit weird */
		if (reg < 8)
			cp1802_out(cpu, reg, cp1802_read(cpu, cpu->r[cpu->x]));
		else {
			/* FIXME: 68 does exactly what on an 1802 */
			cpu->d = cp1802_in(cpu, reg & 7);
			cp1802_write(cpu, cpu->r[cpu->x], cpu->d);
		}
		break;
	case 7:
		/* This block of opcodes is unstructured and reg is something else */
		switch(reg) {
		case 0:		/* RET */
		case 1:
			/* DIS */
			/* Beware: the 1802 datasheet implies the new X is incremented
			   but this is not in fact the case */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]++);
			cpu->x = tmp >> 4;
			cpu->p = tmp & 0x0F;
			cpu->ie = !reg;
			break;
		case 2:		/* LDXA */
			cpu->d = cp1802_read(cpu, cpu->r[cpu->x]++);
			break;
		case 3:		/* STXD */
			cp1802_write(cpu, cpu->r[cpu->x]--, cpu->d);
			break;
		case 4:		/* ADC */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]) + cpu->d + cpu->df;
			cpu->df = (tmp >> 8) & 1;	/* Carry */
			cpu->d = tmp & 0xFF;
			break;
		case 5:		/* SDB */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]) - cpu->d - !cpu->df;
			cpu->df = !(tmp >> 8);		/* Borrow */
			cpu->d = tmp & 0xFF;
			break;
		case 6:		/* SHRC */
			tmp = cpu->d & 1;
			cpu->d >>= 1;
			cpu->d |= cpu->df << 7;
			cpu->df = tmp;
			break;
		case 7:		/* SDM */
			tmp = cpu->d - cp1802_read(cpu, cpu->r[cpu->x]) - !cpu->df;
			cpu->df = !(tmp >> 8);		/* Borrow */
			cpu->d = tmp;
			break;
		case 8:		/* SAV */
			cp1802_write(cpu, cpu->r[cpu->x], cpu->t);
			break;
		case 9:		/* MARK */
			cpu->t = (cpu->x << 4) | cpu->p;
			cp1802_write(cpu, cpu->r[2]--, cpu->t);
			cpu->x = cpu->p;
			break;
		case 10:	/* REQ */
			cpu->q = 0;
			cp1802_q_set(cpu);
			break;
		case 11:	/* SEQ */
			cpu->q = 1;
			cp1802_q_set(cpu);
			break;
		case 12:	/* ADCI */
			tmp = cpu->d + cp1802_read(cpu, cpu->r[cpu->p]++);
			cpu->df = (tmp >> 8) & 1;
			cpu->d = tmp;
			break;
		case 13:	/* SDBI */
			tmp = cp1802_read(cpu, cpu->r[cpu->p]) - cpu->d - !cpu->df;
			cpu->df = !(tmp >> 8);		/* Borrow */
			cpu->d = tmp & 0xFF;
			break;
		case 14:	/* SHLC */
			tmp = cpu->d & 0x80;
			cpu->d <<= 1;
			cpu->d |= cpu->df;
			cpu->df = !tmp;		/* Check this is right ??? */
			break;
		case 15:	/* SMBI */
			tmp = cpu->d - cp1802_read(cpu, cpu->r[cpu->p]++) - !cpu->df;
			cpu->df = !(tmp >> 8);
			cpu->d = tmp;
			break;
		}
		break;
	case 8:	/* GLO */
		cpu->d = cpu->r[reg] & 0xFF;
		break;
	case 9:	/* GHI */
		cpu->d = cpu->r[reg] >> 8;
		break;
	case 10: /* PLO */
		cpu->r[reg] &= 0xFF00;
		cpu->r[reg] |= cpu->d;
		break;
	case 11: /* PHI */
		cpu->r[reg] &= 0x00FF;
		cpu->r[reg] |= (cpu->d << 8);
		break;
	case 12: /* Jumps, skips and NOP */
		cpu->mcycles++;		/* Check always 3 on skips and nop */
		if (reg & 4) {	/* Skip */
			if (skip(cpu, reg))
				cpu->r[cpu->p] += 2;
		} else {	/* LBR */
			tmp = cp1802_read(cpu, cpu->r[cpu->p]++) << 8;
			tmp |= cp1802_read(cpu, cpu->r[cpu->p]++);
			if (condition(cpu, reg))
				cpu->r[cpu->p] = tmp;
		}
		break;
	case 13:	/* SEP */
		cpu->p = reg;
		break;
	case 14:	/* SEX */
		cpu->x = reg;
		break;
		/* Another chunk of misc mostly logic etc */
	case 15:
		switch(reg) {
		case 0:		/* LDX */
			cpu->d = cp1802_read(cpu, cpu->r[cpu->x]);
			break;
		case 1:		/* OR */
			cpu->d |= cp1802_read(cpu, cpu->r[cpu->x]);
			break;
		case 2:		/* AND */
			cpu->d &= cp1802_read(cpu, cpu->r[cpu->x]);
			break;
		case 3:		/* XOR */
			cpu->d ^= cp1802_read(cpu, cpu->r[cpu->x]);
			break;
		case 4:		/* ADD */
			tmp = cpu->d + cp1802_read(cpu, cpu->r[cpu->x]);
			cpu->df = tmp >> 8;
			cpu->d = tmp;
			break;
		case 5:		/* SD */
			tmp = cp1802_read(cpu, cpu->r[cpu->x]) - cpu->d;
			cpu->df = !(tmp >> 8);
			cpu->d = tmp;
			break;
		case 6:		/* SHR */
			cpu->df = cpu->d & 1;
			cpu->d >>= 1;
			break;
		case 7:		/* SM */
			tmp = cpu->d - cp1802_read(cpu, cpu->r[cpu->x]);
			cpu->df = !(tmp >> 8);
			cpu->d = tmp;
			break;
		case 8:		/* LDI */
			cpu->d = cp1802_read(cpu, cpu->r[cpu->p]++);
			break;
		case 9:		/* ORI */
			cpu->d |= cp1802_read(cpu, cpu->r[cpu->p]++);
			break;
		case 10:	/* ANI */
			cpu->d &= cp1802_read(cpu, cpu->r[cpu->p]++);
			break;
		case 11:	/* XRI */
			cpu->d ^= cp1802_read(cpu, cpu->r[cpu->p]++);
			break;
		case 12:	/* ADI */
			tmp = cpu->d + cp1802_read(cpu, cpu->r[cpu->p]++);
			cpu->df = tmp >> 8;
			cpu->d = tmp;
			break;
		case 13:	/* SDI */
			tmp = cp1802_read(cpu, cpu->r[cpu->p]++) - cpu->d;
			cpu->df = !(tmp >> 8);
			cpu->d = tmp;
			break;
		case 14:	/* SHL */
			cpu->df = cpu->d >> 7;
			cpu->d <<= 1;
			break;
		case 15:	/* SMI */
			tmp = cpu->d - cp1802_read(cpu, cpu->r[cpu->p]++);
			cpu->df = !(tmp >> 8);
			cpu->d = tmp;
			break;
		}
	}
}

/* FIXME: need to account this in clocks we return used */
static void interrupt(struct cp1802 *cpu)
{
	cpu->t = cpu->p | (cpu->x << 4);
	cpu->p = 1;
	cpu->x = 2;
	cpu->ie = 0;
	cpu->event = 1;
}

/* FIXME: up cpu->mcycles correctly here */
void cp1802_dma_in_cycle(struct cp1802 *cpu)
{
	cp1802_write(cpu, cpu->r[0]++, cp1802_dma_in(cpu));
	cpu->event = 1;
}

void cp1802_dma_out_cycle(struct cp1802 *cpu)
{
	uint8_t tmp = cp1802_read(cpu, cpu->r[0]++);
	cp1802_dma_out(cpu, tmp);
	cpu->event = 1;
}

void cp1802_interrupt(struct cp1802 *cpu, int irq)
{
	cpu->ipend = irq;
}

/* Run an instruction */
int cp1802_run(struct cp1802 *cpu)
{
	unsigned int elapsed = cpu->mcycles;
	if (cpu->ie && cpu->cie && cpu->ct_int)
		interrupt(cpu);
	else if (cpu->ie && cpu->xie && cpu->ipend)
		interrupt(cpu);
	execute_op(cpu);
	/* Simulate the counter on the 1804/5/6 */
	if (cpu->type >= 1804) {
		elapsed = cpu->mcycles - elapsed;
		while(elapsed--) {
			counter_cycle(cpu);
		}
	}
	return cpu->mcycles;
}

void cp1802_reset(struct cp1802 *cpu)
{
	memset(cpu, 0, sizeof(*cpu));
	cpu->t = (cpu->x <<4) | (cpu->p);
	cpu->x = 0;
	cpu->p = 0;
	cpu->r[0] = 0;
	cpu->q = 0;
	if (cpu->type == 1802) {
		cpu->xie = 1;	/* So we ignore xie */
		cpu->cie = 0;	/* and never do counters */
	} else {
		cpu->xie = 1;
		cpu->cie = 1;
		cpu->ct_stop = 1;
		cpu->ct_int = 0;
		cpu->ct_mode = 0;
		cpu->ct_etq = 0;
	}
	cpu->mcycles += 9;
}

void cp1802_init(struct cp1802 *cpu, int type)
{
	memset(cpu, 0, sizeof(*cpu));
	cpu->type = type;
	cp1802_reset(cpu);
}
