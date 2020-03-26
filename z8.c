/*
 *	Z86C91 CPU emulator
 *
 *	This is a work in progress and not yet usable
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "z8.h"


static uint8_t getreg_internal(struct z8 *z8, uint8_t reg)
{
	if (reg > z8->regmax && reg < R_SIO) {
		fprintf(stderr, "[Read non existent register %d]\n", reg);
		return 0xFF;
	}
	switch(reg) {
	case 2:
	case 3:
		return z8_port_read(z8, reg);
	case R_PRE0:
	case R_PRE1:
	case R_P01M:
	case R_P2M:
	case R_P3M:
	case R_IPR:
		fprintf(stderr, "[Read R%d: Invalid.]\n", reg);
		return 0xFF;
	case R_T0:
		return z8->t0;
	case R_T1:
		return z8->t1;
	default:
		return z8->reg[reg];
	}
}

uint8_t makereg(struct z8 *z8, uint8_t reg)
{
	return (z8->reg[R_RP] & 0xF0) | (reg & 0x0F);
}

static uint8_t getreg(struct z8 *z8, uint8_t reg)
{
	if ((reg & 0xF0) == 0xE0)
		reg = makereg(z8, reg);
	return getreg_internal(z8, reg);
}

static void setreg_internal(struct z8 *z8, uint8_t reg, uint8_t val)
{
	if (reg > z8->regmax && reg < R_SIO) {
		fprintf(stderr, "[Wrote non existent register %d with %d]\n", reg, val);
		return;
	}
	/* Q: is the requirement to DI before modifying IPR/IMR hw
	   enforced */ 
	switch(reg) {
	case 2:
	case 3:
		z8->reg[reg] = val;
		z8_port_write(z8, 2, val);
		break;
	case R_SIO:
		if (z8->reg[R_P3M] & 0x40)
			z8_tx(z8, val);
	default:
	z8->reg[reg] = val;
	}
}

static void setreg(struct z8 *z8, uint8_t reg, uint8_t val)
{
	if ((reg & 0xF0) == 0xE0)
		reg = makereg(z8, reg);
	return setreg_internal(z8, reg, val);
}


static uint8_t getireg(struct z8 *z8, uint8_t reg)
{
	return getreg_internal(z8, getreg(z8, reg));
}

void setireg(struct z8 *z8, uint8_t reg, uint8_t val)
{
	setreg_internal(z8, getreg(z8, reg), val);
}


uint8_t getwreg(struct z8 *z8, uint8_t reg)
{
	return getreg_internal(z8, makereg(z8, reg));
}

static void setwreg(struct z8 *z8, uint8_t reg, uint8_t val)
{
	setreg_internal(z8, makereg(z8, reg), val);
}

static uint8_t getiwreg(struct z8 *z8, uint8_t reg)
{
	return getreg_internal(z8, getwreg(z8, reg));
}

static void setiwreg(struct z8 *z8, uint8_t reg, uint8_t val)
{
	setreg_internal(z8, getreg_internal(z8, makereg(z8, reg)), val);
}

static uint16_t getIrr(struct z8 *z8, uint8_t reg)
{
	reg = getwreg(z8, reg);
	return (getreg(z8, reg) << 8) | getreg(z8, reg + 1);
}

static uint16_t getIRR(struct z8 *z8, uint8_t reg)
{
	reg = getreg(z8, reg);
	return (getreg_internal(z8, reg) << 8) | getreg_internal(z8, reg + 1);
}

static uint8_t z8_pop8(struct z8 *z8)
{
	uint16_t sp;
	uint8_t r;
	if (z8->reg[R_P01M] & 0x04)
		return getreg_internal(z8, z8->reg[R_SPL]++);
	sp = (z8->reg[R_SPH] << 8) | z8->reg[R_SPL];
	r = z8_read_data(z8, sp++);
	z8->reg[R_SPH] = sp >> 8;
	z8->reg[R_SPL] = sp;
	return r;
}

static void z8_push8(struct z8 *z8, uint8_t val)
{
	uint16_t sp;
	if (z8->reg[R_P01M] & 0x04) {
		setreg_internal(z8, --z8->reg[R_SPL], val);
		return;
	}
	sp = (z8->reg[R_SPH] << 8) | z8->reg[R_SPL];
	z8_write_data(z8, --sp, val);
	z8->reg[R_SPH] = sp >> 8;
	z8->reg[R_SPL] = sp;
}

static uint16_t z8_pop16(struct z8 *z8)
{
	uint16_t v = z8_pop8(z8);
	v <<= 8;
	v |= z8_pop8(z8);
}


static void z8_push16(struct z8 *z8, uint16_t v)
{
	z8_push8(z8, v);
	z8_push8(z8, v >> 8);
}

/* Maths operations that affect all the flags. These seem to work identically
   to the 6803 */
static uint8_t z8_maths(struct z8 *z8, uint8_t r, uint8_t d)
{
	uint8_t f = 0;
	uint8_t a = z8->arg0;
	uint8_t b = z8->arg1;

	z8->reg[R_FLAGS] &= ~ (F_C | F_Z | F_S | F_D | F_H | F_V);

	if (r & 0x80)
		f |= F_S;
	if (r == 0)
		f |= F_Z;
	if ((a & b & 0x80) && !(r & 0x80))
		f |= F_V;
	if (!((a | b) & 0x80) && (r & 0x80))
		f |= F_V;
	if (~a & b & 0x80)
		f |= F_C;
	if (b & r & 0x80)
		f |= F_C;
	if (~a & r & 0x80)
		f |= F_C;
	/* And half carry for DAA */
	if ((a & b & 0x08) || ((b & ~r) & 0x08) || ((a & ~r) & 0x08))
		f |= F_H;
	if (d)			/* Remember direction for DAA */
		f |= F_D;
	z8->reg[R_FLAGS] |= f;
	return r;
}

/* Ditto but not affecting D and H (CP) */
static uint8_t z8_maths_noh(struct z8 *z8, uint8_t r)
{
	uint8_t f = 0;
	uint8_t a = z8->arg0;
	uint8_t b = z8->arg1;
	z8->reg[R_FLAGS] &= ~ (F_C | F_Z | F_S | F_V);
	if (r & 0x80)
		f |= F_S;
	if (r == 0)
		f |= F_Z;
	if ((a & b & 0x80) && !(r & 0x80))
		f |= F_V;
	if (!((a | b) & 0x80) && (r & 0x80))
		f |= F_V;
	if (~a & b & 0x80)
		f |= F_C;
	if (b & r & 0x80)
		f |= F_C;
	if (~a & r & 0x80)
		f |= F_C;
	z8->reg[R_FLAGS] |= f;
	return r;
}

/* 8bit logic : affects ZSV only  */
static uint8_t z8_logic(struct z8 *z8, uint8_t r)
{
	uint8_t f = 0;
	z8->reg[R_FLAGS] &= ~ (F_Z | F_S | F_V);
	if (r & 0x80)
		f |= F_S;
	if (r == 0)
		f |= F_Z;
	z8->reg[R_FLAGS] |= f;
	return r;
}

/* 16bit inc and dec */

static void z8_idop16(struct z8 *z8, uint8_t reg, int mod)
{
	uint16_t v = (getreg(z8, reg) << 8) | getreg(z8, reg + 1);
	uint8_t f;
	v += mod;
	setreg(z8, reg, v >> 8);
	setreg(z8, reg + 1, v);

	f = z8->reg[R_FLAGS] & ~(F_Z | F_S | F_V);
	if (v == 0)
		f |= F_Z;
	if (v & 0x8000)
		f |= F_S;
	if (v == 0x8000 && mod == -1)
		f |= F_V;
	if (v == 0x7FFF && mod == 1)
		f |= F_V;
	z8->reg[R_FLAGS] |= f;
}

static int z8_cc_true(struct z8 *z8, int code)
{
	int r = 0;
	uint8_t f = z8->reg[R_FLAGS];
	switch(code & 7) {
	case 0:	/* Never */
		break;
	case 2:	/* LE */
		if (f & F_Z)
			r = 1;
		/* Fall through */
	case 1:	/* LT */
		if (f & (F_S | F_V)) {
			if ((f & (F_S | F_V)) != (F_S | F_V))
				r = 1;
		}
		break;
	case 3:	/* ULE */
		if (f & (F_C|F_Z))
			r = 1;
		break;
	case 4:	/* OV */
		if (f & F_V)
			r = 1;
		break;
	case 5: /* MI */
		if (f & F_S)
			r = 1;
		break;
	case 6:	/* Z */
		if (f & F_Z)
			r = 1;
		break;
	case 7: /* ULT */
		if (f & F_C)
			r = 1;
		break;
	}
	/* Bit 3 inverts the test */
	if (code & 8)
		r = !r;
	return r;
}

static void z8_execute_group2(struct z8 *z8, uint_fast8_t oph, uint_fast8_t opl)
{
	uint8_t d;
	uint16_t da;
	switch (opl) {
	case 0x08:		/* LD r1,R2 */
		d = z8_read_code(z8, z8->pc++);
		setwreg(z8, oph, getreg(z8, d));
		z8->cycles += 6;
		break;
	case 0x09:		/* LD R2,r1 */
		d = z8_read_code(z8, z8->pc++);
		setreg(z8, d, getwreg(z8, oph));
		z8->cycles += 6;
		break;
	case 0x0A:		/* DJNZ */
		d = getwreg(z8, oph) - 1;
		setwreg(z8, oph, d);
		if (d) {
			d = z8_read_code(z8, z8->pc++);
			z8->pc += d;
			z8->cycles += 12;
		} else
			z8->cycles += 10;
		break;
	case 0x0B:		/* JR */
		d = z8_read_code(z8, z8->pc++);
		if (z8_cc_true(z8, oph)) {
			z8->cycles += 12;
			z8->pc += d;
		} else
			z8->cycles += 10;
		break;
	case 0x0C:		/* LD r1,IM */
		d = z8_read_code(z8, z8->pc++);
		setwreg(z8, oph, d);
		break;
	case 0x0D:		/* JP cc,DA */
		da = z8_read_code(z8, z8->pc++);
		da |= (z8_read_code(z8, z8->pc++) << 8);
		if (z8_cc_true(z8, oph)) {
			z8->cycles += 12;
			z8->pc = da;
		} else
			z8->cycles += 10;
		break;
	case 0x0E:		/* INC */
		d = getwreg(z8, oph) + 1;
		setwreg(z8, oph, d);
		z8->reg[R_FLAGS] &= ~(F_S | F_Z | F_V);
		if (d == 0)
			z8->reg[R_FLAGS] |= F_Z;
		if (d & 0x80)
			z8->reg[R_FLAGS] |= F_S;
		if (d == 0x80)
			z8->reg[R_FLAGS] |= F_V;
		z8->cycles += 6;
		break;
	case 0x0F:
		/* Specials */
		switch (oph) {
		case 0x06:
			/* TODO */
			/* STOP */
			break;
		case 0x07:
			/* TODO */
			/* HALT */
			break;	/* FIXME */
		case 0x08:
			/* DI */
			z8->reg[R_IMR] &= 0x7F;
			z8->cycles += 6;
			break;
		case 0x09:
			/* EI */
			z8->reg[R_IMR] |= 0x80;
			z8->cycles += 6;
			break;
		case 0x0A:
			/* RET */
			z8->reg[R_FLAGS] = z8_pop8(z8);
			z8->pc = z8_pop16(z8);
			z8->cycles += 14;
			break;
		case 0x0B:
			/* IRET */
			z8->reg[R_FLAGS] = z8_pop8(z8);
			z8->pc = z8_pop16(z8);
			z8->reg[R_IMR] |= 0x80;
			z8->cycles += 16;
			break;
		case 0x0C:
			/* RCF */
			z8->reg[R_FLAGS] &= ~F_C;
			z8->cycles += 6;
			break;
		case 0x0D:
			/* SCF */
			z8->reg[R_FLAGS] |= F_C;
			z8->cycles += 6;
			break;
		case 0x0E:
			/* CCF */
			z8->reg[R_FLAGS] ^= F_C;
			z8->cycles += 6;
			break;
		case 0x0F:
			/* NOP */
			z8->cycles += 6;
			break;
		}
	}
}

static void z8_execute_group3(struct z8 *z8, uint_fast8_t oph, uint_fast8_t opl)
{
	uint8_t v;
	uint8_t w;
	uint8_t r = z8_read_code(z8, z8->pc++);
	if (opl == 0x01) {
		r = getreg(z8, r);
	}
	switch (oph) {
	case 0x00:		/* DEC */
		v = getreg(z8, r) - 1;
		setreg(z8, r, v);
		z8->reg[R_FLAGS] &= ~(F_S | F_Z | F_V);
		if (v == 0)
			z8->reg[R_FLAGS] |= F_Z;
		if (v & 0x80)
			z8->reg[R_FLAGS] |= F_S;
		if (v == 0x7F)
			z8->reg[R_FLAGS] |= F_V;
		z8->cycles += 6;
		break;
	case 0x01:		/* RLC */
		v = getreg(z8, r);
		w = v << 1;
		w |= CARRY ? 0x01 : 0x00;
		if (v & 0x80)
			z8->reg[R_FLAGS] |= F_C;
		else
			z8->reg[R_FLAGS] &= ~F_C;
		setreg(z8, r, w);
		z8_logic(z8, w);
		z8->cycles += 6;
		break;
	case 0x02:		/* INC */
		v = getreg(z8, r) + 1;
		setreg(z8, r, v);
		z8->reg[R_FLAGS] &= ~(F_S | F_Z | F_V);
		if (v == 0)
			z8->reg[R_FLAGS] |= F_Z;
		if (v & 0x80)
			z8->reg[R_FLAGS] |= F_S;
		if (v == 0x80)
			z8->reg[R_FLAGS] |= F_V;
		z8->cycles += 6;
		break;
	case 0x03:		/* JP and SRP */
		if (opl == 0x00) {
			z8->pc = getIrr(z8, r);
			z8->cycles += 8;
		} else {
			setreg(z8, R_RP, z8_read_code(z8, z8->pc++));
			z8->cycles += 6;
		}
		break;
	case 0x04:		/* DA */
		/* TODO */
		z8->cycles += 8;
		break;
	case 0x05:		/* POP */
		setreg(z8, r, z8_pop8(z8));
		z8->cycles += 10;
		break;
	case 0x06:		/* COM */
		setreg(z8, r, z8_logic(z8, ~getreg(z8, r)));
		z8->cycles += 6;
		break;
	case 0x07:		/* PUSH */
		z8_push8(z8, getreg(z8, r));
		z8->cycles += 10;
		if (opl == 0x01)
			z8->cycles += 2;
		/* FIXME: double check push/pop timing */
		if (!(z8->reg[R_P01M] & 0x04))
			z8->cycles += 2;
		break;
	case 0x08:		/* DECW */
		r &= 0xFE;
		z8_idop16(z8, r, -1);
		z8->cycles += 10;
		break;
	case 0x09:		/* RL */
		v = getreg(z8, r);
		w = v << 1;
		w |= (v & 0x80) ? 0x01 : 0x00;
		if (v & 0x80)
			z8->reg[R_FLAGS] |= F_C;
		else
			z8->reg[R_FLAGS] &= ~F_C;
		setreg(z8, r, w);
		z8_logic(z8, w);
		z8->cycles += 6;
		break;
	case 0x0A:		/* INCW */
		r &= 0xFE;
		z8_idop16(z8, r, 1);
		z8->cycles += 10;
		break;
	case 0x0B:		/* CLR */
		setreg(z8, r, 0);
		z8->cycles += 6;
		break;
	case 0x0C:		/* RRC */
		v = getreg(z8, r);
		w = v >> 1;
		/* Go via carry (9bit rotate in effect) */
		w |= CARRY ? 0x80 : 0x00;
		if (v & 0x01)
			z8->reg[R_FLAGS] |= F_C;
		else
			z8->reg[R_FLAGS] &= ~F_C;
		setreg(z8, r, w);
		z8_logic(z8, w);
		z8->cycles += 6;
		break;
	case 0x0D:		/* SRA */
		v = getreg(z8, r);
		w = v >> 1;
		w |= (v & 0x80);
		if (v & 0x01)
			z8->reg[R_FLAGS] |= F_C;
		else
			z8->reg[R_FLAGS] &= ~F_C;
		setreg(z8, r, w);
		z8_logic(z8, w);
		z8->cycles += 6;
		break;
	case 0x0E:		/* RR */
		v = getreg(z8, r);
		w = v >> 1;
		/* RR is SRA except bit 0 goes into bit 7 instead of bit 7
		   being constant */
		w |= (v & 0x01) ? 0x80 : 0x00;
		if (v & 0x01)
			z8->reg[R_FLAGS] |= F_C;
		else
			z8->reg[R_FLAGS] &= ~F_C;
		setreg(z8, r, w);
		z8_logic(z8, w);
		z8->cycles += 6;
		break;
	case 0x0F:		/* SWAP */
		v = getreg(z8, r);
		v = (v >> 4) | (v << 4);
		setreg(z8, r, w);
		/* C and V are 'undefined'. As we don't know the algorithm the
		   CPU uses just do whatever */
		z8_logic(z8, w);
		z8->cycles += 8;
		break;
	}
}

static void rdecode0(struct z8 *z8)
{
	uint8_t r = z8_read_code(z8, z8->pc++);
	uint8_t r2;
	/* 6 cycles for 2 byte, 10 for 3 */

	z8->cycles += 6;
	if (z8->opl & 0x04) {
		z8->cycles += 4;
		r2 = z8_read_code(z8, z8->pc++);
	}

	switch(z8->opl) {
	case 2:		/* r r */
		z8->dreg = r >> 4;
		z8->arg0 = getwreg(z8, r >> 4);
		z8->arg1 = getwreg(z8, r & 0x0F);
		break;
	case 3:		/* r Ir */
		z8->dreg = r >> 4;
		z8->arg0 = getwreg(z8, r >> 4);
		z8->arg1 = getwreg(z8, r & 0x0F);
		break;
	case 4:		/* R R */
		z8->dreg = r2;
		z8->arg0 = getreg(z8, r2);
		z8->arg1 = getreg(z8, r);
		break;
	case 5:		/* R IR */
		z8->dreg = r2;
		z8->arg0 = getreg(z8, r2);
		z8->arg1 = getireg(z8, r);
		break;
	case 6:		/* R IM */
		z8->dreg = r;
		z8->arg0 = getreg(z8, r);
		z8->arg1 = r2;
		break;
	case 7:		/* IR IM */
		z8->dreg = r;
		z8->arg0 = getreg(z8, r);
		z8->arg1 = getireg(z8, r2);
		break;
	}
}

static void z8_execute_one(struct z8 *z8)
{
	uint8_t opcode = z8_read_code(z8, z8->pc++);
	uint_fast8_t oph = opcode >> 4;
	uint8_t r;
	uint16_t da;

	z8->opl = opcode & 0x0F;

	if (z8->opl & 0x08)
		z8_execute_group2(z8, oph, z8->opl);
	else if (z8->opl < 0x04)
		return z8_execute_group3(z8, oph, z8->opl);
	else {
		switch (oph) {
		case 0x00:	/* ADD group */
			rdecode0(z8);
			setreg(z8, z8->dreg,
			       z8_maths(z8, z8->arg0 + z8->arg1, 0));
			break;
		case 0x01:	/* ADC group */
			rdecode0(z8);
			setreg(z8, z8->dreg,
			       z8_maths(z8, z8->arg0 + z8->arg1 + CARRY,
					0));
			break;
		case 0x02:	/* SUB group */
			rdecode0(z8);
			setreg(z8, z8->dreg,
			       z8_maths(z8, z8->arg0 - z8->arg1, 1));
			break;
		case 0x03:	/* SBC group */
			rdecode0(z8);
			setreg(z8, z8->dreg,
			       z8_maths(z8, z8->arg0 - z8->arg1 - CARRY,
					1));
			break;
		case 0x04:	/* OR group */
			rdecode0(z8);
			setreg(z8, z8->dreg,
			       z8_logic(z8, z8->arg0 | z8->arg1));
			break;
		case 0x05:	/* AND group */
			rdecode0(z8);
			setreg(z8, z8->dreg,
			       z8_logic(z8, z8->arg0 & z8->arg1));
			break;
		case 0x06:	/* TCM group */
			rdecode0(z8);
			z8_logic(z8, ~z8->arg0 & z8->arg1);
			break;
		case 0x07:	/* TM group */
			rdecode0(z8);
			z8_logic(z8, z8->arg0 & z8->arg1);
			break;
		case 0x0A:	/* CP group */
			rdecode0(z8);
			z8_maths_noh(z8, z8->arg0 - z8->arg1);
			break;
		case 0x0B:	/* XOR group */
			rdecode0(z8);
			setreg(z8, z8->dreg,
			       z8_logic(z8, z8->arg0 & z8->arg1));
			break;
		case 0x0E:	/* LD group */
			rdecode0(z8);
			setreg(z8, z8->dreg, z8->arg1);
			break;
			/*
			 * The oddities
			 */
		case 0x08:
		case 0x09:
		case 0x0C:
		case 0x0D:
		case 0x0F:
			switch (opcode) {
			case 0x82:	/* LDE */
				r = z8_read_code(z8, z8->pc++);
				setwreg(z8, r >> 4,
				    z8_read_data(z8, getIrr(z8, r & 0xF)));
				z8->cycles += 12;
				break;
			case 0x83:	/* LDEI */
				/* TODO */
				z8->cycles += 18;
				break;
			case 0x92:	/* LDE */
				r = z8_read_code(z8, z8->pc++);
				z8_write_data(z8, getIrr(z8, r & 0x0F),
					      getwreg(z8, r >> 4));
				z8->cycles += 12;
				break;
			case 0x93:	/* LDEI */
				/* TODO */
				z8->cycles += 18;
				break;
			case 0xC2:	/* LDC */
				r = z8_read_code(z8, z8->pc++);
				setwreg(z8, r >> 4, 
					z8_read_code(z8, getIrr(z8, r & 0xF)));
				z8->cycles += 12;
				break;
			case 0xC3:	/* LDCI */
				/* TODO */
				z8->cycles += 18;
				break;
			case 0xC7:	/* LD r1,x.R2 */
				r = z8_read_code(z8, z8->pc++);
				setwreg(z8, r >> 4, getreg(z8,
						   getwreg(z8,
							   r & 0x0F) +
						   z8_read_code(z8,
								z8->pc++)));
				z8->cycles += 10;
				break;
			case 0xD2:	/* LDC */
				r = z8_read_code(z8, z8->pc++);
				z8_write_code(z8, getIrr(z8, r & 0x0F),
					      getwreg(z8, r >> 4));
				break;
			case 0xD3:	/* LDCI */
				/* TODO */
				z8->cycles += 18;
				break;
			case 0xD4:	/* CALL IRR1 */
				r = z8_read_code(z8, z8->pc++);
				z8_push16(z8, z8->pc);
				z8->pc = getIRR(z8, r);
				z8->cycles += 20;
				break;
			case 0xD6:	/* CALL da */
				da = z8_read_code(z8, z8->pc++);
				da <<= 8;
				da |= z8_read_code(z8, z8->pc++);
				z8_push16(z8, z8->pc);
				z8->pc = da;
				z8->cycles += 20;
				break;
			case 0xD7:	/* LD r2.x,R1 */
				r = z8_read_code(z8, z8->pc++);
				setreg(z8, (getwreg(z8, r & 0x0F) +
					 z8_read_code(z8, z8->pc++)),
					getwreg(z8, r >> 4));
				z8->cycles += 10;
				break;
			case 0xE3:	/* LD r1,Ir2 */
				r = z8_read_code(z8, z8->pc++);
				setireg(z8, r >> 4, getiwreg(z8, r & 0x0f));
				z8->cycles += 6;
				break;
			case 0xE4:	/* LD R2,R1 */
				r = z8_read_code(z8, z8->pc++);
				setreg(z8, z8_read_code(z8, z8->pc++),
					getreg(z8, r));
				z8->cycles += 10;
				break;
			case 0xE5:	/* LD IR2,R1 */
				r = z8_read_code(z8, z8->pc++);
				setreg(z8, z8_read_code(z8, z8->pc++),
					getireg(z8, r));
				z8->cycles += 10;
				break;
			case 0xE6:	/* LD R1,IM */
				r = z8_read_code(z8, z8->pc++);
				setreg(z8, r, z8_read_code(z8, z8->pc++));
				z8->cycles += 10;
				break;
			case 0xE7:	/* LD IR1,IM */
				r = z8_read_code(z8, z8->pc++);
				setireg(z8, r,
					 z8_read_code(z8, z8->pc++));
				z8->cycles += 10;
				break;
			case 0xF3:	/* LD Ir1,r2 */
				r = z8_read_code(z8, z8->pc++);
				setiwreg(z8, r >> 4, getwreg(z8, r & 0x0F));
				z8->cycles += 10;
				break;
			case 0xF5:	/* LD R2,IR1 */
				r = z8_read_code(z8, z8->pc++);
				setireg(z8, z8_read_code(z8, z8->pc++),
					getreg(z8, r));
				z8->cycles += 10;
				break;
			}
		}
	}
}

/*
 *	Emulate timers.
 *	TODO: emulate external clocks
 */
static void z8_clock_t0(struct z8 *z8)
{
	/* Stop at zero ? */
	if (!z8->t0 && !(z8->reg[R_PRE0] & 1))
		return;
	/* Not running */
	if (!(z8->reg[R_TMR] & 0x02))
		return;
	z8->t0--;
	if (z8->t0)
		return;
	if (z8->reg[R_TMR] & 0x01)
		z8->t1 = z8->reg[R_T1];
	z8_raise_irq(z8, 4);
}

static void z8_clock_t1(struct z8 *z8)
{
	/* Stop at zero ? */
	if (!z8->t1 && !(z8->reg[R_PRE1] & 1))
		return;
	/* Not running */
	if (!(z8->reg[R_TMR] & 0x08))
		return;
	z8->t1--;
	if (z8->t1)
		return;
	if (z8->reg[R_TMR] & 0x04)
		z8->t1 = z8->reg[R_T1];
	z8_raise_irq(z8, 5);
}

void z8_clocks(struct z8 *z8, int cycles)
{
	int n = cycles;
	/* Emulate the clock pre-scalers */
	int ps = z8->reg[R_PRE0] >> 2;
	while(z8->psc0 + n >= ps) {
		z8_clock_t0(z8);
		n -= ps;
	}
	z8->psc0 += n;

	/* FIXME: emulate counter chaining */

	n = cycles;
	/* T1 runs at CPU clk / 4 if it runs off the clock, so count
	   four times the prescalar */
	ps = z8->reg[R_PRE1] & 0xFC;
	while(z8->psc1 + n >= ps) {
		z8_clock_t1(z8);
		n -= ps;
	}
	z8->psc1 += n;
}

/* Run an instruction */
void z8_execute(struct z8 *z8)
{
	z8->cycles = 0;
	if (z8->reg[R_IMR] & 0x80) {
		if (z8->reg[R_IMR] & z8->reg[R_IRR] & 0x3F) {
			/* TODO IRQ cycle */
		}
	}
	z8_execute_one(z8);
	z8_clocks(z8, z8->cycles);
}

void z8_reset(struct z8 *z8)
{
	memset(z8, 0, sizeof(*z8));
	z8->pc = 12;
	z8->reg[R_P01M] = 0xA6;
}

struct z8 *z8_create(void)
{
	struct z8 *z8 = malloc(sizeof(struct z8));
	if (z8 == NULL) {
		fprintf(stderr, "z8_create: out of memory.\n");
		exit(1);
	}
	z8_reset(z8);
	return z8;
}

void z8_free(struct z8 *z8)
{
	free(z8);
}

void z8_raise_irq(struct z8 *z8, int irq)
{
	z8->reg[R_IRR] |= (1 << irq);
}

void z8_clear_irq(struct z8 *z8, int irq)
{
	z8->reg[R_IRR] &= ~(1 << irq);
}

void z8_rx_char(struct z8 *z8, uint8_t ch)
{
	if (z8->reg[R_P3M] & 0x40)
		z8->reg[R_SIO] = ch;
	z8_raise_irq(z8, 3);
}
