/*
 *	6803 CPU emulation
 *
 *	This is a 6800 but with some timing differences and the following
 *	extras
 *
 *	ABX
 *	ADDD
 *	ASLD/LSLD
 *	BHS		(extra name for BCC)
 *	BLO		(extra name for BCS)
 *	BRN		(branch never)
 *	JSR direct
 *	LDD
 *	LSL		(actually ASL... same thing)
 *	LSRD
 *	MUL
 *	PSHX
 *	PULX
 *	STD
 *	SUBD
 *	CPX		(adds more conditions)
 *
 *	New interrupt vectors
 *	FFF8	IRQ1/IS3
 *	FFF6	ICF	}
 *	FFF4	OCF	}	IRQ2
 *	FFF2	TOF	}
 *	FFF0	SCI	}
 *
 *	A set of I/O space registers
 *
 *	We do not model most of the I/O and we don't model all the fill
 *	cycles fetching 0xFFFF etc as emulator callbacks. We also as a result
 *	don't model the fact timers are read mid elapsed instruction time.
 *
 *	6303 changes some timings so for multi-cpu we'd need some timing
 *	tables to replace the current constants (would fix 6800 too)
 *
 *	6301X/Y adds a second output comare, 8bit up counter and time
 *	constant register
 *
 *	TODO:
 *	6303 address error if execute 0000-001F
 *	
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "6803.h"

#define REG_D	((cpu->a << 8) | (cpu->b))
#define CARRY	(cpu->p & P_C)
#define HALFCARRY	(cpu->p & P_H)


/*
 *	Debug and trace support
 */

static char *m6803_flags(struct m6803 *cpu)
{
    static char buf[7];
    int i;

    for (i = 0; i < 6; i++) {
        if (cpu->p & (1 << i))
            buf[i] = "CVZNIH"[i];
        else
            buf[i] = '-';
    }
    return buf;
}

static void m6803_cpu_state(struct m6803 *cpu)
{
    fprintf(stderr, "%04X : %6s %02X|%02X %04X %04X | ",
        cpu->pc, m6803_flags(cpu), cpu->a, cpu->b, cpu->x, cpu->s);
}

static char *opmap[256] = {
    /* 0x00 */
    NULL,
    "NOP",
    NULL,
    NULL,

    "LSRD",
    "ASLD",
    "TAP",
    "TPA",

    "INX",
    "DEX",
    "CLV",
    "SEV",

    "CLC",
    "SEC",
    "CLI",
    "SEI",

    /* 0x10 */
    "SBA",
    "CBA",
    NULL,
    NULL,

    NULL,
    NULL,
    "TAB",
    "TBA",

    "XGDX",
    "DAA",
    NULL,
    "ABA",

    NULL,
    NULL,
    NULL,
    NULL,

    /* 0x20 */
    "BRA b",
    "BRN b",
    "BHI b",
    "BLS b",

    "BCC b",
    "BCS b",
    "BNE b",
    "BEQ b",

    "BVC b",
    "BVS b",
    "BPL b",
    "BMI b",

    "BGE b",
    "BLT b",
    "BGT b",
    "BLE b",

    /* 0x30 */
    "TSX",
    "INS",
    "PULA",
    "PULB",

    "DES",
    "TXS",
    "PSHA",
    "PSHB",

    "PULX",
    "RTS",
    "ABX",
    "RTI",

    "PSHX",
    "MUL",
    "WAI",
    "SWI",

    /* 0x40 */
    "NEGA",
    NULL,
    NULL,
    "COMA",

    "LSRA",
    NULL,
    "RORA",
    "ASRA",

    "ASLA",
    "ROLA",
    "DECA",
    NULL,

    "INCA",
    "TSTA",
    "T(HCF)",
    "CLRA",

    /* 0x50 */
    "NEGB",
    NULL,
    NULL,
    "COMB",

    "LSRB",
    NULL,
    "RORB",
    "ASRB",

    "ASLB",
    "ROLB",
    "DECB",
    NULL,

    "INCB",
    "TSTB",
    "T(HCF)",
    "CLRB",

    /* 0x60 */
    "NEG x",
    NULL,
    NULL,
    "COM x,X",

    "LSR x,X",
    NULL,
    "ROR x,X",
    "ASR x,X",

    "ASL x,X",
    "ROL x,X",
    "DEC x,X",
    NULL,

    "INC x,X",
    "TST x,X",
    "JMP x,X0",
    "CLR x,X",

    /* 0x70 */
    "NEG e",
    NULL,
    NULL,
    "COM e",

    "LSR e",
    NULL,
    "ROR e",
    "ASR e",

    "ASL e",
    "ROL e",
    "DEC e",
    NULL,

    "INC e",
    "TST e",
    "JMP e0",
    "CLR e",

    /* 0x80 */
    "SUBA #i",
    "CMPA #i",
    "SBCA #i",
    "SUBD #ii",

    "ANDA #i",
    "BITA #i",
    "LDAA #i",
    NULL,

    "EORA #i",
    "ADCA #i",
    "ORAA #i",
    "ADDA #i",

    "CPX #ii",
    "BSR b",
    "LDS #ii",
    NULL,

    /* 0x90 */
    "SUBA d",
    "CMPA d",
    "SBCA d",
    "SUBD d2",

    "ANDA d",
    "BITA d",
    "LDAA d",
    "STAA d",

    "EORA d",
    "ADCA d",
    "ORAA d",
    "ADDA d",

    "CPX d2",
    "JSR d0",
    "LDS d2",
    "STS d2",

    /* 0xA0 */
    "SUBA x,X",
    "CMPA x,X",
    "SBCA x,X",
    "SUBD x,X",

    "ANDA x,X",
    "BITA x,X",
    "LDAA x,X",
    "STAA x,X",

    "EORA x,X",
    "ADCA x,X",
    "ORAA x,X",
    "ADDA x,X",

    "CPX x,X2",
    "JSR x,X0",
    "LDS x,X2",
    "STS x,X2",

    /* 0xB0 */
    "SUBA e",
    "CMPA e",
    "SBCA e",
    "SUBD e",

    "ANDA e",
    "BITA e",
    "LDAA e",
    "STAA e",

    "EORA e",
    "ADCA e",
    "ORAA e",
    "ADDA e",

    "CPX e2",
    "JSR e0",
    "LDS e2",
    "STS e2",

    /* 0xC0 */
    "SUBB #i",
    "CMPB #i",
    "SBCB #i",
    "ADDD #ii",

    "ANDB #i",
    "BITB #i",
    "LDAB #i",
    NULL,

    "EORB #i",
    "ADCB #i",
    "ORAB #i",
    "ADDB #i",

    "LDD #ii",
    NULL,
    "LDX #ii",
    NULL,

    /* 0xD0 */
    "SUBB d",
    "CMPB d",
    "SBCB d",
    "ADDD d2",

    "ANDB d",
    "BITB d",
    "LDAB d",
    "STAB d",

    "EORB d",
    "ADCB d",
    "ORAB d",
    "ADDB d",

    "LDD d2",
    "STD d2",
    "LDX d2",
    "STX d2",

    /* 0xE0 */
    "SUBB x,X",
    "CMPB x,X",
    "SBCB x,X",
    "ADDD x,X",

    "ANDB x,X",
    "BITB x,X",
    "LDAB x,X",
    "STAB x,X",

    "EORB x,X",
    "ADCB x,X",
    "ORAB x,X",
    "ADDB x,X",

    "LDD x,X2",
    "STD x,X2",
    "LDX x,X2",
    "STX x,X2",

    /* 0xF0 */
    "SUBB e",
    "CMPB e",
    "SBCB e",
    "ADDD e",

    "ANDB e",
    "BITB e",
    "LDAB e",
    "STAB e",

    "EORB e",
    "ADCB e",
    "ORAB e",
    "ADDB e",

    "LDD e2",
    "STD e2",
    "LDX e2",
    "STX e2"
};

static void m6803_disassemble(struct m6803 *cpu, uint16_t pc)
{
    uint8_t op = m6803_read(cpu, pc++);
    uint16_t data;
    uint16_t addr;
    int pcontent = 0;
    int ppair = 0;
    char *x = opmap[op];

    m6803_cpu_state(cpu);

    if (x == NULL) {
        fprintf(stderr, "<ILLEGAL %02X>\n", op);
        return;
    }
    while(*x) {
        /* For double load/stores */
        if (*x == '2') {
            ppair = 1;
            x++;
            continue;
        }
        /* Supress data info for JMP/JSR etc */
        if (*x == '0') {
            pcontent = 0;
            x++;
            continue;
        }
        if (*x == 'd') {
            /* Direct */
            addr = m6803_do_debug_read(cpu, pc++);
            pcontent = 1;
            fprintf(stderr, "%02X", addr);
        } else if (*x == 'e') {
            /* Extended */
            addr = m6803_do_debug_read(cpu, pc++) << 8;
            addr |= m6803_do_debug_read(cpu, pc++);
            pcontent = 1;
            fprintf(stderr, "%04X", addr);
        } else if (*x == 'b') {
            /* Branch */
            data = m6803_do_debug_read(cpu, pc++);
            data = (int8_t)data + pc;
            pcontent = 1;
            fprintf(stderr, "%04X", data);
        } else if (*x == 'x') {
            /* Indexed */
            addr = m6803_do_debug_read(cpu, pc++);
            fprintf(stderr, "%02X", addr);
            addr += cpu->x;
            pcontent = 1;
        } else if (*x == 'i')
            fprintf(stderr, "%02X", m6803_do_debug_read(cpu, pc++));
        else if (*x == 'a')
            fprintf(stderr, " (%04X)", addr);
        else
            fputc(*x, stderr);
        x++;
    }
    /* Don't re-read stuff with side effects */
    /* FIXME: need a debug_read() so the emulator can also avoid this for
       MMIO */
    if (pcontent && addr > 0x1f) {
        if (ppair) {
            data = m6803_do_debug_read(cpu, addr++) << 8;
            data |= m6803_do_debug_read(cpu, addr);
            fprintf(stderr, " [%04X]", data);
        } else {
            fprintf(stderr, " [%02X]", m6803_do_debug_read(cpu, addr));
        }
    }
    fprintf(stderr, "\n");
}

/*
 *	The 6803 stack operations
 */

static void m6803_push(struct m6803 *cpu, uint8_t val)
{
    m6803_do_write(cpu, cpu->s--, val);
}

static void m6803_push16(struct m6803 *cpu, uint16_t val)
{
    m6803_push(cpu, val);
    m6803_push(cpu, val >> 8);
}

static uint8_t m6803_pull(struct m6803 *cpu)
{
    return m6803_do_read(cpu, ++cpu->s);
}

static uint16_t m6803_pull16(struct m6803 *cpu)
{
    uint16_t r = m6803_pull(cpu) << 8;
    r |= m6803_pull(cpu);
    return r;
}

static void m6803_push_interrupt(struct m6803 *cpu)
{
    m6803_push16(cpu, cpu->pc);
    m6803_push16(cpu, cpu->x);
    m6803_push(cpu, cpu->a);
    m6803_push(cpu, cpu->b);
    m6803_push(cpu, cpu->p);
}

static int m6803_vector(struct m6803 *cpu, uint16_t vector)
{
    m6803_push_interrupt(cpu);
    cpu->p |= P_I;
    /* What's the vector Victor ? */
    cpu->pc = m6803_do_read(cpu, vector) << 8;
    cpu->pc |= m6803_do_read(cpu, vector + 1);
    cpu->wait = 0;
    if (cpu->debug)
        fprintf(stderr, "*** Vector %04X\n", vector);
    return 12;
}

static int m6803_vector_masked(struct m6803 *cpu, uint16_t vector)
{
    cpu->wait  = 0;
    if (cpu->p & P_I)
        return 0;
    return m6803_vector(cpu, vector);
}

/*
 *	The different flag behaviours
 */

/* Set N and Z according to the result only */
static void m6803_flags_nz(struct m6803 *cpu, uint8_t r)
{
    cpu->p &= ~(P_Z|P_N);
    if (r == 0)
        cpu->p |= P_Z;
    if (r & 0x80)
        cpu->p |= P_N;
}

/* 8bit maths operation */
static uint8_t m6803_maths8(struct m6803 *cpu, uint8_t a, uint8_t b, uint8_t r)
{
    cpu->p &= ~(P_H|P_N|P_Z|P_V|P_C);
    if (r & 0x80)
        cpu->p |= P_N;
    if (r == 0)
        cpu->p |= P_Z;
    if ((a & b & 0x80) && !(r & 0x80))
        cpu->p |= P_V;
    if (!((a | b) & 0x80) && (r & 0x80))
        cpu->p |= P_V;
    if (~a & b & 0x80)
        cpu->p |= P_C;
    if (b & r & 0x80)
        cpu->p |= P_C;
    if (~a & r & 0x80)
        cpu->p |= P_C;
    /* And half carry for DAA */
    if ((a & b & 0x08) || ((b & ~r) & 0x08) || ((a & ~r) & 0x08))
        cpu->p |= P_H;
    return r;
}

/* 8bit maths operation without half carry - used by most instructions
   only ADC/ADD/ABA support H/C */
static uint8_t m6803_maths8_noh(struct m6803 *cpu, uint8_t a, uint8_t b, uint8_t r)
{
    cpu->p &= ~(P_N|P_Z|P_V|P_C);
    if (r & 0x80)
        cpu->p |= P_N;
    if (r == 0)
        cpu->p |= P_Z;
    if ((a & b & 0x80) && !(r & 0x80))
        cpu->p |= P_V;
    if (!((a | b) & 0x80) && (r & 0x80))
        cpu->p |= P_V;
    if (~a & b & 0x80)
        cpu->p |= P_C;
    if (b & r & 0x80)
        cpu->p |= P_C;
    if (~a & r & 0x80)
        cpu->p |= P_C;
    return r;
}

/* 8bit logic */
static void m6803_logic8(struct m6803 *cpu, uint8_t r)
{
    m6803_flags_nz(cpu, r);
    cpu->p &= ~P_V;
}

/* 8bit shifts */
static void m6803_shift8(struct m6803 *cpu, uint8_t r, int c)
{
    m6803_flags_nz(cpu, r);
    cpu->p &= ~(P_C | P_V);
    if (c)
        cpu->p |= P_C;
    /* Grumble - C needs a ^^ operator */
    if (c && !(cpu->p & P_N))
        cpu->p |= P_V;
    if (!c && (cpu->p & P_N))
        cpu->p |= P_V;
}

/* 16bit maths like this does affect N and V but not necessarily usefully.
   However the behaviour is documented */
static uint16_t m6803_maths16_noh(struct m6803 *cpu, uint16_t a, uint16_t b, uint16_t r)
{
    cpu->p &= ~(P_C|P_Z|P_N);
    if (r == 0)
        cpu->p |= P_Z;
    if (r & 0x8000)
        cpu->p |= P_N;
    if ((a & b & 0x8000) && !(r & 0x8000))
        cpu->p |= P_V;
    if (!((a | b) & 0x8000) && (r & 0x8000))
        cpu->p |= P_V;
    if (~a & b & 0x8000)
        cpu->p |= P_C;
    if (b & r & 0x8000)
        cpu->p |= P_C;
    if (~a & r & 0x8000)
        cpu->p |= P_C;
    return r;
}

/* 16bit logic */
static void m6803_logic16(struct m6803 *cpu, uint16_t r)
{
    cpu->p &= ~(P_Z|P_N);
    if (r == 0)
        cpu->p |= P_Z;
    if (r & 0x8000)
        cpu->p |= P_N;
    cpu->p &= ~P_V;
}

/* 16bit shift : based on 6303 document */
static void m6803_shift16(struct m6803 *cpu, int c)
{
    uint16_t d = REG_D;
    cpu->p &= ~(P_Z|P_N|P_V|P_C);
    if (d == 0)
        cpu->p |= P_Z;
    if (cpu->a & 0x80)
        cpu->p |= P_N;
    if (c)
        cpu->p |= P_C;
    if (((cpu->p & (P_C|P_N)) == P_C) || ((cpu->p & (P_C|P_N)) == P_N))
        cpu->p |= P_V;
}

static void m6803_bra(struct m6803 *cpu, uint8_t data8, uint8_t taken)
{
    if (taken)
        cpu->pc += (int8_t)data8;
}

static void m6803_hcf(struct m6803 *cpu)
{
    fprintf(stderr, "HCF executed at %04X\n", cpu->pc - 1);
    exit(1);
}

static uint8_t m6803_execute_one(struct m6803 *cpu)
{
    uint16_t fetch_pc = cpu->pc;
    uint8_t opcode = m6803_do_read(cpu, cpu->pc);
    uint8_t data8, tmp8;
    uint16_t data16, tmp16;
    uint8_t tmpc, tmp2;
    uint8_t add;

    if (cpu->debug)
        m6803_disassemble(cpu, cpu->pc);

    cpu->pc++;

    /* Fetch address/data for non immediate opcodes */
    switch(opcode & 0xF0) {
        case 0x80:	/* Immediate 8/16bit */
        case 0xC0:	/* Immediate 8/16bit */
            /* FIXME: 8D is weird, CD undefined - how does CD really work ? */
            if (opcode != 0x8D && ((opcode & 0x0F) >= 0x0C || (opcode & 0x0F) == 3)) {
                data16 = m6803_do_read(cpu, cpu->pc++) << 8; 
                data16 |= m6803_do_read(cpu, cpu->pc++);
                break;
            }
        case 0x20:	/* Branches */
        case 0x90:	/* Direct */
        case 0xD0:	/* Direct */
            data8 = m6803_do_read(cpu, cpu->pc++);
            break;
        case 0x60:	/* Indexed */
        case 0xA0:	/* Indexed */
        case 0xE0:	/* Indexed */
            /* Save the first byte for the strange 6303 logic immediate ops */
            data8 = m6803_do_read(cpu, cpu->pc++);
            data16 = data8 + cpu->x;
            break;
        case 0x70:	/* Extended */
        case 0xB0:	/* Extended */
        case 0xF0:	/* Extended */
            /* Ordering ? */
            data16 = m6803_do_read(cpu, cpu->pc++) << 8; 
            data16 |= m6803_do_read(cpu, cpu->pc++);
            break;
    }
    switch(opcode) {
    case 0x01:	/* NOP */
        /* No flags */
        return 2;
    case 0x04: /* LSRD */
        tmpc = cpu->b & 1;
        cpu->b >>= 1;
        if (cpu->a & 0x01)
            cpu->b |= 0x80;
        cpu->a >>= 1;
        m6803_shift16(cpu, tmpc);
        return 3;
    case 0x05: /* ASLD */
        tmpc = cpu->a & 0x80;
        cpu->a <<= 1;
        if (cpu->b & 0x80)
            cpu->a |= 0x01;
        cpu->b <<= 1;
        m6803_shift16(cpu, tmpc);
        return 3;
    case 0x06: /* TAP */
        /* FIXME: If I read the flowchart right then a pending interrupt
           other than NMI does not occur the instruction after TAP (it acts
           as if I was set for one instruction regardless of prev/new state) */
        cpu->p = cpu->a | 0xC0;
        return 2;
    case 0x07: /* TPA */    
        cpu->a = cpu->p | 0xC0;
        return 2;
    case 0x08:	/* INX */
        cpu->x++;
        cpu->p &= ~P_Z;
        if (cpu->x == 0)
            cpu->p |= P_Z;
        return 3;
    case 0x09:	/* DEX */
        cpu->x--;
        cpu->p &= ~P_Z;
        if (cpu->x == 0)
            cpu->p |= P_Z;
        return 3;
    case 0x0A:	/* CLV */
        cpu->p &= ~P_V;
        return 2;
    case 0x0B:	/* SEV */
        cpu->p |= P_V;
        return 2;
    case 0x0C:	/* CLC */
        cpu->p &= ~P_C;
        return 2;
    case 0x0D:	/* SEC */
        cpu->p |= P_C;
        return 2;
    case 0x0E:	/* CLI */
        cpu->p &= ~P_I;
        return 2;
    case 0x0F:	/* SEI */
        cpu->p |= P_I;
        return 2;
    case 0x10:	/* SBA */
        cpu->a = m6803_maths8(cpu, cpu->a, cpu->b, cpu->a - cpu->b);
        return 2;
    case 0x11:	/* CBA */
        m6803_maths8(cpu, cpu->a, cpu->b, cpu->a - cpu->b);
        return 2;
    case 0x16:	/* TAB */
        cpu->b = cpu->a;
        m6803_logic8(cpu, cpu->b);
        return 2;
    case 0x17:	/* TBA */
        cpu->a = cpu->b;
        m6803_logic8(cpu, cpu->a);
        return 2;
    case 0x18:  /* XGDX (6303) */
        if (cpu->type == 6303) {
            tmp16 = cpu->x;
            cpu->x = (cpu->a << 8) | cpu->b;
            cpu->a = tmp16 >> 8;
            cpu->b = tmp16;
            return 2;
        }
        break;
    case 0x19:	/* DAA */
        /* The algorithm for this is complicated but precisely described
           except for what happens to V. 6303 says V is not affected */
        tmp8 = cpu->a >> 4;
        tmp2 = cpu->a & 0x0F;
        add = 0;
        tmpc = 0;
        if (!(cpu->p & P_C)) {
            if (tmp8 < 0x09 && tmp2 >= 0x0A && !(cpu->p & P_H))
                add = 0x06;
            if (tmp8 <= 0x09 && tmp2 <= 0x03 && (cpu->p & P_H))
                add = 0x06;
            if (tmp8 >= 0x0A && tmp2 <= 0x09 && !(cpu->p & P_H)) {
                tmpc = 1;
                add = 0x60;
            }
            if (tmp8 >= 0x09 && tmp2 >= 0x0A && !(cpu->p & P_H)) {
                tmpc = 1;
                add = 0x66;
            }
            if (tmp8 >= 0x0A && tmp2 <= 0x03 && (cpu->p & P_H)) {
                tmpc = 1;
                add = 0x66;
            }
        } else {
            if (tmp8 <= 0x02 && tmp2 <= 0x09 && !(cpu->p & P_H)) {
                tmpc = 1;
                add = 0x60;
            }
            if (tmp8 <= 0x02 && tmp2 >= 0x0A && !(cpu->p & P_H)) {
                tmpc = 1;
                add = 0x66;
            }
            if (tmp8 <= 0x03 && tmp2 <= 0x03 && (cpu->p & P_H)) {
                tmpc = 1;
                add =- 0x66;
            }
        }
        cpu->a += add;
        m6803_flags_nz(cpu, cpu->a);
        cpu->p &= ~P_C;
        if (tmpc)
            cpu->p |= P_C;
        /* FIXME: V is "not defined" on 6803 */
        return 2;
    case 0x1A:	/* SLP (6303) */
        if (cpu->type == 6303) {
            cpu->wait = 1;
            return 4;
        }
        break;
    case 0x1B:	/* ABA */
        cpu->a = m6803_maths8(cpu, cpu->a, cpu->b, cpu->a + cpu->b);
        return 2;
    /* 2x is branches : 3 or 4 cycle check - 4 on 6800, 3 on 6803 ? */
    case 0x20:	/* BRA */
        m6803_bra(cpu, data8, 1);
        return 3;
    case 0x21:	/* BRN */
        m6803_bra(cpu, data8, 0);
        return 3;
    case 0x22:	/* BHI */
        m6803_bra(cpu, data8, !(cpu->p & (P_C|P_Z)));
        return 3;
    case 0x23:	/* BLS */
        m6803_bra(cpu, data8, cpu->p & (P_C|P_Z));
        return 3;
    case 0x24:	/* BCC */
        m6803_bra(cpu, data8, !(cpu->p & P_C));
        return 3;
    case 0x25:	/* BCS */
        m6803_bra(cpu, data8, cpu->p & P_C);
        return 3;
    case 0x26:	/* BNE */
        m6803_bra(cpu, data8, !(cpu->p & P_Z));
        return 3;
    case 0x27:	/* BEQ */
        m6803_bra(cpu, data8, cpu->p & P_Z);
        return 3;
    case 0x28:	/* BVC */
        m6803_bra(cpu, data8, !(cpu->p & P_V));
        return 3;
    case 0x29:	/* BVS */
        m6803_bra(cpu, data8, cpu->p & P_V);
        return 3;
    case 0x2A:	/* BPL */
        m6803_bra(cpu, data8, !(cpu->p & P_N));
        return 3;
    case 0x2B:	/* BMI */
        m6803_bra(cpu, data8, cpu->p & P_N);
        return 3;
    case 0x2C:	/* BGE */
        /* no ^^ in C */
        tmp8 = cpu->p & (P_N|P_V);
        if(tmp8 == (P_N|P_V))
            tmp8 = 0;
        m6803_bra(cpu, data8, !tmp8);
        return 3;
    case 0x2D:	/* BLT : yum yum */
        tmp8 = cpu->p & (P_N|P_V);
        if(tmp8 == (P_N|P_V))
            tmp8 = 0;
        m6803_bra(cpu, data8, tmp8);
        return 3;
    case 0x2E:	/* BGT */
        tmp8 = cpu->p & (P_N|P_V);
        if(tmp8 == (P_N|P_V))
            tmp8 = 0;
        if (cpu->p & P_Z)
            tmp8 = 0;
        m6803_bra(cpu, data8, tmp8);
        return 3;
    case 0x2F:	/* BLE */
        tmp8 = cpu->p & (P_N|P_V);
        if(tmp8 == (P_N|P_V))
            tmp8 = 0;
        if (cpu->p & P_Z)
            tmp8 = 0;
        m6803_bra(cpu, data8, !tmp8);
        return 3;
    /* 3x is stack stuff mostly */
    case 0x30:	/* TSX */
        cpu->x = cpu->s;
        /* No flags */
        return 3;		/* 4 on 6800 ? */
    case 0x31:	/* INS */
        cpu->s++;
        /* No flags */
        return 3;
    case 0x32:	/* PULA */
        cpu->a = m6803_pull(cpu);
        /* No flags */
        return 4;
    case 0x33:	/* PULB */
        cpu->b = m6803_pull(cpu);
        /* No flags */
        return 4;
    case 0x34:	/* DES */
        cpu->s--;
        /* No flags */
        return 3;
    case 0x35:	/* TXS */
        cpu->s = cpu->x;
        /* No flags */
        return 3;		/* 4 on 6800 ? */
    case 0x36:	/* PSHA */
        m6803_push(cpu, cpu->a);
        /* No flags */
        return 3;	/* 3 or 4 - 4 on 6800 ? */
    case 0x37:	/* PSHB */
        m6803_push(cpu, cpu->b);
        /* No flags */
        return 3;
    case 0x38:	/* PULX */
        cpu->x = m6803_pull16(cpu);
        /* No flags */
        return 5;
    case 0x39:	/* RTS */
        cpu->pc = m6803_pull16(cpu);
        /* No flags */
        return 5;
    case 0x3A:	/* ABX */
        cpu->x += cpu->b;
        /* No flags */
        return 3;
    case 0x3B:	/* RTI */
        cpu->p = m6803_pull(cpu);
        cpu->b = m6803_pull(cpu);
        cpu->a = m6803_pull(cpu);
        cpu->x = m6803_pull16(cpu);
        cpu->pc = m6803_pull16(cpu);
        return 10;
    case 0x3C:	/* PSHX */
        m6803_push16(cpu, cpu->x);
        /* No flags */
        return 4;
    case 0x3D:	/* MUL */
        tmp16 = cpu->a * cpu->b;
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        cpu->p &= ~P_C;
        /* This is bizzare but what the 6303 manual says - the 6803 one
           doesn't really help */
        if (cpu->b & 0x80)
            cpu->p |= P_C;
        return 10;
    case 0x3E:	/* WAI */
        m6803_push_interrupt(cpu);
        /* Note we are in wait */
        cpu->wait = 1;
        return 9;
    case 0x3F:	/* SWI */
        m6803_push_interrupt(cpu);
        cpu->p |= P_I;
        cpu->pc = m6803_do_read(cpu, 0xFFFB);
        cpu->pc |= m6803_do_read(cpu, 0xFFFA) << 8;
        return 12;
    /* Implicit logic on A */
    case 0x40:	/* NEGA */
        cpu->a = m6803_maths8_noh(cpu, 0, cpu->a, -cpu->a);
        return 2;
    case 0x43:	/* COMA */
        cpu->a = ~cpu->a;
        m6803_logic8(cpu, cpu->a);
        cpu->p |= P_C;
        return 2;
    case 0x44:	/* LSRA */
        tmp8 = cpu->a & 0x01;
        cpu->a >>= 1;
        m6803_shift8(cpu, cpu->a, tmp8);
        return 2;
    case 0x46:	/* RORA */
        tmpc = cpu->a & 0x01;
        cpu->a >>= 1;
        if (CARRY)
            cpu->a |= 0x80;
        m6803_shift8(cpu, cpu->a, tmpc);
        return 2;
    case 0x47:	/* ASRA */
        tmp8 = cpu->a & 0x01;
        if (cpu->a & 0x80)
            cpu->a = (cpu->a >> 1) | 0x80;
        else
            cpu->a >>= 1;
        m6803_shift8(cpu, cpu->a, tmp8);
        return 2;
    case 0x48:	/* ASLA */
        tmp8 = cpu->a & 0x80;	/* Carry bit */
        cpu->a <<= 1;
        m6803_shift8(cpu, cpu->a, tmp8);
        return 2;
    case 0x49:	/* ROLA */
        tmp8 = cpu->a & 0x80;
        cpu->a = (cpu->a << 1) | CARRY;
        m6803_shift8(cpu, cpu->a, tmp8);
        return 2;
    case 0x4A: 	/* DECA */
        /* Weird as the don't affect C */
        cpu->a--;
        m6803_logic8(cpu, cpu->a);
        if (cpu->a == 0x7F)	/* DEC from 0x80) */
            cpu->p |= P_V;
        return 2;
    case 0x4C:	/* INCA */
        cpu->a++;
        m6803_logic8(cpu, cpu->a);
        if (cpu->a == 0x80)	/* INC from 0x7F) */
            cpu->p |= P_V;
        return 2;
    case 0x4D:	/* TSTA */
        m6803_logic8(cpu, cpu->a);
        cpu->p &= ~P_C;
        return 2;
    case 0x4E:	/* Halt and catch fire */
        m6803_hcf(cpu);
        return 2;
    case 0x4F:	/* CLRA */
        cpu->a = 0;
        cpu->p &= ~(P_N|P_V|P_C);
        cpu->p |= P_Z;
        return 2;
    /* Same as 0x4x but with B */
    case 0x50:	/* NEGB */
        cpu->b = m6803_maths8_noh(cpu, 0, cpu->b, -cpu->b);
        return 2;
    case 0x53:	/* COMB */
        cpu->b = ~cpu->b;
        m6803_logic8(cpu, cpu->b);
        cpu->p |= P_C;
        return 2;
    case 0x54:	/* LSRB */
        tmp8 = cpu->b & 1;
        cpu->b >>= 1;
        m6803_shift8(cpu, cpu->b, tmp8);
        return 2;
    case 0x56:	/* RORB */
        tmpc = cpu->b & 0x01;
        cpu->b >>= 1;
        if (CARRY)
            cpu->b |= 0x80;
        m6803_shift8(cpu, cpu->b, tmpc);
        return 2;
    case 0x57:	/* ASRB */
        tmp8 = cpu->b & 0x01;
        if (cpu->b & 0x80)
            cpu->b = (cpu->b >> 1) | 0x80;
        else
            cpu->b >>= 1;
        m6803_shift8(cpu, cpu->b, tmp8);
        return 2;
    case 0x58:	/* ASLB */
        tmp8 = cpu->b & 0x80;
        cpu->b <<= 1;
        m6803_shift8(cpu, cpu->b, tmp8);
        return 2;
    case 0x59:	/* ROLB */
        tmp8 = cpu->b & 0x80;
        cpu->b = (cpu->b << 1) | CARRY;
        m6803_shift8(cpu, cpu->b, tmp8);
        return 2;
    case 0x5A: 	/* DECB */
        /* Weird as the don't affect C */
        cpu->b--;
        m6803_logic8(cpu, cpu->b);
        if (cpu->b == 0x7F)	/* DEC from 0x80) */
            cpu->p |= P_V;
        return 2;
        return 2;
    case 0x5C:	/* INCB */
        cpu->b++;
        m6803_logic8(cpu, cpu->b);
        if (cpu->b == 0x80)	/* INC from 0x7F) */
            cpu->p |= P_V;
        return 2;
    case 0x5D:	/* TSTB */
        m6803_logic8(cpu, cpu->b);
        cpu->p &= ~P_C;
        return 2;
    case 0x5E:	/* Halt and catch fire */
        m6803_hcf(cpu);
        return 2;
    case 0x5F:	/* CLRB */
        cpu->b = 0;
        cpu->p &= ~(P_N|P_V|P_C);
        cpu->p |= P_Z;
        return 2;
    /* As 0x4x but indexed and includes jumps */
    /* And 0x70 is the same but extended */
    case 0x60:	/* NEG ,X */
    case 0x70:	/* NEG addr */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_do_write(cpu, data16, m6803_maths8_noh(cpu, 0, tmp8, -tmp8));
        return 6;
    case 0x61:	/* AIM direct (6303) and also BCLR */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            data16 = cpu->x + m6803_do_read(cpu, cpu->pc++);
            tmp8 = m6803_do_read(cpu, data16);
            tmp8 &= data8;
            m6803_do_write(cpu, data16 & 0xFF, tmp8);
            m6803_logic8(cpu, tmp8);
            return 6;
        }
        break;
    case 0x71: /* AIM ,X (6303) and also BCLR */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            tmp8 = m6803_do_read(cpu, data16 & 0xFF);
            tmp8 &= data16 >> 8;
            m6803_do_write(cpu, data16 & 0xFF, tmp8);
            m6803_logic8(cpu, tmp8);
            return 6;
        }
        break;
    case 0x62:	/* OIM direct (6303) and also BSET */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            data16 = cpu->x + m6803_do_read(cpu, cpu->pc++);
            tmp8 = m6803_do_read(cpu, data16);
            tmp8 |= data8;
            m6803_do_write(cpu, data16 & 0xFF, tmp8);
            m6803_logic8(cpu, tmp8);
            return 6;
        }
        break;
    case 0x72: /* OIM ,X (6303) and also BSET */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            tmp8 = m6803_do_read(cpu, data16 & 0xFF);
            tmp8 |= data16 >> 8;
            m6803_do_write(cpu, data16 & 0xFF, tmp8);
            m6803_logic8(cpu, tmp8);
            return 6;
        }
        break;
    case 0x63:	/* COM ,X */
    case 0x73:	/* COM addr */
        tmp8 = ~m6803_do_read(cpu, data16);
        m6803_do_write(cpu, data16, tmp8);
        m6803_logic8(cpu, tmp8);
        cpu->p |= P_C;
        return 6;
    case 0x64:	/* LSR ,X */
    case 0x74:	/* LSR addr */
        tmp8 = m6803_do_read(cpu, data16);
        tmpc = tmp8 & 0x01;
        tmp8 >>= 1;
        m6803_do_write(cpu, data16, tmp8);
        m6803_shift8(cpu, tmp8, tmpc);
        return 6;
    case 0x65:	/* EIM direct (6303) and also BTGL */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            data16 = cpu->x + m6803_do_read(cpu, cpu->pc++);
            tmp8 = m6803_do_read(cpu, data16);
            tmp8 ^= data8;
            m6803_do_write(cpu, data16 & 0xFF, tmp8);
            m6803_logic8(cpu, tmp8);
            return 6;
        }
        break;
    case 0x75: /* EIM ,X (6303) and also BTGL */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            tmp8 = m6803_do_read(cpu, data16 & 0xFF);
            tmp8 = data16 >> 8;
            m6803_do_write(cpu, data16 & 0xFF, tmp8);
            m6803_logic8(cpu, tmp8);
            return 6;
        }
        break;
    case 0x66:	/* ROR ,X */
    case 0x76:	/* ROR addr */
        tmp8 = m6803_do_read(cpu, data16);
        tmpc = tmp8 & 0x01;
        tmp8 >>= 1;
        if (CARRY)
            tmp8 |= 0x80;
        m6803_shift8(cpu, tmp8, tmpc);
        m6803_do_write(cpu, data16, tmp8);
        return 6;
    case 0x67:	/* ASR ,X */
    case 0x77:	/* ASR addr */
        tmp8 = m6803_do_read(cpu, data16);
        tmpc = tmp8 & 0x01;
        if (tmp8 & 0x80)
            tmp8 = (tmp8 >> 1) | 0x80;
        else
            tmp8 >>= 1;
        m6803_shift8(cpu, tmp8, tmpc);
        m6803_do_write(cpu, data16, tmp8);
        return 6;
    case 0x68:	/* ASL ,X */
    case 0x78:	/* ASL addr */
        tmp16 = m6803_do_read(cpu, data16);
        tmp16 <<= 1;
        m6803_shift8(cpu, tmp16 & 0xFF, tmp16 & 0x100);
        m6803_do_write(cpu, data16, (uint8_t)tmp16);
        return 6;
    case 0x69:	/* ROL ,X */
    case 0x79:	/* ROL addr */
        tmp8 = m6803_do_read(cpu, data16);
        tmpc = tmp8 & 0x80;
        tmp8 = (tmp8 << 1) | CARRY;
        m6803_do_write(cpu, data16, tmp8);
        m6803_shift8(cpu, tmp8, tmpc);
        return 6;
    case 0x6A: 	/* DEC ,X */
    case 0x7A:	/* DEC addr */
        /* Weird as the don't affect C */
        tmp8 = m6803_do_read(cpu, data16) - 1;
        m6803_logic8(cpu, tmp8);
        if (tmp8 == 0x7F)	/* DEC from 0x80) */
            cpu->p |= P_V;
        m6803_do_write(cpu, data16, tmp8);
        return 6;
    case 0x6B:	/* BTST direct (6303) and TIM */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            data16 = cpu->x + m6803_do_read(cpu, cpu->pc++);
            tmp8 = m6803_do_read(cpu, data16);
            tmp8 &= data8;
            m6803_logic8(cpu, tmp8);
            return 4;
        }
        break;
    case 0x7B: /* BTST ,X (6303) and TIM */
        /* These have strange encodings of the data */
        if (cpu->type == 6303) {
            tmp8 = m6803_do_read(cpu, data16 & 0xFF);
            tmp8 &= data16 >> 8;
            m6803_logic8(cpu, tmp8);
            return 4;
        }
        break;
    case 0x6C:	/* INC ,X */
    case 0x7C:	/* INC addr */
        tmp8 = m6803_do_read(cpu, data16);
        tmp8++;
        m6803_logic8(cpu, tmp8);
        if (tmp8 == 0x80)	/* INC from 0x7F) */
            cpu->p |= P_V;
        m6803_do_write(cpu, data16, tmp8);
        return 6;
    case 0x6D:	/* TST ,X */
    case 0x7D:	/* TST addr */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_logic8(cpu, tmp8);
        cpu->p &= ~P_C;
        return 6;
    case 0x6E:	/* JMP ,X */
    case 0x7E:	/* JMP addr */
        cpu->pc = data16;
        /* No flags */
        return 6;
    case 0x6F:	/* CLR ,X */
    case 0x7F:	/* CLR addr */
        m6803_do_write(cpu, data16, 0);
        cpu->p &= ~(P_N|P_V|P_C);
        cpu->p |= P_Z;
        return 2;
    /* Logical operation blocks these almost make sense but there are oddities */
    /* 0x8X immediate operations on A */
    case 0x80:	/* SUBA immed */
        cpu->a = m6803_maths8_noh(cpu, cpu->a, data8, cpu->a - data8);
        return 2;
    case 0x81:	/* CMPA immed */
        m6803_maths8_noh(cpu, cpu->a, data8, cpu->a - data8);
        return 2;
    case 0x82:	/* SBCA immed */
        m6803_maths8_noh(cpu, cpu->a, data8, cpu->a - data8 - CARRY);
        return 2;
    case 0x83:	/* SUBD immed16 */
        /* FIXME: flags */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D - data16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 4;
    case 0x84:	/* ANDA immed */
        cpu->a &= data8;
        m6803_logic8(cpu, cpu->a);
        return 2;
    case 0x85:	/* BITA */
        m6803_logic8(cpu, cpu->a & data8);
        return 2;
    case 0x86:	/* LDAA */
        cpu->a = data8;
        m6803_logic8(cpu, cpu->a);
        return 2;
    /* No 0x87 store immediate */
    case 0x88:	/* EORA */
        cpu->a ^= data8;
        m6803_logic8(cpu, cpu->a);
        return 2;
    case 0x89:	/* ADCA */
        cpu->a = m6803_maths8(cpu, cpu->a, data8, cpu->a + data8 + CARRY);
        return 2;
    case 0x8A:	/* ORAA */
        cpu->a |= data8;
        m6803_logic8(cpu, cpu->a);
        return 2;
    case 0x8B:	/* ADDA */
        cpu->a = m6803_maths8(cpu, cpu->a, data8, cpu->a + data8);
        return 2;
    case 0x8C:	/* CPX */
        /* Is this right - on 6800 it's only Z less clear on 6803 */
        m6803_maths16_noh(cpu, cpu->x, data16, cpu->x - data16);
        return 4;
    case 0x8D:	/* BSR */
        m6803_push16(cpu, cpu->pc);
        cpu->pc += (int8_t)data8;
        /* No flags */
        return 6;
    case 0x8E:	/* LDS */
        cpu->s = data16;
        /* Weirdly LDS *does* affect flags */
        m6803_logic16(cpu, cpu->s);
        return 3;
    /* 0x8F would be SDS immediate which is meaningless */
    /* Same again for direct */
    case 0x90:	/* SUBA dir */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->a = m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
        return 3;
    case 0x91:	/* CMPA dir */
        tmp8 = m6803_do_read(cpu, data8);
        m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
        return 3;
    case 0x92:	/* SBCA dir */
        tmp8 = m6803_do_read(cpu, data8);
        m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8 - CARRY);
        return 3;
    case 0x93:	/* SUBD dir */
        tmp16 = m6803_do_read(cpu, data8) << 8;
        tmp16 |= m6803_do_read(cpu, data8 + 1);
        /* FIXME: flags */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D - tmp16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 5;
    case 0x94:	/* ANDA dir */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->a &= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 3;
    case 0x95:	/* BITA */
        tmp8 = m6803_do_read(cpu, data8);
        m6803_logic8(cpu, tmp8);
        return 3;
    case 0x96:	/* LDAA */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->a = tmp8;
        m6803_logic8(cpu, cpu->a);
        return 3;
    case 0x97:	/* STAA */
        m6803_do_write(cpu, data8, cpu->a);
        m6803_logic8(cpu, cpu->a);
        return 3;
    case 0x98:	/* EORA */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->a ^= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 3;
    case 0x99:	/* ADCA */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->a = m6803_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8 + CARRY);
        return 3;
    case 0x9A:	/* ORAA */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->a |= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 3;
    case 0x9B:	/* ADDA */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->a = m6803_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8);
        return 3;
    case 0x9C:	/* CPX */
        tmp16 = m6803_do_read(cpu, data8) << 8;
        tmp16 |= m6803_do_read(cpu, data8 + 1);
        m6803_maths16_noh(cpu, cpu->x, data16, cpu->x - data16);
        return 5;
    case 0x9D:	/* JSR */
        m6803_push16(cpu, cpu->pc);
        cpu->pc = data16;
        /* No flags */
        return 5;
    case 0x9E:	/* LDS */
        tmp16 = m6803_do_read(cpu, data8) << 8;
        tmp16 |= m6803_do_read(cpu, data8 + 1);
        /* Weirdly LDS *does* affect flags */
        cpu->s = tmp16;
        m6803_logic16(cpu, cpu->s);
        return 4;
    case 0x9F:	/* STS */
        m6803_do_write(cpu, data8, cpu->s >> 8);
        m6803_do_write(cpu, data8 + 1, cpu->s);	/* Do these wrap ?? */
        m6803_logic16(cpu, cpu->s);
        return 4;
    /* 0xAX: indexed version */
    case 0xA0:	/* SUBA indexed */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
        return 4;
    case 0xA1:	/* CMPA indexed */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
        return 4;
    case 0xA2:	/* SBCA indexed */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8 - CARRY);
        return 4;
    case 0xA3:	/* SUBD indexed */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        /* FIXME: flags */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D - tmp16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 6;
    case 0xA4:	/* ANDA indexed */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a &= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xA5:	/* BITA */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_logic8(cpu, tmp8);
        return 4;
    case 0xA6:	/* LDAA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xA7:	/* STAA */
        m6803_do_write(cpu, data16, cpu->a);
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xA8:	/* EORA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a ^= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xA9:	/* ADCA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = m6803_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8 + CARRY);
        return 4;
    case 0xAA:	/* ORAA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a |= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xAB:	/* ADDA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = m6803_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8);
        return 4;
    case 0xAC:	/* CPX */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        m6803_maths16_noh(cpu, cpu->x, data16, cpu->x - data16);
        return 6;
    case 0xAD:	/* JSR */
        m6803_push16(cpu, cpu->pc);
        cpu->pc = data16;
        /* No flags */
        return 6;
    case 0xAE:	/* LDS */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        /* Weirdly LDS *does* affect flags */
        cpu->s = tmp16;
        m6803_logic16(cpu, cpu->s);
        return 5;
    case 0xAF:	/* STS */
        m6803_do_write(cpu, data16, cpu->s >> 8);
        m6803_do_write(cpu, data16 + 1, cpu->s);
        m6803_logic16(cpu, cpu->s);
        return 5;
    /* 0xBX: extended */
    case 0xB0:	/* SUBA extended */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
        return 4;
    case 0xB1:	/* CMPA extended */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8);
        return 4;
    case 0xB2:	/* SBCA extended */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->a, tmp8, cpu->a - tmp8 - CARRY);
        return 4;
    case 0xB3:	/* SUBD extended */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        /* FIXME: flags */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D - tmp16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 6;
    case 0xB4:	/* ANDA extended */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a &= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xB5:	/* BITA */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_logic8(cpu, tmp8);
        return 4;
    case 0xB6:	/* LDAA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xB7:	/* STAA */
        m6803_do_write(cpu, data16, cpu->a);
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xB8:	/* EORA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a ^= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xB9:	/* ADCA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = m6803_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8 + CARRY);
        return 4;
    case 0xBA:	/* ORAA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a |= tmp8;
        m6803_logic8(cpu, cpu->a);
        return 4;
    case 0xBB:	/* ADDA */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->a = m6803_maths8(cpu, cpu->a, tmp8, cpu->a + tmp8);
        return 4;
    case 0xBC:	/* CPX */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        m6803_maths16_noh(cpu, cpu->x, data16, cpu->x - data16);
        return 6;
    case 0xBD:	/* JSR ext */
        m6803_push16(cpu, cpu->pc);
        cpu->pc = data16;
        /* No flags */
        return 8;
    case 0xBE: /* LDS */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        cpu->s = tmp16;
        m6803_logic16(cpu, cpu->s);
        return 5;
    case 0xBF:	/* STS */
        m6803_do_write(cpu, data16, cpu->s >> 8);
        m6803_do_write(cpu, data16 + 1, cpu->s);
        m6803_logic16(cpu, cpu->s);
        return 7;
    /* And then repeat for B instead of A and X instead of S, and ADD not
       SUBD */
    case 0xC0:	/* SUBB immed */
        cpu->b = m6803_maths8_noh(cpu, cpu->b, data8, cpu->b - data8);
        return 2;
    case 0xC1:	/* CMPB immed */
        m6803_maths8_noh(cpu, cpu->b, data8, cpu->b - data8);
        return 2;
    case 0xC2:	/* SBCB immed */
        m6803_maths8_noh(cpu, cpu->b, data8, cpu->b - data8 - CARRY);
        return 2;
    case 0xC3:	/* ADDD immed16 : weird case where the arg is 16bit */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D + data16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 4;
    case 0xC4:	/* ANDB immed */
        cpu->b &= data8;
        m6803_logic8(cpu, cpu->b);
        return 2;
    case 0xC5:	/* BITB */
        m6803_logic8(cpu, cpu->b & data8);
        return 2;
    case 0xC6:	/* LDAB */
        cpu->b = data8;
        m6803_logic8(cpu, cpu->b);
        return 2;
    /* No 0xC7 store immediate */
    case 0xC8:	/* EORB */
        cpu->b ^= data8;
        m6803_logic8(cpu, cpu->b);
        return 2;
    case 0xC9:	/* ADCB */
        cpu->b = m6803_maths8(cpu, cpu->b, data8, cpu->b + data8 + CARRY);
        return 2;
    case 0xCA:	/* ORAB */
        cpu->b |= data8;
        m6803_logic8(cpu, cpu->b);
        return 2;
    case 0xCB:	/* ADDB */
        cpu->b = m6803_maths8(cpu, cpu->b, data8, cpu->b + data8);
        return 2;
    case 0xCC:	/* LDD immediate */
        cpu->a = data16 >> 8;
        cpu->b = data16;
        m6803_logic16(cpu, REG_D);
        return 3;
    /* No STDD immediate */
    case 0xCE:	/* LDX immediate */
        cpu->x = data16;
        m6803_logic16(cpu, cpu->x);
        return 3;
    /* No STX immediate */
    /* Same again for direct */
    case 0xD0:	/* SUBB dir */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->b = m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
        return 3;
    case 0xD1:	/* CMPB dir */
        tmp8 = m6803_do_read(cpu, data8);
        m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
        return 3;
    case 0xD2:	/* SBCB dir */
        tmp8 = m6803_do_read(cpu, data8);
        m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8 - CARRY);
        return 3;
    case 0xD3:	/* ADDD dir */
        tmp16 = m6803_do_read(cpu, data8) << 8;
        tmp16 |= m6803_do_read(cpu, data8 + 1);
        /* FIXME: flags */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D + tmp16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 5;
    case 0xD4:	/* ANDB dir */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->b &= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 3;
    case 0xD5:	/* BITB */
        tmp8 = m6803_do_read(cpu, data8);
        m6803_logic8(cpu, tmp8);
        return 3;
    case 0xD6:	/* LDAB */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->b = tmp8;
        m6803_logic8(cpu, cpu->b);
        return 3;
    case 0xD7:	/* STAB */
        m6803_do_write(cpu, data8, cpu->b);
        m6803_logic8(cpu, cpu->b);
        return 3;
    case 0xD8:	/* EORB */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->b ^= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 3;
    case 0xD9:	/* ADCB */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->b = m6803_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8 + CARRY);
        return 3;
    case 0xDA:	/* ORAB */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->b |= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 3;
    case 0xDB:	/* ADDB */
        tmp8 = m6803_do_read(cpu, data8);
        cpu->b = m6803_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8);
        return 3;
    case 0xDC:	/* LDD direct */
        cpu->a = m6803_do_read(cpu, data8);
        cpu->b = m6803_do_read(cpu, data8 + 1);
        m6803_logic16(cpu, REG_D);
        return 4;
    case 0xDD:	/* STD direct */
        m6803_do_write(cpu, data8, cpu->a);
        m6803_do_write(cpu, data8 + 1, cpu->b);
        m6803_logic16(cpu, REG_D);
        return 4;
    case 0xDE:	/* LDX direct */
        cpu->x = m6803_do_read(cpu, data8) << 8; 
        cpu->x |= m6803_do_read(cpu, data8 + 1);
        m6803_logic16(cpu, cpu->x);
        return 2;
    case 0xDF: /* STX direct */
        m6803_do_write(cpu, data8, cpu->x >> 8);
        m6803_do_write(cpu, data8 + 1, cpu->x);
        return 4;
    /* 0xEX: indexed version */
    case 0xE0:	/* SUBB indexed */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
        return 4;
    case 0xE1:	/* CMPB indexed */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
        return 4;
    case 0xE2:	/* SBCB indexed */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8 - CARRY);
        return 4;
    case 0xE3:	/* SUBD indexed */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        /* FIXME: flags */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D + tmp16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 6;
    case 0xE4:	/* ANDB indexed */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b &= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xE5:	/* BITB */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_logic8(cpu, tmp8);
        return 4;
    case 0xE6:	/* LDAB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xE7:	/* STAB */
        m6803_do_write(cpu, data16, cpu->b);
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xE8:	/* EORB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b ^= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xE9:	/* ADCB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = m6803_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8 + CARRY);
        return 4;
    case 0xEA:	/* ORAB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b |= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xEB:	/* ADDB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = m6803_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8);
        return 4;
    case 0xEC:	/* LDD indexed */
        cpu->a = m6803_do_read(cpu, data16);
        cpu->b = m6803_do_read(cpu, data16 + 1);
        m6803_logic16(cpu, REG_D);
        return 5;
    case 0xED:	/* STD indexed */
        m6803_do_write(cpu, data16, cpu->a);
        m6803_do_write(cpu, data16 + 1, cpu->b);
        m6803_logic16(cpu, REG_D);
        return 5;
    case 0xEE:	/* LDX indexed */
        cpu->x = m6803_do_read(cpu, data16) << 8;
        cpu->x |= m6803_do_read(cpu, data16 + 1);
        m6803_logic16(cpu, cpu->x);
        return 2;
    case 0xEF:	/* STX indexed */
        m6803_do_write(cpu, data8, cpu->x >> 8);
        m6803_do_write(cpu, data8 + 1, cpu->x);
        return 5;
    /* 0xFX: extended */
    case 0xF0:	/* SUBB extended */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
        return 4;
    case 0xF1:	/* CMPB extended */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8);
        return 4;
    case 0xF2:	/* SBCB extended */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_maths8_noh(cpu, cpu->b, tmp8, cpu->b - tmp8 - CARRY);
        return 4;
    case 0xF3:	/* SUBD extended */
        tmp16 = m6803_do_read(cpu, data16) << 8;
        tmp16 |= m6803_do_read(cpu, data16 + 1);
        /* FIXME: flags */
        tmp16 = m6803_maths16_noh(cpu, REG_D, data16, REG_D + tmp16);
        cpu->a = tmp16 >> 8;
        cpu->b = tmp16;
        return 6;
    case 0xF4:	/* ANDB extended */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b &= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xF5:	/* BITB */
        tmp8 = m6803_do_read(cpu, data16);
        m6803_logic8(cpu, tmp8);
        return 4;
    case 0xF6:	/* LDAB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xF7:	/* STAB */
        m6803_do_write(cpu, data16, cpu->b);
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xF8:	/* EORB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b ^= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xF9:	/* ADCB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = m6803_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8 + CARRY);
        return 4;
    case 0xFA:	/* ORAB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b |= tmp8;
        m6803_logic8(cpu, cpu->b);
        return 4;
    case 0xFB:	/* ADDB */
        tmp8 = m6803_do_read(cpu, data16);
        cpu->b = m6803_maths8(cpu, cpu->b, tmp8, cpu->b + tmp8);
        return 4;
    case 0xFC:	/* LDD extended */
        cpu->a = m6803_do_read(cpu, data16);
        cpu->b = m6803_do_read(cpu, data16 + 1);
        m6803_logic16(cpu, REG_D);
        return 5;
    case 0xFD:	/* STD extended */
        m6803_do_write(cpu, data16, cpu->a);
        m6803_do_write(cpu, data16 + 1, cpu->b);
        m6803_logic16(cpu, REG_D);
        return 5;
    case 0xFE:	/* LDX extended */
        cpu->x = m6803_do_read(cpu, data16) << 8;
        cpu->x |= m6803_do_read(cpu, data16 + 1);
        m6803_logic16(cpu, cpu->x);
        return 3;
    case 0xFF:	/* STX extened */
        m6803_do_write(cpu, data16, cpu->a);
        m6803_do_write(cpu, data16 + 1, cpu->b);
        m6803_logic16(cpu, cpu->x);
        return 5;
    }
    if (cpu->type == 6303) {
        /* TRAP pushes the faulting address */
        fprintf(stderr, "illegal instruction %02X at %04X\n",
            opcode, fetch_pc);
        cpu->pc = fetch_pc;
        m6803_vector(cpu, 0xFFEE);
        return 12;	/* Not correct */
    } else {
        /* An invalid instruction we don't yet model */
        fprintf(stderr, "illegal instruction %02X at %04X\n",
            opcode, fetch_pc);
        return 0;
    }
}


/* Exception handling */

/* An exception takes 12 cycles. The first two we re-read the opcode
   (not modelled) then we push then read the vector then go */

/* FIXME: The 6303 is pipelined. This code is correct for a 6803 but a 6303
   the logical I mask follows reality by 2 instructions so for example
   "CLI NOP SEI" is not interruptible. We need some kind of shift reg
   holding the state of I by cycle */
static int m6803_pre_execute(struct m6803 *cpu)
{
    /* Interrupts are not latched */
    if (cpu->irq & IRQ_NMI) {
        cpu->irq &= ~IRQ_NMI;
        return m6803_vector(cpu, 0xFFFC);
    }

    if (cpu->irq & IRQ_IRQ1)
        return m6803_vector_masked(cpu, 0xFFF8);
    if (cpu->irq & IRQ_ICF)
        return m6803_vector_masked(cpu, 0xFFF6);
    if (cpu->irq & IRQ_OCF)
        return m6803_vector_masked(cpu, 0xFFF4);
    if (cpu->irq & IRQ_TOF)
        return m6803_vector_masked(cpu, 0xFFF2);
    if (cpu->irq & IRQ_SCI)
        return m6803_vector_masked(cpu, 0xFFF0);
    return 0;
}

void m6803_clear_interrupt(struct m6803 *cpu, int irq)
{
    cpu->irq &= ~irq;
}

void m6803_raise_interrupt(struct m6803 *cpu, int irq)
{
    cpu->irq |= irq;
}

/*
 *	Execute a machine cycle and return how many clocks
 *	we took doing it.
 */
int m6803_execute(struct m6803 *cpu)
{
    int cycles;
    uint32_t n;
    /* Interrupts ? */
    cycles = m6803_pre_execute(cpu);
    /* A cycle passes but we are waiting */
    if (cpu->wait)
        return 1;
    cycles += m6803_execute_one(cpu);

    /* See if we passed the output compare, and as we don't check every
       cycle deal with wraps */
    n = cpu->counter + cycles;
    if (cpu->oc_hold == 0 && cpu->counter >= cpu->ocr && cpu->counter < n) {
        cpu->tcsr |= TCSR_OCF;	/* OCF */
        if (cpu->tcsr & TCSR_EOCI)
            m6803_raise_interrupt(cpu, IRQ_OCF);
        cpu->tcsr ^= TCSR_OLVL;
    }
    cpu->oc_hold = 0;
    if (n > 0xFFFF) {
        cpu->tcsr |= TCSR_TOF;	/* TOF */
        if (cpu->tcsr & TCSR_ETOI)
            m6803_raise_interrupt(cpu, IRQ_TOF);
    }
    cpu->counter = (uint16_t)n;
    return cycles;
}

void m6803_reset(struct m6803 *cpu, int mode)
{
    cpu->p = P_I;
    cpu->pc = m6803_do_read(cpu, 0xFFFE) << 8;
    cpu->pc |= m6803_do_read(cpu, 0xFFFF);
    cpu->ramcr = RAMCR_RAME;	/* Internal RAM on FIXME check */
    cpu->tcsr = 0;
    cpu->counter = 0;	/* Really this is E clocks after reset FIXME */
    cpu->rmcr = 0;
    cpu->trcsr = TRCSR_TDRE;
    cpu->mode = mode;
    cpu->p2dr = mode << 5;
    cpu->p2ddr = 0;
    cpu->p1ddr = 0;
    cpu->iram_base = 0x80;	/* We don't yet emulate X/Y1 CPUs */
}


/*
 *	6803 device model
 */
 

/* Counter interrupts */
static void m6803_counter_ints(struct m6803 *cpu)
{
    if ((cpu->tcsr & (TCSR_ETOI|TCSR_TOF)) == (TCSR_ETOI|TCSR_TOF))
        m6803_raise_interrupt(cpu, IRQ_TOF);
    else
        m6803_clear_interrupt(cpu, IRQ_TOF);

    if ((cpu->tcsr & (TCSR_EOCI|TCSR_OCF)) == (TCSR_EOCI|TCSR_OCF))
        m6803_raise_interrupt(cpu, IRQ_OCF);
    else
        m6803_clear_interrupt(cpu, IRQ_OCF);
    if ((cpu->tcsr & (TCSR_EICI|TCSR_ICF)) == (TCSR_EICI|TCSR_ICF))
        m6803_raise_interrupt(cpu, IRQ_ICF);
    else
        m6803_clear_interrupt(cpu, IRQ_ICF);
}

/* SCI interrupts */ 
static void m6803_sci_ints(struct m6803 *cpu)
{
    int irq = 0;
    if ((cpu->trcsr & (TRCSR_TDRE|TRCSR_TIE)) == (TRCSR_TDRE|TRCSR_TIE))
        irq = 1;
    if ((cpu->trcsr & (TRCSR_RDRF|TRCSR_RIE)) == (TRCSR_RDRF|TRCSR_RIE))
        irq = 1;
    if ((cpu->trcsr & (TRCSR_ORFE|TRCSR_RIE)) == (TRCSR_ORFE|TRCSR_RIE))
        irq = 1;
    if (irq)
        m6803_raise_interrupt(cpu, IRQ_SCI);
    else
        m6803_clear_interrupt(cpu, IRQ_SCI);
}

/* We have received a byte of external data */
void m6803_rx_byte(struct m6803 *cpu, uint8_t c)
{
    if (cpu->trcsr & TRCSR_RE) {
        if (cpu->trcsr & TRCSR_RDRF)
            cpu->trcsr |= TRCSR_ORFE;
        else {
            cpu->trcsr |= TRCSR_RDRF;
            cpu->rdr = c;
            m6803_sci_ints(cpu);
        }
    }
}

void m6803_tx_done(struct m6803 *cpu)
{
    if (!(cpu->trcsr & TRCSR_TDRE)) {
        cpu->trcsr |= TRCSR_TDRE;
        m6803_sci_ints(cpu);
    }
}

uint8_t m6803_read_io(struct m6803 *cpu, uint8_t addr)
{
    uint8_t val;
    switch(addr) {
        case 0x00:
            return cpu->p1ddr;
        case 0x01:
            return cpu->p2ddr;
        case 0x02:
            val = m6803_port_input(cpu, 1) & ~cpu->p1ddr;
            val |= cpu->p1dr & cpu->p1ddr;
            return val;
        case 0x03:
            /* FIXME: what happens with serial enabled */
            /* P21 is special but we leave that to the called code */
            val = m6803_port_input(cpu, 2) & ~cpu->p2ddr;
            val |= cpu->p2dr & cpu->p2ddr;
            return val;
        /* 4-7 form a hole */
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            return m6803_read(cpu, addr);
        case 0x08:	/* Timer control and status */
            val = cpu->tcsr;
            /* Reading it clears several bits maybe: docs unclear */
            return val;
        case 0x09:	/* Free running counter */
            /* When this is read 0x0A is latched for one cycle
               and returns the latched bits to avoid wrap errors. This
               works by magic because we don't do per clock cycle emulation */
            cpu->tcsr &= ~(TCSR_TOF|TCSR_OCF);
            m6803_counter_ints(cpu);
            return cpu->counter >> 8;
        case 0x0A:
            return cpu->counter;
        case 0x0B:	/* Output compare */
            return cpu->ocr >> 8;
        case 0x0C:
            return cpu->ocr;
        case 0x0D:	/* Input capture is not emulated */
            cpu->tcsr &= ~TCSR_ICF;
            m6803_counter_ints(cpu);
        case 0x0E:
            return 0;
        case 0x0F:	/* hole in expanded */
            return m6803_read(cpu, addr);
        /* 0x10 is WO */
        case 0x11:	/* TRCSR */
            /* Some of the side effects need review as they are tangled
               with actual other port read/writes and the doc isn't clear */
            val = cpu->trcsr;
            return val;
        case 0x12:      /* RDR */
            cpu->trcsr &= ~(TRCSR_RDRF|TRCSR_ORFE);	/* RDRF off */
            m6803_sci_ints(cpu);
            return cpu->rdr;
        case 0x14:	/* RAMCR */
            return cpu->ramcr;
        /* On the later 6303
            0x14 becomes RAM/port5cr
            0x15 becomes port5 (R)
            0x16 becomes port6 ddr (W)
            0x17 becomes port6 (RW)
            0x18 becomes port7 (RW)
            0x19 becomes OCR2H
            0x1A becomes OCR2L
            0x1B becomes TCSR3
            0x1C becomes TCR
            0x1D becomes T2UP
            
            And latest of them
            0x1e becomes TXRXCSR2
            0x1f becomes test
            0x20 becomes port5 ddr
            0x21 becomes port6 csr
        */
        default:
            /* FIXME: log this */
            return 0xFF;	/* Reserved */
    }
}

void m6803_write_io(struct m6803 *cpu, uint8_t addr, uint8_t val)
{
    switch(addr) {
        case 0x00:
            cpu->p1ddr = val;
            m6803_port_output(cpu, 1);
            break;
        case 0x01:
            /* FIXME: what bits are writable - is the latched top fixed */
            cpu->p2ddr = val;
            m6803_port_output(cpu, 2);
            break;
        case 0x02:
            cpu->p2dr = val;
            m6803_port_output(cpu, 1);
            break;
        case 0x03:
            /* FIXME: what happens with serial enabled */
            cpu->p2dr = val;
            m6803_port_output(cpu, 2);
            break;
        /* 4-7 form a hole */
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            m6803_write(cpu, addr, val);
            break;
        case 0x08:
            cpu->tcsr = val;
            break;
        /* FIXME: on 6303 a double store *only* writes the timer correctly,
            a single causes the FFF8 effect as on 6803 */
        case 0x09:	/* Timer - test function set to FFF8 */
            cpu->counter = 0xFFF8;
            break;
        case 0x0A:	/* Timer - no effect */
            break;
        case 0x0B:	/* Output compare high */
            cpu->ocr &= 0xFF;
            cpu->ocr |= (val << 8);
            cpu->tcsr &= ~0x40;	/* Clear OCF */
            /* Review : 1 insn or one clock ? */
            cpu->oc_hold = 1;
            break;
        case 0x0C:	/* Output compare low */
            cpu->ocr &= 0xFF00;
            cpu->ocr |= val;
            cpu->tcsr &= ~0x40;	/* Clear OCF */
            break;
        case 0x0D:	/* Input capture (not emulated) */
        case 0x0E:
            break;
        case 0x0F:	/* Port 3 : hole for expanded mode */
            m6803_write(cpu, addr, val);
            break;
        case 0x10:
            cpu->rmcr = val & 0x0F;
            /* TODO: model the serial clocking from this */
            /* Call back so the caller knows the serial state changed */
            m6803_sci_change(cpu);
            break;
        case 0x11:
            cpu->trcsr &= ~0x1F;
            cpu->trcsr |= (val & 0x1E);
            m6803_sci_ints(cpu);
            /* Call back so the caller knows the serial state changed */
            m6803_sci_change(cpu);
            break;
        case 0x12:
            /* Check RO */
            break;
        case 0x13:
            /* TX enabled, TDRE empty */
            /* FIXME: this doesn't correctly model writing a byte then setting
               TE later */
            if ((cpu->trcsr & (TRCSR_TDRE|TRCSR_TE)) == (TRCSR_TDRE|TRCSR_TE)) {
                m6803_tx_byte(cpu, val);
                m6803_sci_ints(cpu);
            }
            break;
        case 0x14:
            /* FIXME: we need to watch bit 6 */
            cpu->ramcr = val;
            break;
    }
}

/* We only support mode 2 and mode 3 */

/* 0x40-0xFF are IRAM on the later 6303 parts */
uint8_t m6803_do_read(struct m6803 *cpu, uint16_t addr)
{
    if (addr >= 0x0100)
        return m6803_read(cpu, addr);
    if (addr < 0x20)
        return m6803_read_io(cpu, addr);
    if (cpu->mode == 2 && (cpu->ramcr & RAMCR_RAME) && (addr >= cpu->iram_base && addr <= 0xFF))
        return cpu->iram[addr - cpu->iram_base];
    return m6803_read(cpu, addr);
}

void m6803_do_write(struct m6803 *cpu, uint16_t addr, uint8_t val)
{
    if (addr >= 0x0100)
        m6803_write(cpu, addr, val);
    else if (addr < 0x20)
        m6803_write_io(cpu, addr, val);
    else if (cpu-> mode == 2 && (cpu->ramcr & RAMCR_RAME) && (addr >= cpu->iram_base && addr <= 0xFF))
        cpu->iram[addr - cpu->iram_base] = val;
    else
        m6803_write(cpu, addr, val);
}

uint8_t m6803_do_debug_read(struct m6803 *cpu, uint16_t addr)
{
    if (addr >= 0x0100)
        return m6803_debug_read(cpu, addr);
    if (addr < 0x20)
        return 0xFF;	/* Strictly we should handle the holes.. */
    if (cpu->mode == 2 && (cpu->ramcr & RAMCR_RAME) && (addr >= cpu->iram_base && addr <= 0xFF))
        return cpu->iram[addr - cpu->iram_base];
    return m6803_debug_read(cpu, addr);
}
