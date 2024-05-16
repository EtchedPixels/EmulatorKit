#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ns807x.h"

/*
 *	The NS8070/72/73 - "7000 series"
 *
 *	The NS807x is a reasonably sane architecture for the most part with
 *	some interesting quirks. In particular branches are based on the
 *	accumulator and flags are little used and harder to access than on
 *	most processors. The other major quirk is that there are no absolute
 *	loads except for the direct page. All loads must go through a pointer.
 *
 *	INS8070		64 bytes onchip RAM (FFC0-FFFF) no onchip ROM 
 *	INS8072		As above 2.5K on chip ROM (0000-09FF)
 *	INS8073		An 8072 with a standard TinyBASIC ROM
 *
 *	Limits (to fix)
 *	-	T after a divide is wrong
 *	-	No undocumented instruction behaviour
 */

struct ns8070 {
    uint16_t pc;
    uint16_t t;
    uint16_t sp;
    uint16_t p2;
    uint16_t p3;
    uint8_t a;
    uint8_t e;
    uint8_t s;
    uint8_t *rom;
    uint8_t ram[64];

    uint8_t i;
    uint8_t int_latch;
#define INT_A	1
#define INT_B	2
    uint8_t input;

    int trace;			/* TODO */
};


static uint8_t mread(struct ns8070 *cpu, uint16_t addr)
{
    if (addr >= 0xFFC0)
        return cpu->ram[addr - 0xFFC0];
    if (cpu->rom && addr < 0xA00)
        return cpu->rom[addr];
    return mem_read(cpu, addr);
}

static uint16_t mread16(struct ns8070 *cpu, uint16_t addr)
{
    uint16_t val = mread(cpu, addr);
    val |= mread(cpu, addr + 1) << 8;
    return val;
}

static void mwrite(struct ns8070 *cpu, uint16_t addr, uint8_t val)
{
    if (addr >= 0xFFC0) {
        cpu->ram[addr - 0xFFC0] = val;
        return;
    }
    else if (!cpu->rom || addr >= 0xA00) {
        mem_write(cpu, addr, val);
        return;
    }
    if (cpu->trace)
        fprintf(stderr, "Write to ROM 0x%04X<-%02X\n", addr, val);
}

/* Only defined for 16bit unsigned divided by 15bit  */
static void divide1616(struct ns8070 *cpu)
{
    uint16_t val = (cpu->e << 8) | cpu->a;
    /* T is set to a partial remainder. We need to work out how this works
       so we can emulate it TODO */
    uint16_t r = val % cpu->t;
    val /= cpu->t;
    cpu->t = r;
    cpu->e = val >> 8;
    cpu->a = val;
    /* What should happen to CY/L and OV ? */
}

/* This isn't quite right. The operation is defined for a signed 16bit x unsigned 15bit (T)
   but not clear what happens otherwise */
static void mul32(struct ns8070 *cpu)
{
    int16_t val = (cpu->e << 8) | cpu->a;
    int32_t ret = (int16_t)(cpu->t & 0x7FFF) * (int16_t)val;
    /* EA gets the high word T the low */
    cpu->e = ret >> 24;
    cpu->a = ret >> 16;
    cpu->t = ret;
    /* What should CY/L and OV be set to */
    if ((cpu->t * val) & 0x80000000)
        cpu->s |= S_CL;
    
}

static uint8_t maths8add(struct ns8070 *cpu, uint8_t a, uint8_t b, uint8_t r)
{
    cpu->s &= ~(S_CL|S_OV);
    if (r & 0x80) {
        if (!((a | b) & 0x80))
            cpu->s |= S_OV;
    } else {
        if (a & b & 0x80)
            cpu->s |= S_OV;
    }
    if (a & b & 0x80)
        cpu->s |= S_CL;
    if (a & ~r & 0x80)
        cpu->s |= S_CL;
    if (b & ~r & 0x80)
        cpu->s |= S_CL;
    return r;
}

static uint8_t maths8sub(struct ns8070 *cpu, uint8_t a, uint8_t b, uint8_t r)
{
    cpu->s &= ~(S_CL|S_OV);
    if (a & 0x80) {
        if (!((b | r) & 0x80))
            cpu->s |= S_OV;
    } else {
        if (b & r & 0x80)
            cpu->s |= S_OV;
    }
    if (~a & b & 0x80)
        cpu->s |= S_CL;
    if (b & r & 0x80)
        cpu->s |= S_CL;
    if (~a & r & 0x80)
        cpu->s |= S_CL;
    cpu->s ^= S_CL;
    return r;
}

static void maths16ea(struct ns8070 *cpu, uint16_t val, int dir)
{
    uint16_t ea = (cpu->e << 8) | cpu->a;
    uint32_t r;

    cpu->s &= ~(S_CL|S_OV);

    if (dir == -1) {
        r = ea - val;
        if (((r ^ ea) & 0x8000) && !((r ^ val) & 0x8000))
            cpu->s |= S_OV;
        if (r & 0x10000)
            cpu->s |= S_CL;
        cpu->s ^= S_CL;
    } else { 
        r = ea + val;
        if (((r ^ ea) & 0x8000)  && ((r ^ val) & 0x8000))
            cpu->s |= S_OV;
        if (r & 0x10000)
            cpu->s |= S_CL;
    }
    cpu->e = r >> 8;
    cpu->a = r;
}

static unsigned int do_ssm(struct ns8070 *cpu, uint16_t *ptr)
{
    unsigned int n = 0;
    unsigned int clocks = 0;

    while(n++ < 256) {
        if (mread(cpu, (*ptr)++) == cpu->a) {
            cpu->pc += 2;
            return clocks + 7;
        }
        clocks += 4;
    }
    (*ptr)--;		/* Gets 255 increments */
    return clocks + 5;
}    

static uint8_t get_s(struct ns8070 *cpu)
{
    uint8_t val = cpu->s & ~(S_SB | S_SA);
    val |= cpu->input;
    return val;
}

static uint16_t get_ea(struct ns8070 *cpu)
{
    uint16_t r = cpu->e << 8;
    r |= cpu->a;
    return r;
}

static char *cpu_flags(struct ns8070 *cpu)
{
    static char buf[9];
    char *p = buf;
    char *x = "COBA321I";
    uint8_t s = get_s(cpu);
    
    while(*x) {
        if (s & 0x80)
            *p++ = *x;
        else
            *p++ = '-';
        x++;
        s <<= 1;
    }
    return buf;
}

static char *iname(uint8_t i)
{
    fprintf(stderr, "[%02X]", i);
    if (i & 0x80) {
        switch(i & 0xF8) {
        case 0x80:
            return "LD EA,";
        case 0x88:
            return "ST EA,";
        case 0x90:
            return "ILD A,";
        case 0x98:
            return "DLD A,";
        case 0xA0:
            return "LD T,";
        case 0xA8:
            return "ILL ST T,";
        case 0xB0:
            return "ADD EA,";
        case 0xB8:
            return "SUB EA,";
        case 0xC0:
            return "LD A,";
        case 0xC8:
            return "ST A,";
        case 0xD0:
            return "AND A,";
        case 0xD8:
            return "OR A,";
        case 0xE0:
            return "XOR A,";
        case 0xE8:
            return "ILL";
        case 0xF0:
            return "ADD A,";
        case 0xF8:
            return "SUB A,";
        }
    }
    if (i >= 0x10 && i <= 0x1F) {
        static char calltmp[16];
        snprintf(calltmp, 16, "CALL%d", i & 0x0F);
        return calltmp;
    }
    switch(i) {
    case 0x00:
        return "NOP";
    case 0x01:
        return "XCH A,E";
    case 0x06:
        return "LD A,S";
    case 0x07:
        return "LD S,A";
    case 0x08:
        return "PUSH EA";
    case 0x09:
        return "LD T,";
    case 0x0A:
        return "PUSH A,";
    case 0x0B:
        return "LD EA,T";
    case 0x0C:
        return "SR EA";
    case 0x0D:
        return "DIV EA,T";
    case 0x0E:
        return "SL A";
    case 0x0F:
        return "SL EA";
    case 0x20:
        return "JSR";
    case 0x22:
        return "PLI P2,";
    case 0x23:
        return "PLI P3,";
    case 0x24:
        return "JMP ";
    case 0x25:
        return "LD SP,";
    case 0x26:
        return "LD P2,";
    case 0x27:
        return "LD P3,";
    case 0x2C:
        return "MPY EA,T";
    case 0x2D:
        return "BND";
    case 0x2E:
        return "SSM P2";
    case 0x2F:
        return "SSM P3";
    case 0x30:
        return "LD EA,PC";
    case 0x31:
        return "LD EA,SP";
    case 0x32:
        return "LD EA,P2";
    case 0x33:
        return "LD EA,P3";
    case 0x38:
        return "POP A";
    case 0x39:
        return "AND S,";
    case 0x3A:
        return "POP EA";
    case 0x3B:
        return "OR S,";
    case 0x3C:
        return "SR A";
    case 0x3D:
        return "SRL A";
    case 0x3E:
        return "RR A";
    case 0x3F:
        return "RRL A";
    case 0x40:
        return "LD A,E";
    case 0x44:
        return "LD PC,EA";
    case 0x45:
        return "LD SP,EA";
    case 0x46:
        return "LD P2,EA";
    case 0x47:
        return "LD P3,EA";
    case 0x48:
        return "LD E,A";
    case 0x4C:
        return "XCH EA,PC";
    case 0x4D:
        return "XCH EA,SP";
    case 0x4E:
        return "XCH EA,P2";
    case 0x4F:
        return "XCH EA,P3";
    case 0x54:
        return "PUSH PC";
    case 0x56:
        return "PUSH P2";
    case 0x57:
        return "PUSH P3";
    case 0x58:
        return "OR A,E";
    case 0x5C:
        return "RET";
    case 0x5E:
        return "POP P2";
    case 0x5F:
        return "POP P3";
    case 0x60:
        return "XOR A,E";
    case 0x64:
        return "BP";
    case 0x66:
        return "BP P2";
    case 0x67:
        return "BP P3";
    case 0x6C:
        return "BZ";
    case 0x6E:
        return "BZ P2";
    case 0x6F:
        return "BZ P3";
    case 0x70:
        return "ADD A,E";
    case 0x74:
        return "BRA";
    case 0x76:
        return "BRA P2";
    case 0x77:
        return "BRA P3";
    case 0x78:
        return "SUB A,E";
    case 0x7C:
        return "BNZ";
    case 0x7E:
        return "BNZ P2";
    case 0x7F:
        return "BNZ P3";
    default:
        return "ILLEGAL";
    }
}

static char *hexsigned(uint8_t v)
{
    static char buf[4];
    if (v < 0)
        snprintf(buf, 4, "-%02X", -v);
    else
        snprintf(buf, 4, "%02X", v);
    return buf;
}

/*
 *	This is probably the weirdest part of the NS8070. PC is incremented
 *	on instruction fetch. This means that for example a jump to $5000 is
 *	encoded as a jump to $4FFF and also means that stacked PC values are
 *	of the last byte of the call and is why we execute from address 1
 */

static void fetch_instruction(struct ns8070 *cpu)
{
    cpu->pc++;
    cpu->i = mread(cpu, cpu->pc);
    if (cpu->trace) {
        fprintf(stderr, "%04X: ", cpu->pc);
        fprintf(stderr, "%s %02X:%02X %04X %04X %04X %04X",
            cpu_flags(cpu),
            cpu->e, cpu->a, cpu->t, cpu->p2, cpu->p3, cpu->sp);
        fprintf(stderr, " :%s ", iname(cpu->i));
    }
}

static void illegal(struct ns8070 *cpu)
{
    fprintf(stderr, "Illegal instruction %02X at %04X\n",
        cpu->i, cpu->pc);
    /* No illegal model so can't continue */
    exit(1);
}

static uint16_t make_address(struct ns8070 *cpu, uint8_t mode, int8_t offset)
{
    uint16_t addr;
    switch(mode) {
    case 0:	/* PC relative 8bit signed */
        if (cpu->trace)
            fprintf(stderr, "%s,PC", hexsigned(offset));
        return cpu->pc + offset;
    case 1:	/* SP relative */
        if (cpu->trace)
            fprintf(stderr, "%s,SP", hexsigned(offset));
        return cpu->sp + offset;
    case 2:	/* P2 relative */
        if (cpu->trace)
            fprintf(stderr, "%s,P2", hexsigned(offset));
        return cpu->p2 + offset;
    case 3:	/* P3 relative */
        if (cpu->trace)
            fprintf(stderr, "%s,P3", hexsigned(offset));
        return cpu->p3 + offset;
    case 4:	/* Immediate */
        fprintf(stderr, "Internal error: make_address of immediate.\n");
        exit(1);
    case 5:	/* Direct - which is FFxx in our case */
        if (cpu->trace)
            fprintf(stderr, "FF%02X", (uint8_t)offset);
        return 0xFF00 + (uint8_t)offset;
    /* Auto indexing is a bit odd. It implements negative pre-decrement and
       positive post increment. For things like stacks this makes a lot of
       sense */
    case 6:	/* Auto indexed P2 */
        if (offset < 0) {
            if (cpu->trace)
                fprintf(stderr, "@%s,P2", hexsigned(offset));
            cpu->p2 += offset;
            return cpu->p2;
        } else {
            if (cpu->trace)
                fprintf(stderr, "%s,@P2", hexsigned(offset));
            addr = cpu->p2;
            cpu->p2 += offset;
            return addr;
        }
    case 7:
        if (offset < 0) {
            if (cpu->trace)
                fprintf(stderr, "@%s,P3", hexsigned(offset));
            cpu->p3 += offset;
            return cpu->p3;
        } else {
            if (cpu->trace)
                fprintf(stderr, "%s,@P3", hexsigned(offset));
            addr = cpu->p3;
            cpu->p3 += offset;
            return addr;
        }
    }
    fprintf(stderr, "Invalid addressing mode\n");
    exit(1);
}

static uint8_t pop8(struct ns8070 *cpu)
{
    return mread(cpu, cpu->sp++);
}

static uint16_t pop16(struct ns8070 *cpu)
{
    uint16_t v = pop8(cpu);
    v |= pop8(cpu) << 8;
    return v;
}

static void push8(struct ns8070 *cpu, uint8_t val)
{
    mwrite(cpu, --cpu->sp, val);
}

static void push16(struct ns8070 *cpu, uint16_t val)
{
    mwrite(cpu, --cpu->sp, val >> 8);
    mwrite(cpu, --cpu->sp, val);
}

static unsigned int check_interrupt(struct ns8070 *cpu)
{
    if (!(cpu->s & S_IE))
        return 0;
    if (cpu->int_latch == 0)
        return 0;
    cpu->s &= ~S_IE;
    push16(cpu, cpu->pc);
    /* A has priority */
    if (cpu->int_latch & INT_A)	{
        cpu->int_latch &= ~INT_A;
        cpu->pc = 3;
    } else {
        cpu->int_latch &= ~INT_B;
        cpu->pc = 6;
    }
    return 9;
}

static int8_t get_off8(struct ns8070 *cpu)
{
    int8_t v = mread(cpu, ++cpu->pc);
    if (cpu->trace) {
        if (v >= 0)
            fprintf(stderr, "+%02X ", v);
        else
            fprintf(stderr, "-%02X ", -v);
    }
    return v;
}

static uint8_t get_imm8_q(struct ns8070 *cpu)
{
    uint8_t v = mread(cpu, ++cpu->pc);
    return v;
}


static uint8_t get_imm8(struct ns8070 *cpu)
{
    uint8_t v = mread(cpu, ++cpu->pc);
    if (cpu->trace)
        fprintf(stderr, "=%02X ", v);
    return v;
}

static uint16_t get_imm16(struct ns8070 *cpu)
{
    uint16_t r = mread(cpu, ++cpu->pc);
    r |= mread(cpu, ++cpu->pc) << 8;
    if (cpu->trace)
        fprintf(stderr, "=%04X ", r);
    return r;
}

/* 84 (8C) (94) (9C) A4 (AC) B4 BC are data 2
   C4 D4 E4 F4 and (CC) DC (EC) FC are data 1 */
   
static unsigned int execute_high(struct ns8070 *cpu)
{
    uint8_t mode = cpu->i & 0x07;
    uint8_t op = cpu->i & 0xF8;
    uint16_t val;
    uint16_t addr;
    unsigned int immed = 0;
    unsigned int cost = 0;

    /* Immediate */
    if (mode == 0x04) {
        if (op & 0x40)
            val = get_imm8(cpu);
        else
            val = get_imm16(cpu);
        immed = 1;
    } else
        addr = make_address(cpu, mode, get_imm8_q(cpu));

    if (mode >= 6)
        cost += 2;

    switch(op) {
    case 0x80:	/* LD EA, */
        if (!immed)
            val = mread16(cpu, addr);
        cpu->e = val >> 8;
        cpu->a = val;
        return 10 + cost;
    case 0x88:	/* ST EA, */
        if (immed)
            illegal(cpu);
        else {
            mwrite(cpu, addr, cpu->a);
            mwrite(cpu, addr + 1, cpu->e);
        }
        return 10 + cost;
    case 0x90:	/* ILD A, */
        if (immed)
            illegal(cpu);
        else {
            /* FIXME: the manual implies auto-indexing occurs after
               the increment, which seems weird and if so we need to handle
               it specially. This instruction is also interlocked on SMP. */
            cpu->a = mread(cpu, addr) + 1;
            mwrite(cpu, addr, cpu->a);
        }
        return 8 + cost;
    case 0x98:	/* DLD A, */
        if (immed)
            illegal(cpu);
        else {
            /* FIXME: the manual implies auto-indexing occurs after
               the increment, which seems weird and if so we need to handle
               it specially. This instruction is also interlocked on SMP. */
            cpu->a = mread(cpu, addr) - 1;
            mwrite(cpu, addr, cpu->a);
        }
        return 8 + cost;
    case 0xA0:  /* LD T, */
        if (!immed)
            val = mread16(cpu, addr);
        cpu->t = val;
        return 10 + cost;
    case 0xA8:	/* Unused *- no ST T, ? */
    case 0xB0:	/* ADD EA, */
        if (!immed)
            val = mread16(cpu, addr);
        maths16ea(cpu, val, 1);
        return 8 + cost;
    case 0xB8:	/* SUB EA, */
        if (!immed)
            val = mread16(cpu, addr);
        maths16ea(cpu, val, -1);
        return 8 + cost;
    case 0xC0:	/* LD A,, */
        if (!immed)
            val = mread(cpu, addr);
        cpu->a = val;
        return 7 + cost;
    case 0xC8:	/* ST A,, */
        if (immed)
            illegal(cpu);
        else
            mwrite(cpu, addr, cpu->a);
        return 7 + cost;
    case 0xD0:	/* AND A,, */
        if (!immed)
            val = mread(cpu, addr);
        cpu->a &= val;
        return 7 + cost;
    case 0xD8:	/* OR A,, */
        if (!immed)
            val = mread(cpu, addr);
        cpu->a |= val;
        return 7 + cost;
    case 0xE0:	/* XOR A, */
        if (!immed)
            val = mread(cpu, addr);
        cpu->a ^= val;
        return 7 + cost;
    case 0xE8:	/* UNUSED */
        return 7;	/* Unclear */
    case 0xF0:	/* ADD A,, */
        if (!immed)
            val = mread(cpu, addr);
        maths8add(cpu, cpu->a, val, cpu->a + val);
        cpu->a += val;
        return 7 + cost;
    case 0xF8:	/* SUB A,, */
        if (!immed)
            val = mread(cpu, addr);
        maths8sub(cpu, cpu->a, val, cpu->a - val);
        cpu->a -= val;
        return 7 + cost;
    }
    fprintf(stderr, "Unknown instruction %x\n", op);
    exit(1);
}

/* There does not seem to be a very convenient logic about what operations
   use what memory types.*/
static unsigned int execute_op(struct ns8070 *cpu)
{
    uint8_t tmp8;
    uint16_t tmp16;
    int8_t offs;

    /* The upper half of the instruction space has a rather more sane decode
       model */
    if (cpu->i & 0x80)
        return execute_high(cpu);

    switch(cpu->i) {
    case 0x00:	/* NOP */
        return 3;
    case 0x01:	/* XCH A,E */
        tmp8 = cpu->a;
        cpu->a = cpu->e;
        cpu->e = tmp8;
        return 5;
    case 0x06:	/* LD A,S */
        cpu->a = get_s(cpu);
        return 3;
    case 0x07:	/* LD S,A */
        cpu->s = cpu->a;
        flag_change(cpu, cpu->s & (S_F3|S_F2|S_F1));
        return 3;
    case 0x08:	/* PUSH EA */
        push8(cpu, cpu->e);
        push8(cpu, cpu->a);
        return 8;
    case 0x09:	/* LD T,EA */
        cpu->t = get_ea(cpu);
        return 4;
    case 0x0A:	/* PUSH A */
        push8(cpu, cpu->a);
        return 5;
    case 0x0B:	/* LD EA,T */
        cpu->a = cpu->t;
        cpu->e = cpu->t >> 8;
        return 4;
    case 0x0C:	/* SR EA */
        cpu->a >>= 1;
        if (cpu->e & 1)
            cpu->a |= 0x80;
        cpu->e >>= 1;
        return 4;
    case 0x0D:	/* DIV EA,T */
        divide1616(cpu);
        return 42;
    case 0x0E:	/* SL A */
        cpu->a <<= 1;
        return 3;
    case 0x0F:	/* SL EA */
        cpu->e <<= 1;
        if (cpu->a & 0x80)
            cpu->e |= 0x01;
        cpu->a <<= 1;
        return 4;
    case 0x10:	/* CALL */
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x1F:
        /* 16 one byte subroutine calls */
        tmp8 = (cpu->i & 0x0F) << 1;
        push16(cpu, cpu->pc);
        cpu->pc = mread(cpu, 0x21 + tmp8) << 8;
        cpu->pc |= mread(cpu, 0x20 + tmp8);
        return 16;
    case 0x20:	/* JSR  (actually PLI PC,=ADDR) */
        push16(cpu, cpu->pc + 2);
        cpu->pc = get_imm16(cpu);
        return 16;
    case 0x22:	/* PLI P2,=ADDR - push and load immediate - genius little instruction */
        push16(cpu, cpu->p2);
        cpu->p2 = get_imm16(cpu);
        return 16;
    case 0x23:	/* PLI P3,=ADDR */
        push16(cpu, cpu->p3);
        cpu->p3 = get_imm16(cpu);
        return 16;
    case 0x24:	/* JMP (really LD PC,=ADDR)*/
        cpu->pc = get_imm16(cpu);
        return 9;
    case 0x25:	/* LD SP,=ADDR */
        cpu->sp = get_imm16(cpu);
        return 9;
    case 0x26:	/* LD P2,=ADDR */
        cpu->p2 = get_imm16(cpu);
        return 9;
    case 0x27:	/* LD P3,=ADDR */
        cpu->p3 = get_imm16(cpu);
        return 9;
    case 0x2C:	/* MPY EA,T */
        /* 32bit multiply ! */
        mul32(cpu);
        return 37;
    case 0x2D:	/* BND */
        /* Seriously weird - branch if not ascii 0-9, otherwise subtract "0" */
        offs = get_off8(cpu);
        tmp8 = cpu->a;
        if (cpu->a >= 48 && cpu->a <= 57) {
            cpu->a -= 48;
        } else {
            cpu->pc += offs;
        }        
        /* The low test occurs first and is 2 clocks shorter */
        return tmp8 < 48 ? 7 : 9;
    case 0x2E:	/* SSM P2 */
        return do_ssm(cpu, &cpu->p2);
    case 0x2F:	/* SSM P3 */
        return do_ssm(cpu, &cpu->p3);
    case 0x30:	/* LD EA,PC */
        cpu->e = cpu->pc >> 8;
        cpu->a = cpu->pc;
        return 4;
    case 0x31:	/* LD EA,SP */
        cpu->e = cpu->sp >> 8;
        cpu->a = cpu->sp;
        return 4;
    case 0x32:	/* LD EA,P2 */
        cpu->e = cpu->p2 >> 8;
        cpu->a = cpu->p2;
        return 4;
    case 0x33:	/* LD EA,P3 */
        cpu->e = cpu->p3 >> 8;
        cpu->a = cpu->p3;
        return 4;
    case 0x38:	/* POP A */
        cpu->a = pop8(cpu);
        return 6;
    case 0x39:	/* AND S,=DATA1 */
        cpu->s &= get_imm8(cpu);
        flag_change(cpu, cpu->s & (S_F3|S_F2|S_F1));
        return 5;
    case 0x3A:	/* POP EA */
        cpu->a = pop8(cpu);
        cpu->e = pop8(cpu);
        return 9;
    case 0x3B:	/* OR S,=DATA1 */
        cpu->s |= get_imm8(cpu);
        flag_change(cpu, cpu->s & (S_F3|S_F2|S_F1));
        return 5;
    case 0x3C:	/* SR A */
        cpu->a >>= 1;
        return 3;
    case 0x3D:	/* SRL A */
        cpu->a >>= 1;
        if (cpu->s & S_CL)
            cpu->a |= 0x80;
        return 3;
    case 0x3E:	/* RR A */
        tmp8 = (cpu->a & 0x01) ? 0x80: 0x00;
        cpu->a >>= 1;
        cpu->a |= tmp8;
        return 3;
    case 0x3F:	/* RRL A */
        tmp8 = cpu->a;
        cpu->a >>= 1;
        if (cpu->s & S_CL)
            cpu->a |= 0x80;
        if (tmp8 & 1)
            cpu->s |= S_CL;
        else
            cpu->s &= ~S_CL;
        return 3;
    /* The low bits encode the register (E,A A,E or PC/SP/P2/P3) */
    case 0x40:  /* LD A,E */
        cpu->a = cpu->e;
        return 5;
    case 0x44:	/* LD PC,EA */
        cpu->pc = get_ea(cpu);
        return 5;
    case 0x45:	/* LD SP,EA */
        cpu->sp = get_ea(cpu);
        return 5;
    case 0x46:	/* LD P2,EA */
        cpu->p2 = get_ea(cpu);
        return 5;
    case 0x47:	/* LD P3,EA */
        cpu->p3 = get_ea(cpu);
        return 5;
    case 0x48:	/* LD E,A */
        cpu->e = cpu->a;
        return 4;
    case 0x4C:	/* XCH EA,PC */
        tmp16 = (cpu->e << 8) | cpu->a;
        cpu->e = cpu->pc >> 8;
        cpu->a = cpu->pc;
        cpu->pc = tmp16;
        return 5;
    case 0x4D:	/* XCH EA,SP */
        tmp16 = (cpu->e << 8) | cpu->a;
        cpu->e = cpu->sp >> 8;
        cpu->a = cpu->sp;
        cpu->sp = tmp16;
        return 5;
    case 0x4E:	/* XCH EA,P2 */
        tmp16 = (cpu->e << 8) | cpu->a;
        cpu->e = cpu->p2 >> 8;
        cpu->a = cpu->p2;
        cpu->p2 = tmp16;
        return 5;
    case 0x4F:	/* XCH EA,P3 */
        tmp16 = (cpu->e << 8) | cpu->a;
        cpu->e = cpu->p3 >> 8;
        cpu->a = cpu->p3;
        cpu->p3 = tmp16;
        return 5;
    case 0x54:	/* PUSH PC */
        push16(cpu, cpu->pc);
        return 8;
                /* No PUSH SP */
    case 0x56:	/* PUSH P2 */
        push16(cpu, cpu->p2);
        return 8;
    case 0x57:	/* PUSH P3 */
        push16(cpu, cpu->p3);
        return 8;
    case 0x58:	/* OR A,E */
        cpu->a |= cpu->e;
        return 4;
    case 0x5C:	/* RET (actually POP PC) */
        cpu->pc = pop16(cpu);
        return 10;
    case 0x5E:	/* POP P2 */
        cpu->p2 = pop16(cpu);
        return 10;
    case 0x5F:	/* POP P3 */
        cpu->p3 = pop16(cpu);
        return 10;
    case 0x60:	/* XOR A,E */
        cpu->a ^= cpu->e;
        return 4;
    case 0x64:	/* BP (PC) */
        offs = get_off8(cpu);
        if (!(cpu->a & 0x80))
            cpu->pc += offs;
        return 5;
    case 0x66:	/* BP P2 */
        offs = get_off8(cpu);
        if (!(cpu->a & 0x80))
            cpu->pc = cpu->p2 + offs;
        return 5;
    case 0x67:	/* BP P3 */
        offs = get_off8(cpu);
        if (!(cpu->a & 0x80))
            cpu->pc = cpu->p3 + offs;
        return 5;
    case 0x6C:	/* BZ */
        offs = get_off8(cpu);
        if (cpu->a == 0)
            cpu->pc += offs;
        return 5;
    case 0x6E:	/* BZ P2 */
        offs = get_off8(cpu);
        if (cpu->a == 0)
            cpu->pc = cpu->p2 + offs;
        return 5;
    case 0x6F:	/* BZ P3 */
        offs = get_off8(cpu);
        if (cpu->a == 0)
            cpu->pc = cpu->p3 + offs;
        else
            cpu->pc++;
        return 5;
    case 0x70:	/* ADD A,E */
        cpu->a = maths8add(cpu, cpu->a, cpu->e, cpu->a + cpu->e);
        return 5;
    case 0x74:	/* BRA */
        offs = get_off8(cpu);
        cpu->pc += offs;
        return 5;
    case 0x76:	/* BRA P2 */
        cpu->pc = cpu->p2 + get_off8(cpu);
        return 5;
    case 0x77:	/* BRA P3 */
        cpu->pc = cpu->p3 + get_off8(cpu);
        return 5;
    case 0x78:	/* SUB A,E */
        cpu->a = maths8sub(cpu, cpu->a, cpu->e, cpu->a - cpu->e);
        return 5;
    case 0x7C:	/* BNZ */
        offs = get_off8(cpu);
        if (cpu->a)
            cpu->pc += offs;
        return 5;
    case 0x7E:	/* BNZ P2 */
        offs = get_off8(cpu);
        if (cpu->a)
            cpu->pc = cpu->p2 + offs ;
        return 5;
    case 0x7F:	/* BNZ P3 */
        offs = get_off8(cpu);
        if (cpu->a)
            cpu->pc = cpu->p3 + offs;
        return 5;
    default:
        illegal(cpu);
        return 10;
    }
}

unsigned int ns8070_execute_one(struct ns8070 *cpu)
{
    unsigned int clocks = 0;
    clocks += check_interrupt(cpu);
    fetch_instruction(cpu);
    clocks += execute_op(cpu);
    if (cpu->trace)
        fprintf(stderr, "\n");
    return clocks;
}

void ns8070_reset(struct ns8070 *cpu)
{
    cpu->pc = 0;
    cpu->sp = 0;
    cpu->s = 0;
    flag_change(cpu, 0);

    cpu->int_latch = 0;	/* Open to debate */
}

struct ns8070 *ns8070_create(uint8_t *rom)
{
    struct ns8070 *cpu = malloc(sizeof(struct ns8070));
    if (cpu == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    cpu->rom = rom;
    cpu->trace = 0;
    ns8070_reset(cpu);
    return cpu;
}

void ns8070_trace(struct ns8070 *cpu, unsigned int onoff)
{
    cpu->trace = onoff;
}

void ns8070_set_a(struct ns8070 *cpu, unsigned int bit)
{
    /* TODO: interrupt */
    if (bit)
        cpu->input |= S_SA;
    else {
        /* Trailing edge - trigger IRQ latch */
        if (cpu->input & S_SA) {
            cpu->int_latch |= INT_A;
            cpu->input &= ~S_SA;
        }
    }
}

void ns8070_set_b(struct ns8070 *cpu, unsigned int bit)
{
    if (bit)
        cpu->input |= S_SB;
    else {
        /* Trailing edge - trigger IRQ latch */
        if (cpu->input & S_SB) {
            cpu->int_latch |= INT_B;
            cpu->input &= ~S_SB;
        }
    }
}
