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
        case 4:		/* B! */
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
        /* I/O is a bit weird */
        if (reg < 8)
            cp1802_out(cpu, reg, cp1802_read(cpu, cpu->r[cpu->x]));
        else {
            /* FIXME: 68 does exactly what ? */
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
    case 10:	/* PLO */
        cpu->r[reg] &= 0xFF00;
        cpu->r[reg] |= cpu->d;
        break;
    case 11:	/* PHI */
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
    if (cpu->ie && cpu->ipend)
        interrupt(cpu);
    execute_op(cpu);
    return cpu->mcycles;
}

/* This is near enough right - D in fact isn not always 0 */
void cp1802_reset(struct cp1802 *cpu)
{
    memset(cpu, 0, sizeof(*cpu));
}
