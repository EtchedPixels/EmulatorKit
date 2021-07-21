/*
 *	An Intel CPU 8008 emulator
 *
 *	The 8008 is in some ways very primitive but in other ways very similar
 *	to the 8080. The instruction encodings in particular are close but
 *	the 3bit register coding is in a different order.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parity.h"

#include "i8008.h"

struct i8008 {
	uint16_t callstack[8];	/* PC is part of the hardware call stack */
	unsigned int ctop;
#define reg_pc callstack[cpu->ctop]

	bool flags[4];		/* There is no real flags register just a set of testable conds */
#define cf flags[0]
#define zf flags[1]
#define sf flags[2]
#define pf flags[3]

	uint8_t reg[7];
#define reg_a reg[0]		/* Not the same order as Z80 */
#define reg_b reg[1]
#define reg_c reg[2]
#define reg_d reg[3]
#define reg_e reg[4]
#define reg_h reg[5]
#define reg_l reg[6]
/* and code 7 is (M) - aka 'HL' */

	/* FIXME: we really need to be able to simulate stepping and jamming
	   the arguments */
	uint8_t ins_jammed[3];
	int ins_jlen;
	int ins_jpos;

	bool pending_int;
	bool trace;
	bool step;
	uint16_t breakpt;

	unsigned int cycle_count;	/* Needed for bit bang serial emulation */

	bool halted;
};

#define tprintf		if (cpu->trace) printf

static void what_changed(struct i8008 *cpu);

static const char *regnames = "ABCDEHLM";
static const char *concode = "CZSP";

static uint8_t read_M(struct i8008 *cpu)
{
	uint8_t bank = cpu->reg_h & 0x3F;
	return mem_read(cpu, (bank << 8) | cpu->reg_l, 0);
}

static uint8_t read_M_debug(struct i8008 *cpu)
{
	uint8_t bank = cpu->reg_h & 0x3F;
	return mem_read(cpu, (bank << 8) | cpu->reg_l, 1);
}

static void write_M(struct i8008 *cpu, uint8_t val)
{
	uint8_t bank = cpu->reg_h & 0x3f;
	mem_write(cpu, (bank << 8) | cpu->reg_l, val);
}

/* The detail matters - 0xFF is halt and some setups rely on
   nonexistent memory halting the processor */
static uint8_t fetch(struct i8008 *cpu)
{
	uint8_t bank = (cpu->reg_pc >> 8) & 0x3f;
	uint8_t r;

	if (cpu->ins_jpos != -1) {
		printf("jpos %d, jlen %d\n", cpu->ins_jpos, cpu->ins_jlen);
		r = cpu->ins_jammed[cpu->ins_jpos++];
		tprintf("Jam->");
		if (cpu->ins_jpos >= cpu->ins_jlen)
			cpu->ins_jpos = -1;
	} else {
		r = mem_read(cpu, (bank << 8) | (cpu->reg_pc & 0xff), 0);
		cpu->reg_pc++;
	}
	tprintf("%o ", r);
	return r;
}

static bool condition(struct i8008 *cpu, uint8_t code)
{
	/* 0CC is "not", 1CC is "is" */
	bool c = cpu->flags[code & 3];
	if (code & 4)
		return c;
	return !c;
}

static void halt_op(struct i8008 *cpu)
{
	tprintf("HLT\n");
	cpu->halted = 1;
	cpu->cycle_count += 4;
}

static void jumpcall(struct i8008 *cpu, uint8_t op)
{
	/* 01 [cond.3] [nocond][call/jump][0] */
	uint16_t newpc;

	cpu->cycle_count += 9;
	tprintf("%c", op & 002 ? 'C' : 'J');
	if (op & 004) {
		if (op & 002) {
			tprintf("AL ");
		} else {
			tprintf("MP ");
		}
	} else {
		tprintf("%c%c ", concode[(op >> 3) & 3],
			op & 040 ? 'T' : 'F');
	}

	newpc = fetch(cpu);
	newpc |= fetch(cpu) << 8;
	if ((op & 004) || condition(cpu, op >> 3)) {
		if (op & 002) {
			cpu->cycle_count += 2;
			cpu->ctop++;
			cpu->ctop &= 7;
		}
		cpu->reg_pc = newpc;
	}
}

static void ret(struct i8008 *cpu, uint8_t op)
{
	/* 00 [cond.3][nocond] 11 */
	if (op & 004) {
		tprintf("RET");
	} else {
		tprintf("R%c%c", concode[(op >> 3) & 3],
			op & 040 ? 'T' : 'F');
	}
	cpu->cycle_count += 3;
	if ((op & 004) || condition(cpu, op >> 3)) {
		cpu->ctop--;
		cpu->ctop &= 7;
		cpu->cycle_count += 2;
	}
}

static void rst(struct i8008 *cpu, uint8_t op)
{
	/* 00 [addr] 101 */
	cpu->ctop++;
	cpu->ctop &= 7;
	cpu->reg_pc = op & 0070;
	tprintf("RST %d", cpu->reg_pc);
	cpu->cycle_count += 5;
}

static void inout(struct i8008 *cpu, uint8_t op)
{
	uint8_t port = (op & 076) >> 1;
	if ((op & 0060) == 0) {
		/* In */
		tprintf("INP %o ", port);
		cpu->cycle_count += 8;
		cpu->reg_a = io_read(cpu, port);
	} else {
		/* Out */
		tprintf("OUT %o ", port);
		cpu->cycle_count += 5;
		io_write(cpu, port, cpu->reg_a);
	}
}

/* Loads do not affect the condition flip flops */
static void loadr(struct i8008 *cpu, uint8_t op)
{
	/* 11 [dst][src], where 111 = M, but M to M means halt (just like 8080) */
	uint8_t src = op & 007;
	uint8_t dst = (op & 070) >> 3;


	if (op == 0377)
		halt_op(cpu);
	else {
		tprintf("L%c%c", regnames[dst], regnames[src]);
		if (src == 007) {
			src = read_M(cpu);
			cpu->cycle_count += 3;
		} else
			src = cpu->reg[src];

		if (dst == 007) {
			write_M(cpu, src);
			cpu->cycle_count += 2;
		} else
			cpu->reg[dst] = src;
	}
	cpu->cycle_count += 5;
}

static void loadi(struct i8008 *cpu, uint8_t op)
{
	/* 00 [dst] 110 */
	uint8_t dst = (op & 070) >> 3;

	tprintf("L%cI ", regnames[dst]);
	if (dst == 007) {
		cpu->cycle_count += 9;
		write_M(cpu, fetch(cpu));
	} else {
		cpu->cycle_count += 8;
		cpu->reg[dst] = fetch(cpu);
	}
}

static char *mops[] = { "AD", "AC", "SU", "SB", "ND", "XR", "OR", "CP" };

/* Maths affects all the condition flip flops */
static void math_op(struct i8008 *cpu, uint8_t op)
{
	uint8_t src;
	uint16_t dst;		/* We need bit 8 */
	/* Math is logically structured as

	   [immediate]0[alu.3]xxx where
	   0 = immediate and xxx is 100
	   1 = register and xxx is source or M

	   again much like 8080

	   The ALU codes are
	   000 add
	   001 add with carry
	   010 sub
	   011 sub with carry
	   100 and
	   101 xor
	   110 or
	   111 compare */

	cpu->cycle_count += 5;

	tprintf("%s", mops[(op & 070) >> 3]);
	if (op & 0200) {
		src = op & 007;
		tprintf("%c ", regnames[src]);
		if (src == 007) {
			cpu->cycle_count += 3;
			src = read_M(cpu);
		} else
			src = cpu->reg[src];
	} else {
		tprintf("I ");
		cpu->cycle_count += 3;
		src = fetch(cpu);
	}

	switch (op & 070) {
	case 000:
		dst = cpu->reg_a + src;
		break;
	case 010:
		dst = cpu->reg_a + src + cpu->cf;
		break;
	case 020:
		dst = cpu->reg_a - src;
		break;
	case 030:
		dst = cpu->reg_a - src - cpu->cf;
		break;
	case 040:
		dst = cpu->reg_a & src;
		break;
	case 050:
		dst = cpu->reg_a ^ src;
		break;
	case 060:
		dst = cpu->reg_a | src;
		break;
	case 070:
		dst = cpu->reg_a - src;
		break;
	}
	if ((op & 070) != 070)
		cpu->reg_a = dst;
	cpu->cf = (dst & 0x100) ? true : false;
	cpu->zf = (dst == 0) ? true : false;
	cpu->pf = parity[dst & 0xFF];
	cpu->sf = dst & 0x80;
}

static void incdec(struct i8008 *cpu, uint8_t op)
{
	/* 00 reg 00 [inc/dec] */
	/* inc A is not supported, nor dec A and halt the CPU !!! */
	/* Fixme - M case */
	uint8_t r = (op & 070) >> 3;
	if (r == 0)
		halt_op(cpu);
	else {
		if (op & 001) {
			tprintf("DC%c", regnames[r]);
			cpu->reg[r]--;
		} else {
			tprintf("IC%c", regnames[r]);
			cpu->reg[r]++;
		}
		/* inc/dec do not affect carry */
		cpu->zf = (cpu->reg[r] == 0) ? 1 : 0;
		/* Do other flags */
		cpu->pf = parity[cpu->reg[r]];
		cpu->sf = cpu->reg[r] & 0x80;
		cpu->cycle_count += 5;
	}
}

static void rotate(struct i8008 *cpu, uint8_t op)
{
	/* Only carry is affected */
	uint8_t dst;

	cpu->cycle_count += 5;

	/* 000 [withcarry][left/right]010 */
	switch (op) {
	case 002:
		tprintf("RLC");
		dst = (cpu->reg_a << 1) | ((cpu->reg_a & 0x80) ? 1 : 0);
		cpu->cf = (cpu->reg_a & 0x80) ? 1 : 0;
		break;
	case 012:
		tprintf("RRC");
		dst = (cpu->reg_a >> 1) | ((cpu->reg_a & 1) ? 0x80 : 0);
		cpu->cf = cpu->reg_a & 0x01;
		break;
	case 022:
		tprintf("RAL");
		dst = (cpu->reg_a << 1) | (cpu->cf ? 1 : 0);
		cpu->cf = (cpu->reg_a & 0x80) ? 1 : 0;
		break;
	case 032:
		tprintf("RAR");
		dst = (cpu->reg_a >> 1) | (cpu->cf ? 0x80 : 0);
		cpu->cf = cpu->reg_a & 0x01;
		break;
	}
	cpu->reg_a = dst;
}

static void opcode00(struct i8008 *cpu, uint8_t op)
{
	if ((op & 007) < 002)
		incdec(cpu, op);
	else if ((op & 007) == 002)
		rotate(cpu, op);
	else if ((op & 003) == 003)
		ret(cpu, op);
	else if ((op & 007) == 004)
		math_op(cpu, op);
	else if ((op & 007) == 005)
		rst(cpu, op);
	else if ((op & 007) == 006)
		loadi(cpu, op);
}

static void execute_op(struct i8008 *cpu, uint8_t op)
{
	if ((op & 0300) == 0300)
		loadr(cpu, op);
	else if (op & 0200)
		math_op(cpu, op);
	else if (op & 0100) {
		if (op & 1)
			inout(cpu, op);
		else
			jumpcall(cpu, op);
	} else
		opcode00(cpu, op);
	if (cpu->trace)
		what_changed(cpu);
	tprintf("\n");
	if (cpu->step)
		cpu->halted = true;
}

/* Note: the CPU is supposed to stay halted until first IRQ after
   power up */

static void execute_one(struct i8008 *cpu)
{
	/* Tidy this logic... FIXME */
	if (cpu->pending_int && cpu->ins_jpos != -1) {
		cpu->pending_int = false;
		cpu->ins_jpos++;
		if (cpu->ins_jpos >= cpu->ins_jlen)
			cpu->ins_jpos = -1;
		execute_op(cpu, cpu->ins_jammed[0]);
		return;
	}
	if (!cpu->halted) {
		tprintf("%o: ", cpu->reg_pc);
		if (cpu->reg_pc == cpu->breakpt)
			cpu->halted = 1;
		execute_op(cpu, fetch(cpu));
	}
}

unsigned int i8008_execute(struct i8008 *cpu, unsigned int tstates)
{
	cpu->cycle_count = 0;
	while (!cpu->halted && cpu->cycle_count < tstates)
		execute_one(cpu);
	return cpu->cycle_count;
}

int i8008_stuff(struct i8008 *cpu, uint8_t * bytes, int len)
{
	if (len > sizeof(cpu->ins_jammed))
		return -1;
	memcpy(cpu->ins_jammed, bytes, len);
	cpu->ins_jlen = len;
	cpu->ins_jpos = 0;
	cpu->pending_int = true;
	cpu->halted = false;
	return 0;
}

int i8008_halted(struct i8008 *cpu)
{
	return cpu->halted;
}

void i8008_halt(struct i8008 *cpu, unsigned int onoff)
{
	cpu->halted = onoff;
}

void i8008_resume(struct i8008 *cpu)
{
	cpu->halted = 0;
}

void i8008_breakpoint(struct i8008 *cpu, uint16_t addr)
{
	cpu->breakpt = addr;
}

uint16_t i8008_pc(struct i8008 *cpu)
{
	return cpu->reg_pc;
}

void i8008_singlestep(struct i8008 *cpu, unsigned int step)
{
	cpu->step = step;
}

void i8008_reset(struct i8008 *cpu)
{
	cpu->reg_a = 0;
	cpu->reg_b = 0;
	cpu->reg_c = 0;
	cpu->reg_d = 0;
	cpu->reg_e = 0;
	cpu->reg_h = 0;
	cpu->reg_l = 0;

	cpu->flags[0] = false;
	cpu->flags[1] = false;
	cpu->flags[2] = false;
	cpu->flags[3] = false;

	cpu->ctop = 0;
	cpu->reg_pc = 0;

	cpu->halted = true;
	cpu->ins_jlen = 0;
	cpu->ins_jpos = -1;

	cpu->pending_int = 0;
	cpu->step = 0;
}

struct i8008 *i8008_create(void)
{
	struct i8008 *cpu = malloc(sizeof(struct i8008));
	if (cpu == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	cpu->cycle_count = 0;
	cpu->breakpt = 0xFFFF;	/* Impossible address */
	i8008_reset(cpu);
	return cpu;
}

void i8008_trace(struct i8008 *cpu, unsigned int trace)
{
	cpu->trace = trace;
}

static void what_changed(struct i8008 *cpu)
{
	static int snap = 0;
	static uint8_t saved_reg[7];
	int chl = 0;
	int i;

	if (snap == 0) {
		snap = 1;
		return;
	}
	printf("   ");
	for (i = 0; i < 4; i++)
		printf("%c", cpu->flags[i] ? "CZSP"[i] : '-');
	printf("\n");
	for (i = 0; i < 7; i++) {
		if (cpu->reg[i] != saved_reg[i]) {
			if (i > 4)
				chl = 1;
			printf("%c %o(%d)->%o(%d)  ", "ABCDEHL"[i],
			       (unsigned int) saved_reg[i],
			       (unsigned int) saved_reg[i],
			       (unsigned int) cpu->reg[i],
			       (unsigned int) cpu->reg[i]);
			saved_reg[i] = cpu->reg[i];
		}
		if (chl)
			printf("(M)=%o(%d) ", read_M_debug(cpu),
			       read_M_debug(cpu));
	}
}


void i8008_dump(struct i8008 *cpu)
{
	int i;
	printf("PC=%o(%d)     ", cpu->reg_pc, cpu->reg_pc);
	for (i = 0; i < 4; i++)
		printf("%c", cpu->flags[i] ? "CZSP"[i] : '-');
	printf("\n");
	for (i = 0; i < 7; i++)
		printf("%c %o(%d)\n", "ABCDEHL"[i],
		       (unsigned int) cpu->reg[i],
		       (unsigned int) cpu->reg[i]);
}
