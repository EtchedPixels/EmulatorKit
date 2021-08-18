/* =========================================================
 *  libz80 - Z80 emulation library
 * =========================================================
 *
 * (C) Gabriel Gambetta (gabriel.gambetta@gmail.com) 2000 - 2012
 *
 * Version 2.1.0
 *
 * ---------------------------------------------------------
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "z80.h"
#include "string.h"


#define BR (ctx->R1.br)
#define WR (ctx->R1.wr)

#define SETFLAG(F) setFlag(ctx, F)
#define RESFLAG(F) resFlag(ctx, F)
#define GETFLAG(F) getFlag(ctx, F)

#define VALFLAG(F,V) valFlag(ctx, F, V)


/* ---------------------------------------------------------
 *  Flag tricks
 * --------------------------------------------------------- 
 *
 * To avoid repeating entries in the spec files, many operations that look similar are treated as special cases
 * of a more general operation.
 *
 * For example, ADD and ADC are similar in syntax and operation - the difference is that ADC takes the carry flag
 * into account.
 *
 * So we define a general operation doArithmetic(...) which accepts a boolean parameter specifying wheter to do
 * a Carry-operation or not. Then, when we parse, we can say
 *
 * (ADD|ADC) ....
 *		doArithmetic(FLAG_FOR_%1)
 *
 * and everything works fine.
 *
 */
 
/* Flags for doIncDec() */
static const int ID_INC = 0;
static const int ID_DEC = 1;

/* Flags for enable / disable interrupts */
static const int IE_DI = 0;
static const int IE_EI = 1;

/* Flags for doSetRes() */
static const int SR_RES = 0;
static const int SR_SET = 1;

/* Flags for logical / arithmetic operations */
static const int IA_L = 0;
static const int IA_A = 1;

/* Flags for doArithmetic() - F1 = withCarry, F2 = isSub */
static const int F1_ADC = 1;
static const int F1_SBC = 1;
static const int F1_ADD = 0;
static const int F1_SUB = 0;

static const int F2_ADC = 0;
static const int F2_SBC = 1;
static const int F2_ADD = 0;
static const int F2_SUB = 1;

/* Increment or decrement R, preserving bit 7 */
#define INCR (ctx->R = (ctx->R & 0x80) | ((ctx->R + 1) & 0x7f))
#define DECR (ctx->R = (ctx->R & 0x80) | ((ctx->R - 1) & 0x7f))


/* ---------------------------------------------------------
 *  The opcode implementations
 * --------------------------------------------------------- 
 */
#include "codegen/opcodes_decl.h"

typedef enum
{
	OP_NONE,
	OP_BYTE,
	OP_OFFSET,
	OP_WORD	
} Z80OperandType;

typedef void (*Z80OpcodeFunc) (Z80Context* ctx); 

struct Z80OpcodeEntry
{
	Z80OpcodeFunc func;
	
	int operand_type;
	char* format;	
	
	const struct Z80OpcodeTable* table;
};


struct Z80OpcodeTable
{
	int opcode_offset;
	const struct Z80OpcodeEntry entries[256];
};


#include "codegen/opcodes_table.h"


/* ---------------------------------------------------------
 *  Data operations
 * --------------------------------------------------------- 
 */ 
static void write8 (Z80Context* ctx, ushort addr, byte val)
{
	ctx->tstates += 3;
	ctx->memWrite(ctx->memParam, addr, val);	
}


static void write16 (Z80Context* ctx, ushort addr, ushort val)
{
	write8(ctx, addr, val);
	write8(ctx, addr + 1, val >> 8);
}


static byte read8 (Z80Context* ctx, ushort addr)
{
	ctx->tstates += 3;
	return ctx->memRead(ctx->memParam, addr);	
}


static ushort read16 (Z80Context* ctx, ushort addr)
{
	byte lsb = read8(ctx, addr);
	byte msb = read8(ctx, addr + 1);
	return msb << 8 | lsb;
}


static byte ioRead (Z80Context* ctx, ushort addr)
{
	ctx->tstates += 4;
	return ctx->ioRead(ctx->ioParam, addr);
}


static void ioWrite (Z80Context* ctx, ushort addr, byte val)
{
	ctx->tstates += 4;
	ctx->ioWrite(ctx->ioParam, addr, val);
}


/* ---------------------------------------------------------
 *  Flag operations
 * --------------------------------------------------------- 
 */
 
/** Sets a flag */
static void setFlag(Z80Context* ctx, Z80Flags flag)
{
	BR.F |= flag;
}

/** Resets a flag */
static void resFlag(Z80Context* ctx, Z80Flags flag)
{
	BR.F &= ~flag;
}

/** Puts a value in a flag */
static void valFlag(Z80Context* ctx, Z80Flags flag, int val)
{
	if (val)
		SETFLAG(flag);
	else
		RESFLAG(flag);
}

/** Returns a flag */
static int getFlag(Z80Context* ctx, Z80Flags flag)
{
	return (BR.F & flag) != 0;
}


/* ---------------------------------------------------------
 *  Flag adjustments
 * --------------------------------------------------------- 
 */

static int parityBit[256] = { 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 };


static void adjustFlags (Z80Context* ctx, byte val)
{
	VALFLAG(F_5, (val & F_5) != 0);
	VALFLAG(F_3, (val & F_3) != 0);
}


static void adjustFlagSZP (Z80Context* ctx, byte val)
{
	VALFLAG(F_S, (val & 0x80) != 0);
	VALFLAG(F_Z, (val == 0));
	VALFLAG(F_PV, parityBit[val]);
}


/* Adjust flags after AND, OR, XOR */
static void adjustLogicFlag (Z80Context* ctx, int flagH)
{
    VALFLAG(F_S, (BR.A & 0x80) != 0);
    VALFLAG(F_Z, (BR.A == 0));
    VALFLAG(F_H, flagH);
    VALFLAG(F_N, 0);
    VALFLAG(F_C, 0);
    VALFLAG(F_PV, parityBit[BR.A]);

    adjustFlags(ctx, BR.A);
}


/* ---------------------------------------------------------
 *  Condition checks
 * --------------------------------------------------------- 
 */
 
typedef enum
{
	C_,
	C_Z,
	C_NZ,
	C_C,
	C_NC,
	C_M,
	C_P,
	C_PE,
	C_PO		
} Z80Condition;

static int condition(Z80Context* ctx, Z80Condition cond)
{
	if (cond == C_)
		return 1;
		
	if (cond == C_Z)
		return GETFLAG(F_Z);
	
	if (cond == C_NZ)
		return !GETFLAG(F_Z);
	
	if (cond == C_C)
		return GETFLAG(F_C);
	
	if (cond == C_NC)
		return !GETFLAG(F_C);
		
	if (cond == C_M)
		return GETFLAG(F_S);
	
	if (cond == C_P)
		return !GETFLAG(F_S);
		
	if (cond == C_PE)
		return GETFLAG(F_PV);
		
/*	if (cond == C_PO)*/
		return !GETFLAG(F_PV);
}


/* ---------------------------------------------------------
 *  Generic operations
 * --------------------------------------------------------- 
 */
 
 
static int doComplement(byte v)
{
	if ((v & 0x80) == 0)
		return v;

	v = ~v;
	v &= 0x7F;
	v++;

	return -v;
}

 
/** Do an arithmetic operation (ADD, SUB, ADC, SBC y CP) */
static byte doArithmetic (Z80Context* ctx, byte value, int withCarry, int isSub)
{
	ushort res; /* To detect carry */

	if (isSub)
	{
		SETFLAG(F_N);
		VALFLAG(F_H, (((BR.A & 0x0F) - (value & 0x0F)) & 0x10) != 0);
		res = BR.A - value;
		if (withCarry && GETFLAG(F_C))
			res--;
	}
	else
	{
		RESFLAG(F_N);
		VALFLAG(F_H, (((BR.A & 0x0F) + (value & 0x0F)) & 0x10) != 0);
		res = BR.A + value;
		if (withCarry && GETFLAG(F_C))
			res++;
	}
	VALFLAG(F_S, ((res & 0x80) != 0));
	VALFLAG(F_C, ((res & 0x100) != 0));
	VALFLAG(F_Z, ((res & 0xff) == 0));
	int minuend_sign = BR.A & 0x80;
	int subtrahend_sign = value & 0x80;
	int result_sign = res & 0x80;
	int overflow;
	if(isSub)
		overflow = minuend_sign != subtrahend_sign && result_sign != minuend_sign;
	else
		overflow = minuend_sign == subtrahend_sign && result_sign != minuend_sign;
	VALFLAG(F_PV, overflow);
	adjustFlags(ctx, res);

	return (byte)(res & 0xFF);
}


/* Do a 16-bit addition, setting the appropriate flags. */
static ushort doAddWord(Z80Context* ctx, ushort a1, ushort a2, int withCarry, int isSub)
{
	if(withCarry && GETFLAG(F_C))
		a2++;
	int sum = a1;
	if(isSub)
	{
		sum -= a2;
		VALFLAG(F_H, ((a1 & 0x0fff) - (a2 & 0x0fff)) & 0x1000);
	}
	else
	{
		sum += a2;
		VALFLAG(F_H, ((a1 & 0x0fff) + (a2 & 0x0fff)) & 0x1000);
	}
	VALFLAG(F_C, sum & 0x10000);
	if(withCarry || isSub)
	{
		int minuend_sign = a1 & 0x8000;
		int subtrahend_sign = a2 & 0x8000;
		int result_sign = sum & 0x8000;
		int overflow;
		if(isSub)
			overflow = minuend_sign != subtrahend_sign && result_sign != minuend_sign;
		else
			overflow = minuend_sign == subtrahend_sign && result_sign != minuend_sign;
		VALFLAG(F_PV, overflow);
		VALFLAG(F_S, (sum & 0x8000) != 0);
		VALFLAG(F_Z, (sum & 0xFFFF) == 0);
	}
	VALFLAG(F_N, isSub);
	adjustFlags(ctx, sum >> 8);
	return sum;
}


static void doAND (Z80Context* ctx, byte value)
{
	BR.A &= value;
	adjustLogicFlag(ctx, 1);
}


static void doOR (Z80Context* ctx, byte value)
{
	BR.A |= value;
	adjustLogicFlag(ctx, 0);
}


static void doXOR (Z80Context* ctx, byte value)
{
	BR.A ^= value;
	adjustLogicFlag(ctx, 0);
}


static void doBIT (Z80Context* ctx, int b, byte val)
{
	if (val & (1 << b))
        RESFLAG(F_Z | F_PV);
    else
        SETFLAG(F_Z | F_PV);

    SETFLAG(F_H);
    RESFLAG(F_N);

    RESFLAG(F_S);
    if ((b == 7) && !GETFLAG(F_Z))
        SETFLAG(F_S);
}


static void doBIT_r(Z80Context* ctx, int b, byte val)
{
	doBIT(ctx, b, val);
	VALFLAG(F_5, val & F_5);
	VALFLAG(F_3, val & F_3);
}


static void doBIT_indexed(Z80Context* ctx, int b, ushort address)
{
	byte val = read8(ctx, address);
	doBIT(ctx, b, val);
	VALFLAG(F_5, (address >> 8) & F_5);
	VALFLAG(F_3, (address >> 8) & F_3);
}


byte doSetRes (Z80Context* ctx, int bit, int pos, byte val)
{
    if (bit)
		val |= (1 << pos);
    else
		val &= ~(1 << pos);
    return val;
}



static byte doIncDec (Z80Context* ctx, byte val, int isDec)
{
    if (isDec)
    {
        VALFLAG(F_PV, (val & 0x80) && !((val - 1) & 0x80));
        val--;
        VALFLAG(F_H, (val & 0x0F) == 0x0F);
    }
    else
    {
        VALFLAG(F_PV, !(val & 0x80) && ((val + 1) & 0x80));
        val++;
        VALFLAG(F_H, !(val & 0x0F));
    }

    VALFLAG(F_S, ((val & 0x80) != 0));
    VALFLAG(F_Z, (val == 0));
    VALFLAG(F_N, isDec);

    adjustFlags(ctx, val);

    return val;
}


static byte doRLC (Z80Context* ctx, int adjFlags, byte val)
{
    VALFLAG(F_C, (val & 0x80) != 0);
    val <<= 1;
    val |= (byte)GETFLAG(F_C);

    adjustFlags(ctx, val);
    RESFLAG(F_H | F_N);

    if (adjFlags)
        adjustFlagSZP(ctx, val);

    return val;
}


static byte doRL (Z80Context* ctx, int adjFlags, byte val)
{
    int CY = GETFLAG(F_C);
    VALFLAG(F_C, (val & 0x80) != 0);
    val <<= 1;
    val |= (byte)CY;

    adjustFlags(ctx, val);
    RESFLAG(F_H | F_N);

    if (adjFlags)
        adjustFlagSZP(ctx, val);

    return val;
}


static byte doRRC (Z80Context* ctx, int adjFlags, byte val)
{
    VALFLAG(F_C, (val & 0x01) != 0);
    val >>= 1;
    val |= ((byte)GETFLAG(F_C) << 7);

    adjustFlags(ctx, val);
    RESFLAG(F_H | F_N);

    if (adjFlags)
        adjustFlagSZP(ctx, val);

    return val;
}


static byte doRR (Z80Context* ctx, int adjFlags, byte val)
{
    int CY = GETFLAG(F_C);
    VALFLAG(F_C, (val & 0x01));
    val >>= 1;
    val |= (CY << 7);

    adjustFlags(ctx, val);
    RESFLAG(F_H | F_N);

    if (adjFlags)
        adjustFlagSZP(ctx, val);

    return val;
}


static byte doSL (Z80Context* ctx, byte val, int isArith)
{
    VALFLAG(F_C, (val & 0x80) != 0);
    val <<= 1;

    if (!isArith)
        val |= 1;

    adjustFlags(ctx, val);
    RESFLAG(F_H | F_N);
    adjustFlagSZP(ctx, val);

    return val;
}


static byte doSR (Z80Context* ctx, byte val, int isArith)
{
    int b = val & 0x80;

    VALFLAG(F_C, (val & 0x01) != 0);
    val >>= 1;

    if (isArith)
        val |= b;

    adjustFlags(ctx, val);
    RESFLAG(F_H | F_N);
    adjustFlagSZP(ctx, val);

    return val;
}


static void doPush (Z80Context* ctx, ushort val)
{
	WR.SP--;
	WR.SP--;
	write16(ctx, WR.SP, val);
}


static ushort doPop (Z80Context* ctx)
{
	ushort val;
	val = read16(ctx, WR.SP);
	WR.SP++;
	WR.SP++;
    return val;
}


static byte doCP_HL(Z80Context * ctx)
{
	byte val = read8(ctx, WR.HL);
	byte result = doArithmetic(ctx, val, 0, 1);	
	adjustFlags(ctx, val);
	return result;
}


/* The DAA opcode
 * According to the value in A and the flags set, add a value to A
 * This algorithm taken from:
 * http://www.worldofspectrum.org/faq/reference/z80reference.htm
 * and verified against the specification in the Zilog
 * Z80 Family CPU User Manual, rev. 04, Dec. 2004, pp. 166-167
 */	

static void doDAA(Z80Context * ctx) {
  int correction_factor = 0x00;
  int carry = 0;
  if(BR.A > 0x99 || GETFLAG(F_C)) {
    correction_factor |= 0x60;
    carry = 1;
  }
  if((BR.A & 0x0f) > 9 || GETFLAG(F_H))
    correction_factor |= 0x06;
  int a_before = BR.A;
  if(GETFLAG(F_N))
    BR.A -= correction_factor;
  else              
    BR.A += correction_factor;
  VALFLAG(F_H, (a_before ^ BR.A) & 0x10);
  VALFLAG(F_C, carry);
  VALFLAG(F_S, (BR.A & 0x80) != 0);
  VALFLAG(F_Z, (BR.A == 0));
  VALFLAG(F_PV, parityBit[BR.A]);
  adjustFlags(ctx, BR.A);
}
 
#include "codegen/opcodes_impl.c"


/* ---------------------------------------------------------
 *  The top-level functions
 * --------------------------------------------------------- 
 */ 


static void do_execute(Z80Context* ctx)
{
	const struct Z80OpcodeTable* current = &opcodes_main;
	const struct Z80OpcodeEntry* entries = current->entries;
	Z80OpcodeFunc func;
	
	byte opcode;
	int offset = 0;
	ctx->M1PC = ctx->PC;

	do
	{
		if (ctx->exec_int_vector)
		{
			opcode = ctx->int_vector;
			ctx->tstates += 6;
		}
		else
		{
			ctx->M1 = 1;
			opcode = read8(ctx, ctx->PC + offset);
			ctx->M1 = 0;
			ctx->PC++;
			ctx->tstates += 1;
		}

		INCR;
		func = entries[opcode].func;
		if (func != NULL)
		{			
			ctx->PC -= offset;
			if (ctx->trace)
				ctx->trace(ctx->memParam);
			func(ctx);
			ctx->PC += offset;
			break;
		}
		else if (entries[opcode].table != NULL)
		{
			current = entries[opcode].table;
			entries = current->entries;
			offset = current->opcode_offset;
			if (offset > 0)
				DECR;
		}

		else
		{
			/* NOP */
			break;	
		}
	} while(1);
}


static void unhalt(Z80Context* ctx)
{
    if (ctx->halted)
    {
        ctx->halted = 0;
        ctx->PC++;
    }
}


static void do_nmi(Z80Context* ctx)
{
	unhalt(ctx);
	ctx->IFF2 = ctx->IFF1;
	ctx->IFF1 = 0;
    doPush(ctx, ctx->PC);
	ctx->PC = 0x0066;	
	ctx->nmi_req = 0;
	ctx->tstates += 5;
}


static void do_int(Z80Context* ctx)
{
    unhalt(ctx);
	ctx->IFF1 = 0;
	ctx->IFF2 = 0;
	ctx->int_req = 0;
    if (ctx->IM == 0)
    {
		ctx->exec_int_vector = 1;
		do_execute(ctx);
		ctx->exec_int_vector = 0;
    }
    else if (ctx->IM == 1)
    {
        doPush(ctx, ctx->PC);
        ctx->PC = 0x0038;
		ctx->tstates += 7;
    }
    else if (ctx->IM == 2)
    {
        doPush(ctx, ctx->PC);
		ushort vector_address = (ctx->I << 8) | ctx->int_vector;
		ctx->PC = read16(ctx, vector_address);
		ctx->tstates += 7;
    }
}


void Z80Execute (Z80Context* ctx)
{
	if (ctx->nmi_req)
		do_nmi(ctx);
	else if (ctx->int_req && !ctx->defer_int && ctx->IFF1)
		do_int(ctx);
	else
	{
		ctx->defer_int = 0;
		do_execute(ctx);
	}
}


unsigned Z80ExecuteTStates(Z80Context* ctx, unsigned tstates)
{
	ctx->tstates = 0;
	while (ctx->tstates < tstates)
		Z80Execute(ctx);
	return ctx->tstates;
}


void Z80Debug (Z80Context* ctx, char* dump, char* decode)
{
	char tmp[20];	
	const struct Z80OpcodeTable* current = &opcodes_main;
	const struct Z80OpcodeEntry* entries = current->entries;
	char* fmt;
	byte opcode;
	ushort parm;
	int offset = 0;
	int PC = ctx->PC;
	int size = 0;
	
	if (dump)
		dump[0] = 0;
		
	if (decode)
		decode[0] = 0;

	do
	{
		opcode = read8(ctx, PC + offset);

		size++;
		
		PC++;
		fmt = entries[opcode].format;
		if (fmt != NULL)
		{			
			PC -= offset;
			parm = read16(ctx, PC);
		
			if (entries[opcode].operand_type == OP_NONE)
				size++;
			else
				size += 2;
			if (entries[opcode].operand_type != OP_WORD)
			{
				parm &= 0xFF;
				size--;
			}
				
			if (decode)
				sprintf(decode, fmt, parm);
			
			PC += offset;
			break;
		}
		else if (entries[opcode].table != NULL)
		{
			current = entries[opcode].table;
			entries = current->entries;
			offset = current->opcode_offset;
		}

		else
		{
			if (decode != NULL)
				strcpy(decode, "NOP (ignored)");
			break;	
		}
	} while(1);	
	
	if (dump)
	{
		for (offset = 0; offset < size; offset++)
		{
			sprintf(tmp, "%02X", read8(ctx, ctx->PC + offset));
			strcat(dump, tmp);
		}		
	}
}


void Z80RESET (Z80Context* ctx)
{
	ctx->PC = 0x0000;
	BR.F = 0;
	ctx->IM = 0;
	ctx->IFF1 = ctx->IFF2 = 0;
	ctx->R = 0;
	ctx->I = 0;
	ctx->halted = 0;
	ctx->tstates = 0;
	ctx->nmi_req = 0;
	ctx->int_req = 0;
	ctx->defer_int = 0;
	ctx->exec_int_vector = 0;
	ctx->M1 = 0;
}


void Z80INT (Z80Context* ctx, byte value)
{
	ctx->int_req = 1;
	ctx->int_vector = value;
}

/* Interrupt pin has been dropped - and since the interrupt is not edge
   triggered this matters on some emulations */
void Z80NOINT (Z80Context* ctx)
{
	ctx->int_req = 0;
}

/* NMI is edge triggered */

void Z80NMI (Z80Context* ctx)
{
	if (ctx->nmi_low)
		return;
	ctx->nmi_req = 1;
	ctx->nmi_low = 1;
}

void Z80NMI_Clear(Z80Context *ctx)
{
	ctx->nmi_low = 0;
}
