#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ns806x.h"

/*
 *	The NS8060 series aka the SC/MP and SC/MP-II
 *
 *	A very simple early microprocessor/controller.
 */

struct ns8060 {
    uint16_t p[4];	/* P1 to P3 - entry 0 is PC due to the encodings */
    uint8_t a;
    uint8_t e;
    uint8_t s;
#define S_CL	0x80		/* Carry / Link */
#define S_OV	0x40		/* Signed overflow */
#define S_SB	0x20		/* Buffered input B */
#define S_SA	0x10		/* Buffered input A */
#define S_F3	0x08		/* CPU flag outputs */
#define S_F2	0x04		/* Basically 3 GPIO */
#define S_F1	0x02		/* lines for bitbang */
#define S_IE	0x01		/* Interrupt enable */

    uint8_t i;
    uint8_t int_latch;
    uint8_t input;		/* A and B bits */

    int trace;			/* TODO */
};


static uint8_t mread(struct ns8060 *cpu, uint16_t addr)
{
    return mem_read(cpu, addr);
}

static void mwrite(struct ns8060 *cpu, uint16_t addr, uint8_t val)
{
    mem_write(cpu, addr, val);
}

/* Binary */
static void add(struct ns8060 *cpu, uint8_t b)
{
    int ov = 0;
    int r  = cpu->a + b;
    if (cpu->s & S_CL) {
        r++;
    }

	if(( (cpu->a & 0x80)==(b & 0x80) )&&( (cpu->a & 0x80)!=(r & 0x80) )) {
		ov = S_OV;
	}

    cpu->s &= ~(S_CL|S_OV);

	if( r & 0x100 ) cpu->s |= S_CL;
	
	cpu->s |= ov;

	cpu->a = r;
}

#if 0
/* Binary */
static void add(struct ns8060 *cpu, uint8_t b)
{
    uint8_t r = cpu->a + b;
    
    if (cpu->s & S_CL)
        r++;
    
    cpu->s &= ~(S_CL|S_OV);

    if (r & 0x80) {
        if (!((cpu->a | b) & 0x80))
            cpu->s |= S_OV;
    } else {
        if (cpu->a & b & 0x80)
            cpu->s |= S_OV;
    }
    /* A input sign differs from result and sign of inputs are the same */
    if (((cpu->a ^ r) & 0x80) && !((cpu->a ^ b) & 0x80))
        cpu->s |= S_OV;

    cpu->a = r;
}
#endif

/* BCD */
static void dad(struct ns8060 *cpu, uint8_t b)
{
    uint8_t ln = (cpu->a & 0x0F) + (b & 0x0F);
    uint8_t hn = (cpu->a & 0xF0) + (b & 0xF0);

    /* Add carry in to low nibble */
    if (cpu->s & S_CL)
        ln++;
    /* Carry between nibbles */
    if (ln > 0x09) {
        ln -= 0x0A;
        hn += 0x10;
    }
    /* Carry from the high nibble int CL */
    if (hn > 0x90) {
        hn -= 0xA0;
        cpu->s |= S_CL;
    } else
        cpu->s &= ~S_CL;

    cpu->a = hn + ln;
}

/* Generate a current S value */

static uint8_t get_s(struct ns8060 *cpu)
{
    uint8_t s = cpu->s & ~(S_SA|S_SB);
    s |= cpu->input;
    return s;
}

/* Complement and add (ie sub) - a ones complement machine at heart */
static char *cpu_flags(struct ns8060 *cpu)
{
    static char buf[9];
    char *p = buf;
    char *x = "COBAI210";
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

   
/* The weird 12bit pointer maths */
static uint16_t add12(uint16_t a, int8_t off)
{
    uint16_t top = a & 0xF000;
    a += off;
    a &= 0x0FFF;
    a |= top;
    return a;
}

/*
 *	Increment is done on the instruction fetch (hence we start at 1, and
 *	the branch encoding). In addition only the low 12 bits of PC increment.
 */
static void fetch_instruction(struct ns8060 *cpu)
{
    cpu->p[0] = add12(cpu->p[0], 1);
    cpu->i = mread(cpu, cpu->p[0]);
    if (cpu->trace) {
        fprintf(stderr, "%04X: ", cpu->p[0]);
        fprintf(stderr, "%s %02X%02X %04X %04X %04X",
            cpu_flags(cpu),
            cpu->e, cpu->a, cpu->p[1], cpu->p[2], cpu->p[3]);
        fprintf(stderr, " :%02X", cpu->i);
    }
}

static unsigned int check_interrupt(struct ns8060 *cpu)
{
    uint8_t tmp;
    if (!(cpu->s & S_IE))
        return 0;
    if (!(cpu->int_latch))
        return 0;
    cpu->s &= ~S_IE;
    tmp = cpu->p[0];
    cpu->p[0] = cpu->p[3];
    cpu->p[3] = tmp;
    cpu->int_latch = 0;
    return 9;
}

static int8_t get_off8(struct ns8060 *cpu)
{
    int8_t v;
    cpu->p[0] = add12(cpu->p[0], 1);
    v = mread(cpu, cpu->p[0]);
    if (cpu->trace)
        fprintf(stderr, "%02X ", v);
    return v;
}

static uint8_t get_imm8(struct ns8060 *cpu)
{
    uint8_t v;
    cpu->p[0] = add12(cpu->p[0], 1);
    v = mread(cpu, cpu->p[0]);
    if (cpu->trace)
        fprintf(stderr, "%02X ", v);
    return v;
}

/* Generate an effective address */
static uint16_t make_address(struct ns8060 *cpu, int mode, int8_t off, int notjmp)
{
    if( (off&0xff) == 0x80 && notjmp)
        off = cpu->e;

    if (!(mode & 4)) {  // not AutoIndex
        return add12(cpu->p[mode & 3], off);
    } else {            // AutoIndex
        if (off < 0) {
            cpu->p[mode & 3] = add12(cpu->p[mode & 3], off);
            return cpu->p[mode & 3];
        }else {
			uint16_t oldptr = cpu->p[mode & 3];
            cpu->p[mode & 3] = add12(cpu->p[mode & 3], off);
            return oldptr;
        }
    }
}

#if 0
/* Generate an effective address */
static uint16_t make_address(struct ns8060 *cpu, int mode, int8_t off, int notjmp)
{
    if (off == 0x80 && notjmp)
        off = cpu->e;
    if (!(mode & 4)) {
        return mread(cpu, add12(cpu->p[mode & 3], off));
    } else {
        if (off < 0)
            return mread(cpu, cpu->p[mode & 3] = add12(cpu->p[mode & 3], off));
        else {
            uint8_t tmp8 = mread(cpu, cpu->p[mode & 3]);
            cpu->p[mode & 3] = add12(cpu->p[mode & 3], off);
            return tmp8;
        }
    }
}
#endif
static void illegal(struct ns8060 *cpu)
{
    fprintf(stderr, "Illegal operation 0x%02X at 0x%04X.\n",
        cpu->i, cpu->p[0]);
    /* We don't have a model for ilegals so we don't know how to behave here */
    exit(1);
}

static unsigned int memop(struct ns8060 *cpu)
{
    uint16_t addr;
    uint8_t val;
    unsigned int immediate = 0;

    if ((cpu->i & 0x07) == 4) {
        val = get_imm8(cpu);
        immediate = 1;
    } else
        addr = make_address(cpu, cpu->i & 7, get_off8(cpu), 1);
    
    switch(cpu->i & 0xF8) {
        case 0xC0:	/* LD */
            if (!immediate) {
                cpu->a = mread(cpu, addr);
                return 18;
            }
            cpu->a = val;
            return 10;
        case 0xC8:	/* ST */
            if (immediate)
                break;
            mwrite(cpu, addr, cpu->a);
            return 18;
        case 0xD0:	/* AND */
            if (!immediate) {
                cpu->a &= mread(cpu, addr);
                return 18;
            }
            cpu->a &= val;
            return 10;
        case 0xD8:	/* OR */
            if (!immediate) {
                cpu->a |= mread(cpu, addr);
                return 18;
            }
            cpu->a |= val;
            return 10;
        case 0xE0:	/* XOR */
            if (!immediate) {
                cpu->a ^= mread(cpu, addr);
                return 18;
            }
            cpu->a ^= val;
            return 10;
        case 0xE8:	/* DAD */
            if (!immediate) {
                dad(cpu, mread(cpu, addr));
                return 23;
            }
            dad(cpu, val);
            return 15;
        case 0xF0:	/* ADD */
            if (!immediate) {
                add(cpu, mread(cpu, addr));
                return 19;
            }
            add(cpu, val);
            return 11;
        case 0xF8:	/* CAD */
            if (!immediate) {
                add(cpu, ~mread(cpu, addr));
                return 20;
            }
            add(cpu, ~val);
            return 12;
    }
    illegal(cpu);
    return 20;	/* ?? */
}

static unsigned int dbop(struct ns8060 *cpu)
{
    unsigned int ptr = cpu->i & 3;
    int8_t disp = get_off8(cpu);

#if 1
    int notjmp = 1;
	if((cpu->i >= 0x90) &&(cpu->i < 0xa0)) notjmp = 0;
	
	unsigned int addr = make_address(cpu, cpu->i & 3, disp, notjmp);
#else
    /* E substitution does not apply to 9x instructions */
    unsigned int addr = make_address(cpu, cpu->i & 3, disp, (cpu->i & 0x20));
#endif
    switch(cpu->i & 0xFC)
    {
        case 0x8C:
            /* TODO: We don't model stuff happening during the delay */
            if (ptr == 3)
                return 514 * (uint8_t)disp + cpu->a + 13;
            break;
        case 0x90:	/* JMP */
            cpu->p[0] = addr;
            return 11;
        case 0x94:	/* JP */
            if (cpu->a & 0x80)
                return 9;
            cpu->p[0] = addr;
            return 11;
        case 0x98:	/* JZ */
            if (cpu->a)
                return 9;
            cpu->p[0] = addr;
            return 11;
        case 0x9C:	/* JNZ */
            if (cpu->a == 0)
                return 9;
            cpu->p[0] = addr;
            return 11;
        case 0xA8:	/* ILD */
            mwrite(cpu, addr , cpu->a = mread(cpu, addr) + 1);
            return 22;
        case 0xB8:	/* DLD */
            mwrite(cpu, addr , cpu->a = mread(cpu, addr) - 1);
            return 22;
    }
    illegal(cpu);
    return 20; /* ?? */
}

static unsigned int execute_op(struct ns8060 *cpu)
{
    uint8_t tmp8;
    uint16_t tmp16;
    unsigned int ptr;

    if (cpu->trace)
        fprintf(stderr, "%02X ", cpu->i);

    if ((cpu->i & 0xC0) == 0xC0)
        return memop(cpu);
    if ((cpu->i & 0x80) == 0x80)
        return dbop(cpu);
    /* Single byte ops */
    switch(cpu->i) {
    case 0x00:	/* HALT */
        /* TODO */
        return 8;
    case 0x01:	/* XAE */
        tmp8 = cpu->a;
        cpu->a = cpu->e;
        cpu->e = tmp8;
        return 7;
    case 0x02:	/* CCL */
        cpu->s &= ~S_CL;
        return 5;
    case 0x03:	/* SCL */
        cpu->s |= S_CL;
        return 5;
    case 0x04:	/* DINT */
        cpu->s &= ~S_IE;
        return 6;
    case 0x05:	/* IEN */
        cpu->s |= S_IE;
        return 6;
    case 0x06:	/* CSA */
        cpu->a = get_s(cpu);
        return 5;
    case 0x07:	/* CAS */
        cpu->s = cpu->a;
        return 6;
    case 0x08:	/* NOP */
        return 0;
    case 0x19:	/* SIO */
        tmp8 = cpu->e;
        cpu->e >>= 1;
        cpu->e |= ser_input(cpu) ? 0x80: 0;
        ser_output(cpu, tmp8 & 1);
        return 5;
    case 0x1C:	/* SR */
        cpu->a >>= 1;
        return 5;
    case 0x1D:	/* SRL */
        tmp8 = cpu->a & 1;
        cpu->a >>= 1;
        if (cpu->s & S_CL)
            cpu->a |= 0x80;
        if (tmp8)
            cpu->s |= S_CL;
        else
            cpu->s &= ~S_CL;
        return 5;
    case 0x1E:	/* RR */
        tmp8 = cpu->a & 1;
        cpu->a >>= 1;
        if (tmp8)
            cpu->a |= 0x80;
        return 5;
    case 0x1F:	/* RRL */
        tmp8 = cpu->a & 1;
        cpu->a >>= 1;
        if (cpu->s & S_CL)
            cpu->a |= 0x80;
        if (tmp8)
            cpu->s |= S_CL;
        else
            cpu->s &= ~S_CL;
        return 5;

#if 1 // Print I/O Hook.		
    case 0x20:  /* UNDEFINED */
		ns8060_emu_putch(cpu->a);
        return 5;
    case 0x21:  /* UNDEFINED */
		cpu->a = cpu->e = ns8060_emu_getch();
        return 5;
#endif

    case 0x30:	/* XPAL */
    case 0x31:
    case 0x32:
    case 0x33:
        ptr = cpu->i & 3;
        tmp8 = cpu->p[ptr];
        cpu->p[ptr] &= 0xFF00;
        cpu->p[ptr] |= cpu->a;
        cpu->a = tmp8;
        return 8;
    case 0x34:	/* XPAH */
    case 0x35:
    case 0x36:
    case 0x37:
        ptr = cpu->i & 3;
        tmp8 = cpu->p[ptr] >> 8;
        cpu->p[ptr] &= 0xFF;
        cpu->p[ptr] |= cpu->a << 8;
        cpu->a = tmp8;
        return 8;
    case 0x3C:	/* XPPC */
    case 0x3D:
    case 0x3E:
    case 0x3F:
        ptr = cpu->i & 3;
        tmp16 = cpu->p[ptr];
        cpu->p[ptr] = cpu->p[0];
        cpu->p[0] = tmp16;
        return 7;
    case 0x40:	/* LDE */
        cpu->a = cpu->e;
        return 6;
    case 0x50:	/* ANE */
        cpu->a &= cpu->e;
        return 6;
    case 0x58:	/* ORE */
        cpu->a |= cpu->e;
        return 6;
    case 0x60:	/* XRE */
        cpu->a ^= cpu->e;
        return 6;
    case 0x68:	/* DAE */
        dad(cpu, cpu->e);
        return 11;
    case 0x70:	/* ADE */
        add(cpu, cpu->e);
        return 7;
    case 0x78:	/* CAE */
        add(cpu, ~cpu->e);
        return 8;
    default:
        illegal(cpu);
        return 10;
    }
}

unsigned int ns8060_execute_one(struct ns8060 *cpu)
{
    unsigned int clocks = 0;
    clocks += check_interrupt(cpu);
    fetch_instruction(cpu);
    clocks += execute_op(cpu);
    if (cpu->trace)
        fprintf(stderr, "\n");
    return clocks;
}

void ns8060_reset(struct ns8060 *cpu)
{
    cpu->p[0] = 0;
    cpu->p[1] = 0;
    cpu->p[2] = 0;
    cpu->p[3] = 0;
    cpu->s = 0;

    cpu->int_latch = 0;	/* Open to debate */
}

struct ns8060 *ns8060_create(void)
{
    struct ns8060 *cpu = malloc(sizeof(struct ns8060));
    if (cpu == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    cpu->trace = 0;
    ns8060_reset(cpu);
	return cpu;
}

void ns8060_trace(struct ns8060 *cpu, unsigned int onoff)
{
    cpu->trace = onoff;
}

void ns8060_set_a(struct ns8060 *cpu, unsigned int bit)
{
    /* TODO: interrupt */
    if (bit)
        cpu->input |= S_SA;
    else
        cpu->input &= ~S_SA;
}

void ns8060_set_b(struct ns8060 *cpu, unsigned int bit)
{
    if (bit)
        cpu->input |= S_SB;
    else
        cpu->input &= ~S_SB;
}
