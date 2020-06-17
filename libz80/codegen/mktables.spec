#
# Z80 -> C specification


#
# Add / Sub / Adc / Sbc
#
(ADC|SBC|ADD|SUB) A,(A|B|C|D|E|H|L|IXh|IXl|IYh|IYl)
	BR.A = doArithmetic(ctx, BR.%2, F1_%1, F2_%1);

(ADC|SBC|ADD|SUB) A,\(HL\)
	BR.A = doArithmetic(ctx, read8(ctx, WR.HL), F1_%1, F2_%1);

(ADC|SBC|ADD|SUB) A,\((IX|IY)\+d\)
	ctx->tstates += 5;
	signed char displacement = read8(ctx, ctx->PC++);
	BR.A = doArithmetic(ctx, read8(ctx, WR.%2 + displacement), F1_%1, F2_%1);
	
(ADC|SBC|ADD|SUB) A,n
	BR.A = doArithmetic(ctx, read8(ctx, ctx->PC++), F1_%1, F2_%1);


# Particular cases
(ADC|ADD|SBC) HL,(SP|BC|DE|HL)
	ctx->tstates += 7;
	WR.HL = doAddWord(ctx, WR.HL, WR.%2, F1_%1, F2_%1);

ADD (IX|IY),(SP|BC|DE|IX|IY)
	ctx->tstates += 7;
	WR.%1 = doAddWord(ctx, WR.%1, WR.%2, 0, 0);

#
# Logic operations
#
(AND|XOR|OR) \(HL\)
	do%1(ctx, read8(ctx, WR.HL));

(AND|XOR|OR) \((IX|IY)\+d\)
	ctx->tstates += 5;
	do%1(ctx, read8(ctx, WR.%2 + (signed char) read8(ctx, ctx->PC++)));

(AND|XOR|OR) (A|B|C|D|E|H|L|IXh|IXl|IYh|IYl)
	do%1(ctx, BR.%2);

(AND|XOR|OR) n
	do%1(ctx, read8(ctx, ctx->PC++));


#
# Bit operations
#
BIT ([0-7]),(A|B|C|D|E|H|L)
	doBIT_r(ctx, %1, BR.%2);

BIT ([0-7]),\(HL\)
	ctx->tstates += 1;
	doBIT_r(ctx, %1, read8(ctx, WR.HL));

BIT ([0-7]),\((IX|IY)\+d\)
	ctx->tstates += 2;
	ushort address = WR.%2 + (signed char) read8(ctx, ctx->PC++);
	doBIT_indexed(ctx, %1, address);

(SET|RES) ([0-7]),(A|B|C|D|E|H|L)
	BR.%3 = doSetRes(ctx, SR_%1, %2, BR.%3);

(SET|RES) ([0-7]),\(HL\)
	ctx->tstates += 1;
	write8(ctx, WR.HL, doSetRes(ctx, SR_%1, %2, read8(ctx, WR.HL)));

(SET|RES) ([0-7]),\((IX|IY)\+d\)
	ctx->tstates += 2;
	signed char off = read8(ctx, ctx->PC++);
	write8(ctx, WR.%3 + off, doSetRes(ctx, SR_%1, %2, read8(ctx, WR.%3 + off)));
	
	
#
# Jumps and calls
#
CALL (C|M|NZ|NC|P|PE|PO|Z)?,?\(nn\)
	ushort addr = read16(ctx, ctx->PC);
	ctx->PC += 2;
	if (condition(ctx, C_%1))
	{
		ctx->tstates += 1;
		doPush(ctx, ctx->PC);
		ctx->PC = addr;
	}
	
JP \((HL|IX|IY)\)
	ctx->PC = WR.%1;
	
JP (C|M|NZ|NC|P|PE|PO|Z)?,?\(nn\)
	ushort addr = read16(ctx, ctx->PC);
	ctx->PC += 2;
	if (condition(ctx, C_%1))
		ctx->PC = addr;
	
JR (C|NZ|NC|Z)?,?\(PC\+e\)
	int off = doComplement(read8(ctx, ctx->PC++));
	if (condition(ctx, C_%1))
	{
		ctx->tstates += 5;
		ctx->PC += off;
	}

RETI
	ctx->IFF1 = ctx->IFF2;
	%RET		
		
RETN
	ctx->IFF1 = ctx->IFF2;
	%RET


RET (C|M|NZ|NC|P|PE|PO|Z)
	ctx->tstates += 1;
	if (condition(ctx, C_%1))
		ctx->PC = doPop(ctx);
		
RET
	ctx->PC = doPop(ctx);

	
DJNZ \(PC\+e\)
	ctx->tstates += 1;
	signed char off = read8(ctx, ctx->PC++);
	BR.B--;
	if (BR.B)
	{
		ctx->tstates += 5;
		ctx->PC += off;
	}


RST (0|8|10|18|20|28|30|38)H
	ctx->tstates += 1;
	doPush(ctx, ctx->PC);
	ctx->PC = 0x0%1;
	
	
#
# Misc
#
CCF
	VALFLAG(F_C, (1 - (byte)GETFLAG(F_C) != 0));
	RESFLAG(F_N);
	adjustFlags(ctx, BR.A);

SCF
	SETFLAG(F_C);
	RESFLAG(F_N | F_H);
	adjustFlags(ctx, BR.A);

CPL
	BR.A = ~BR.A;
	SETFLAG(F_H | F_N);
	adjustFlags(ctx, BR.A);
	
DAA
	doDAA(ctx);
	
EX \(SP\),(HL|IX|IY)
	ctx->tstates += 3;
	ushort tmp = read16(ctx, WR.SP);
	write16(ctx, WR.SP, WR.%1);
	WR.%1 = tmp;

EX AF,AF'
	ushort tmp = ctx->R1.wr.AF;
	ctx->R1.wr.AF = ctx->R2.wr.AF;
	ctx->R2.wr.AF = tmp;

EX DE,HL
	ushort tmp = WR.DE;
	WR.DE = WR.HL;
	WR.HL = tmp;

EXX
	ushort tmp;	
	tmp = ctx->R1.wr.BC;
	ctx->R1.wr.BC = ctx->R2.wr.BC;
	ctx->R2.wr.BC = tmp;	
	
	tmp = ctx->R1.wr.DE;
	ctx->R1.wr.DE = ctx->R2.wr.DE;
	ctx->R2.wr.DE = tmp;	
	
	tmp = ctx->R1.wr.HL;
	ctx->R1.wr.HL = ctx->R2.wr.HL;
	ctx->R2.wr.HL = tmp;

HALT
	ctx->halted = 1;
	ctx->PC--;

#
# Compare
#
CP \(HL\)
	doCP_HL(ctx);

CP \((IX|IY)\+d\)
	ctx->tstates += 5;
	signed char displacement = read8(ctx, ctx->PC++);
	byte val = read8(ctx, WR.%1 + displacement);
	doArithmetic(ctx, val, 0, 1);	
	adjustFlags(ctx, val);

CP (A|B|C|D|E|H|L|IXh|IXl|IYh|IYl)
	doArithmetic(ctx, BR.%1, 0, 1);	
	adjustFlags(ctx, BR.%1);

CP n
	byte val = read8(ctx, ctx->PC++);
	doArithmetic(ctx, val, 0, 1);	
	adjustFlags(ctx, val);

CPDR
	%CPD
	if (WR.BC != 0 && !GETFLAG(F_Z))
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

CPD
	ctx->tstates += 5;
	int carry = GETFLAG(F_C);
	byte value = doCP_HL(ctx);
	if(GETFLAG(F_H))
		value--;
	WR.HL--;
	WR.BC--;
	VALFLAG(F_PV, WR.BC != 0);
	if(carry)
		SETFLAG(F_C);
	else
		RESFLAG(F_C);
	VALFLAG(F_5, value & (1 << 1));
	VALFLAG(F_3, value & (1 << 3));

CPIR
	%CPI
	if (WR.BC != 0 && !GETFLAG(F_Z))
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

CPI
	ctx->tstates += 5;
	int carry = GETFLAG(F_C);
	byte value = doCP_HL(ctx);
	if(GETFLAG(F_H))
		value--;
	WR.HL++;
	WR.BC--;
	VALFLAG(F_PV, WR.BC != 0);
	VALFLAG(F_C, carry);
	VALFLAG(F_5, value & (1 << 2));
	VALFLAG(F_3, value & (1 << 3));

#
# INC and DEC
#
(INC|DEC) \(HL\)
	ctx->tstates += 1;
	byte value = read8(ctx, WR.HL);
	write8(ctx, WR.HL, doIncDec(ctx, value, ID_%1));

(INC|DEC) \((IX|IY)\+d\)
	ctx->tstates += 6;
	signed char off = read8(ctx, ctx->PC++);
	byte value = read8(ctx, WR.%2 + off);
	write8(ctx, WR.%2 + off, doIncDec(ctx, value, ID_%1));
	
(INC|DEC) (A|B|C|D|E|H|L|IXh|IXl|IYh|IYl)
	BR.%2 = doIncDec(ctx, BR.%2, ID_%1);

INC (BC|DE|HL|SP|IX|IY)
	ctx->tstates += 2;
	WR.%1++;

DEC (BC|DE|HL|SP|IX|IY)
	ctx->tstates += 2;
	WR.%1--;

#
# Interrupts
#
(EI|DI)
	ctx->IFF1 = ctx->IFF2 = IE_%1;
	ctx->defer_int = 1;

IM ([012])
	ctx->IM = %1;


#
# IO ports
#
IN (A|B|C|D|E|F|H|L),\(C\)
	BR.%1 = ioRead(ctx, WR.BC);
	RESFLAG(F_H | F_N);
	adjustFlagSZP(ctx, BR.%1);
	adjustFlags(ctx, BR.%1);

IN A,\(n\)
	byte port = read8(ctx, ctx->PC++);	
	BR.A = ioRead(ctx, BR.A << 8 | port);

INDR
	%IND
	if (BR.B != 0)
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

IND
	ctx->tstates += 1;
	byte val = ioRead(ctx, WR.BC);
	write8(ctx, WR.HL, val);
	WR.HL--;
	BR.B = doIncDec(ctx, BR.B, ID_DEC);
	VALFLAG(F_N, (val & 0x80) != 0);
	int flagval = val + ((BR.C - 1) & 0xff);
	VALFLAG(F_H, flagval > 0xff);
	VALFLAG(F_C, flagval > 0xff);
	VALFLAG(F_PV, parityBit[(flagval & 7) ^ BR.B]);

INIR
	%INI
	if (BR.B != 0)
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

INI
	ctx->tstates += 1;
	byte val = ioRead(ctx, WR.BC);
	write8(ctx, WR.HL, val);
	WR.HL++;
	BR.B = doIncDec(ctx, BR.B, ID_DEC);
	VALFLAG(F_N, (val & 0x80) != 0);
	int flagval = val + ((BR.C + 1) & 0xff);
	VALFLAG(F_H, flagval > 0xff);
	VALFLAG(F_C, flagval > 0xff);
	VALFLAG(F_PV, parityBit[(flagval & 7) ^ BR.B]);

#
# Loads
#
LD \((BC|DE|HL)\),A
	write8(ctx, WR.%1, BR.A);

LD \(HL\),(B|C|D|E|H|L)
	write8(ctx, WR.HL, BR.%1);

LD \(HL\),n
	write8(ctx, WR.HL, read8(ctx, ctx->PC++));
	
LD \((IX|IY)\+d\),(A|B|C|D|E|H|L)
	ctx->tstates += 5;
	write8(ctx, WR.%1 + (signed char) read8(ctx, ctx->PC++), BR.%2);
	
LD \((IX|IY)\+d\),n
	ctx->tstates += 2;
	signed char offset = read8(ctx, ctx->PC++);
	byte n = read8(ctx, ctx->PC++);
	write8(ctx, WR.%1 + offset, n);
	
LD \(nn\),A
	write8(ctx, read16(ctx, ctx->PC), BR.A);
	ctx->PC += 2;
	
LD (BC|DE|HL|IX|IY|SP),(BC|DE|HL|IX|IY|SP)
	ctx->tstates += 2;
	WR.%1 = WR.%2;

LD \(nn\),(BC|DE|HL|IX|IY|SP)
	write16(ctx, read16(ctx, ctx->PC), WR.%1);
	ctx->PC += 2;
	
LD A,\((BC|DE)\)
	BR.A = read8(ctx, WR.%1);

LD (A|B|C|D|E|H|L),\(HL\)
	BR.%1 = read8(ctx, WR.HL);

LD (A|B|C|D|E|H|L),\((IX|IY)\+d\)
	ctx->tstates += 5;
	BR.%1 = read8(ctx, WR.%2 + (signed char) read8(ctx, ctx->PC++));

LD (A|B|C|D|E|H|L),\(nn\)
	BR.%1 = read8(ctx, read16(ctx, ctx->PC));
	ctx->PC += 2;
	
LD (A|B|C|D|E|H|L|IXh|IXl|IYh|IYl),(A|B|C|D|E|H|L|IXh|IXl|IYh|IYl)
	BR.%1 = BR.%2;

LD (A|B|C|D|E|H|L),(SL|SR)A \((IX|IY)\+d\)
	ctx->tstates += 2;
	signed char off = read8(ctx, ctx->PC++);
	BR.%1 = do%2(ctx, read8(ctx, WR.%3 + off), 1);
	write8(ctx, WR.%3 + off, BR.%1);	
	
LD (A|B|C|D|E|H|L),(SL|SR)L \((IX|IY)\+d\)
	ctx->tstates += 2;
	signed char off = read8(ctx, ctx->PC++);
	BR.%1 = do%2(ctx, read8(ctx, WR.%3 + off), 0);
	write8(ctx, WR.%3 + off, BR.%1);	
	  
LD (A|B|C|D|E|H|L),(RL|RLC|RR|RRC) \((IX|IY)\+d\)
	ctx->tstates += 2;
	signed char off = read8(ctx, ctx->PC++);
	BR.%1 = do%2(ctx, 1, read8(ctx, WR.%3 + off));
	write8(ctx, WR.%3 + off, BR.%1);

LD (A|B|C|D|E|H|L),(SET|RES) ([0-7]),\((IX|IY)\+d\)
	ctx->tstates += 2;
	signed char off = read8(ctx, ctx->PC++);
	BR.%1 = doSetRes(ctx, SR_%2, %3, read8(ctx, WR.%4 + off));
	write8(ctx, WR.%4 + off, BR.%1);	

LD A,(I|R)
	ctx->tstates += 1;
	BR.A = ctx->%1;
	adjustFlags(ctx, BR.A);
	RESFLAG(F_H | F_N);
	VALFLAG(F_PV, ctx->IFF2);
	VALFLAG(F_S, (BR.A & 0x80) != 0);
	VALFLAG(F_Z, (BR.A == 0));
	
LD (I|R),A
	ctx->tstates += 1;
	ctx->%1 = BR.A;

LD (A|B|C|D|E|H|L|IXh|IXl|IYh|IYl),n
	BR.%1 = read8(ctx, ctx->PC++);
	
LD (BC|DE|HL|SP|IX|IY),\(nn\)
	ushort addr = read16(ctx, ctx->PC);
	ctx->PC += 2;
	WR.%1 = read16(ctx, addr);	

LD (BC|DE|HL|SP|IX|IY),nn
	WR.%1 = read16(ctx, ctx->PC);
	ctx->PC += 2;
	

LDIR
	%LDI
	if (WR.BC != 0)
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

LDI
	ctx->tstates += 2;
	byte val = read8(ctx, WR.HL);
	write8(ctx, WR.DE, val);
	WR.DE++;
	WR.HL++;
	WR.BC--;
	VALFLAG(F_5, (BR.A + val) & 0x02);
	VALFLAG(F_3, ((BR.A + val) & F_3) != 0);
	RESFLAG(F_H | F_N);
	VALFLAG(F_PV, WR.BC != 0);

LDDR
	%LDD
	if (WR.BC != 0)
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

LDD
	ctx->tstates += 2;
	byte val = read8(ctx, WR.HL);
	write8(ctx, WR.DE, val);
	WR.DE--;
	WR.HL--;
	WR.BC--;
	VALFLAG(F_5, ((BR.A + val) & 0x02) != 0);
	VALFLAG(F_3, ((BR.A + val) & F_3) != 0);
	RESFLAG(F_H | F_N);
	VALFLAG(F_PV, WR.BC != 0);

NEG
	int temp = BR.A;
	BR.A = 0;
	BR.A = doArithmetic(ctx, temp, 0, 1);
	SETFLAG(F_N);

NOP
	/* NOP */
	

OUTI
	ctx->tstates += 1;
	byte value = read8(ctx, WR.HL);
	BR.B = doIncDec(ctx, BR.B, 1);
	ioWrite(ctx, WR.BC, value);
	WR.HL++;
	int flag_value = value + BR.L;
	VALFLAG(F_N, value & 0x80);
	VALFLAG(F_H, flag_value > 0xff);
	VALFLAG(F_C, flag_value > 0xff);
	VALFLAG(F_PV, parityBit[(flag_value & 7) ^ BR.B]);
	adjustFlags(ctx, BR.B);

OTIR
	%OUTI
	if (BR.B != 0)
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

OUTD
	ctx->tstates += 1;
	byte value = read8(ctx, WR.HL);
	BR.B = doIncDec(ctx, BR.B, 1);
	ioWrite(ctx, WR.BC, value);
	WR.HL--;
	int flag_value = value + BR.L;
	VALFLAG(F_N, value & 0x80);
	VALFLAG(F_H, flag_value > 0xff);
	VALFLAG(F_C, flag_value > 0xff);
	VALFLAG(F_PV, parityBit[(flag_value & 7) ^ BR.B]);
	adjustFlags(ctx, BR.B);

OTDR
	%OUTD
	if (BR.B != 0)
	{
		ctx->tstates += 5;
		ctx->PC -= 2;
	}

OUT \(C\),0
	ioWrite(ctx, WR.BC, 0);
	
OUT \(C\),(A|B|C|D|E|H|L)
	ioWrite(ctx, WR.BC, BR.%1);

OUT \(n\),A
	ioWrite(ctx, BR.A << 8 | read8(ctx, ctx->PC++), BR.A);

POP (AF|BC|DE|HL|IX|IY)
	WR.%1 = doPop(ctx);

PUSH (AF|BC|DE|HL|IX|IY)
	ctx->tstates += 1;
	doPush(ctx, WR.%1);


#
# Rotate & shift
#

(RLC|RRC|RL|RR) \(HL\)
	ctx->tstates += 1;
	write8(ctx, WR.HL, do%1(ctx, 1, read8(ctx, WR.HL)));
	
(RLC|RRC|RL|RR) (A|B|C|D|E|H|L|IXh|IXl|IYh|IYl)
	BR.%2 = do%1(ctx, 1, BR.%2);

(RLC|RRC|RL|RR) \((IX|IY)\+d\)
	ctx->tstates += 2;
	signed char off = read8(ctx, ctx->PC++);
	write8(ctx, WR.%2 + off, do%1(ctx, 1, read8(ctx, WR.%2 + off)));

(RL|RR|RLC|RRC)A
	BR.A = do%1(ctx, 0, BR.A);
	
	
RLD
	ctx->tstates += 4;
	byte Ah = BR.A & 0x0f;
	byte hl = read8(ctx, WR.HL);
	BR.A = (BR.A & 0xf0) | ((hl & 0xf0) >> 4);
	hl = (hl << 4) | Ah;
	write8(ctx, WR.HL, hl);
	RESFLAG(F_H | F_N);
	adjustFlagSZP(ctx, BR.A);
	adjustFlags(ctx, BR.A);


RRD
	ctx->tstates += 4;
	byte Ah = BR.A & 0x0f;
	byte hl = read8(ctx, WR.HL);
	BR.A = (BR.A & 0xf0) | (hl & 0x0f);
	hl = (hl >> 4) | (Ah << 4);
	write8(ctx, WR.HL, hl);
	RESFLAG(F_H | F_N);
	adjustFlagSZP(ctx, BR.A);


(SL|SR)(L|A) \(HL\)
	ctx->tstates += 1;
	write8(ctx, WR.HL, do%1(ctx, read8(ctx, WR.HL), IA_%2));


(SL|SR)(L|A) \((IX|IY)\+d\)
	ctx->tstates += 2;
	signed char off = read8(ctx, ctx->PC++);
	write8(ctx, WR.%3 + off, do%1(ctx, read8(ctx, WR.%3 + off), IA_%2));

(SL|SR)(L|A) (A|B|C|D|E|H|L|IXh|IXl|IYh|IYl)
	BR.%3 = do%1(ctx, BR.%3, IA_%2);
	
