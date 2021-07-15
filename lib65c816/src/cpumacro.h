/*
 * lib65816/cpumacro.h Release 1p1
 * See LICENSE for more details.
 *
 * Code originally from XGS: Apple IIGS Emulator (cpumacro.h)
 *
 * Originally written and Copyright (C)1996 by Joshua M. Thompson
 * Copyright (C) 2006 by Samuel A. Falvo II
 *
 * Modified for greater portability and virtual hardware independence.
 */

/* This file contains the macros which are affected by the E, M, or X	*/
/* bits (as opposed to the macros in micros.h, which aren't affected).	*/

/* First some #undef's to prevent a lot of annoying compiler warnings. */

#undef C_LDA
#undef C_LDX
#undef C_LDY
#undef C_STA
#undef C_STX
#undef C_STY
#undef C_STZ
#undef C_INC
#undef C_DEC
#undef C_ASL
#undef C_LSR
#undef C_ROL
#undef C_ROR
#undef C_TSB
#undef C_TRB
#undef C_AND
#undef C_ORA
#undef C_EOR
#undef C_BIT
#undef C_ADC
#undef C_SBC
#undef C_CMP
#undef C_CPX
#undef C_CPY
#undef S_PUSH
#undef S_PULL
#undef O_i8
#undef O_i16
#undef O_a
#undef O_al
#undef O_d
#undef O_dix
#undef O_dixl
#undef O_dxi
#undef O_dxx
#undef O_dxy
#undef O_axx
#undef O_axy
#undef O_alxx
#undef O_pcr
#undef O_pcrl
#undef O_ai
#undef O_ail
#undef O_di
#undef O_dil
#undef O_axi
#undef O_sr
#undef O_srix

/*------- Routines that operate on a memory address -------*/

#ifdef SHORT_M
#define C_LDA(a)	otmp.B.L = M_READ(a.A);		\
			C_LDA8(otmp.B.L)
#else
#define C_LDA(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_LDA16(otmp.W)
#endif

#ifdef SHORT_X
#define C_LDX(a)	otmp.B.L = M_READ(a.A);		\
			C_LDX8(otmp.B.L)
#else
#define C_LDX(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_LDX16(otmp.W)
#endif

#ifdef SHORT_X
#define C_LDY(a)	otmp.B.L = M_READ(a.A);		\
			C_LDY8(otmp.B.L)
#else
#define C_LDY(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_LDY16(otmp.W)
#endif

#ifdef SHORT_M
#define C_STA(a)	M_WRITE(a.A,A.B.L)
#else
#define C_STA(a)	M_WRITE(a.A,A.B.L);	\
			M_WRITE(a.A+1,A.B.H)
#endif

#ifdef SHORT_X
#define C_STX(a)	M_WRITE(a.A,X.B.L)
#else
#define C_STX(a)	M_WRITE(a.A,X.B.L);	\
			M_WRITE(a.A+1,X.B.H)
#endif

#ifdef SHORT_X
#define C_STY(a)	M_WRITE(a.A,Y.B.L)
#else
#define C_STY(a)	M_WRITE(a.A,Y.B.L);	\
			M_WRITE(a.A+1,Y.B.H)
#endif

#ifdef SHORT_M
#define C_STZ(a)	M_WRITE(a.A,0)
#else
#define C_STZ(a)	M_WRITE(a.A,0);		\
			M_WRITE(a.A+1,0)
#endif

#ifdef SHORT_M
#define C_INC(a)	otmp.B.L = M_READ(a.A);		\
			C_INC8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_INC(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_INC16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_DEC(a)	otmp.B.L = M_READ(a.A);		\
			C_DEC8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_DEC(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_DEC16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_ASL(a)	otmp.B.L = M_READ(a.A);		\
			C_ASL8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_ASL(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_ASL16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_LSR(a)	otmp.B.L = M_READ(a.A);		\
			C_LSR8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_LSR(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_LSR16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_ROL(a)	otmp.B.L = M_READ(a.A);		\
			C_ROL8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_ROL(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_ROL16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_ROR(a)	otmp.B.L = M_READ(a.A);		\
			C_ROR8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_ROR(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_ROR16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_TSB(a)	otmp.B.L = M_READ(a.A);		\
			C_TSB8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_TSB(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_TSB16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_TRB(a)	otmp.B.L = M_READ(a.A);		\
			C_TRB8(otmp.B.L);		\
			M_WRITE(a.A,otmp.B.L)
#else
#define C_TRB(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_TRB16(otmp.W);		\
			M_WRITE(a.A,otmp.B.L);		\
			M_WRITE(a.A+1,otmp.B.H)
#endif

#ifdef SHORT_M
#define C_AND(a)	otmp.B.L = M_READ(a.A);		\
			C_AND8(otmp.B.L)
#else
#define C_AND(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_AND16(otmp.W)
#endif

#ifdef SHORT_M
#define C_ORA(a)	otmp.B.L = M_READ(a.A);		\
			C_ORA8(otmp.B.L)
#else
#define C_ORA(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_ORA16(otmp.W)
#endif

#ifdef SHORT_M
#define C_EOR(a)	otmp.B.L = M_READ(a.A);		\
			C_EOR8(otmp.B.L)
#else
#define C_EOR(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_EOR16(otmp.W)
#endif

#ifdef SHORT_M
#define C_BIT(a)	otmp.B.L = M_READ(a.A);		\
			C_BIT8(otmp.B.L)
#else
#define C_BIT(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_BIT16(otmp.W)
#endif

#ifdef SHORT_M
#define C_ADC(a)	otmp.B.L = M_READ(a.A);		\
			C_ADC8(otmp.B.L)
#else
#define C_ADC(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_ADC16(otmp.W)
#endif

#ifdef SHORT_M
#define C_SBC(a)	otmp.B.L = M_READ(a.A);		\
			C_SBC8(otmp.B.L)
#else
#define C_SBC(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_SBC16(otmp.W)
#endif

#ifdef SHORT_M
#define C_CMP(a)	otmp.B.L = M_READ(a.A);		\
			C_CMP8(otmp.B.L)
#else
#define C_CMP(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_CMP16(otmp.W)
#endif

#ifdef SHORT_X
#define C_CPX(a)	otmp.B.L = M_READ(a.A);		\
			C_CPX8(otmp.B.L)
#else
#define C_CPX(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_CPX16(otmp.W)
#endif

#ifdef SHORT_X
#define C_CPY(a)	otmp.B.L = M_READ(a.A);		\
			C_CPY8(otmp.B.L)
#else
#define C_CPY(a)	otmp.B.L = M_READ(a.A);		\
			otmp.B.H = M_READ(a.A+1);	\
			C_CPY16(otmp.W)
#endif

/* Macros for pushing or pulling bytes on the 65816 stack */

#ifdef NATIVE_MODE
#define S_PUSH(v)	M_WRITE(S.W,v); S.W--
#define S_PULL(v)	++S.W; v = M_READ(S.W)
#else
#define S_PUSH(v)	M_WRITE(S.W,v); S.B.L--
#define S_PULL(v)	S.B.L++; v = M_READ(S.W)
#endif

/* Macros to retrieve an 8 or 16-bit operand. They take as their parameter	*/
/* a "dualw" union variable, which they set to the operand.			*/

#define O_i8(v)		v.B.L = M_READ(PC.A); v.B.H = 0; PC.W.PC++

#define O_i16(v)	v.B.L = M_READ(PC.A); v.B.H = M_READ(PC.A+1); PC.W.PC += 2

/* Macros to retrieve the operand address. These take as their parameter	*/
/* a "duala" union variable, which they set to the operand address.		*/

#define O_a(a)		a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = DB; PC.W.PC += 2;

#define O_al(a)		a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = M_READ(PC.A+2); PC.W.PC += 3

#define O_d(a)		if (D.B.L) cpu_cycle_count++;			\
			a.A = D.W + M_READ(PC.A); a.B.B = 0; PC.W.PC++

#ifdef SHORT_X

#define O_dix(a)	if (D.B.L) cpu_cycle_count++;					\
			atmp.A = D.W + M_READ(PC.A); atmp.B.B = 0; PC.W.PC++;		\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = DB;	\
			atmp.A = a.A;							\
			a.A += Y.W;							\
			if (atmp.B.H != a.B.H) cpu_cycle_count++;

#else

#define O_dix(a)	if (D.B.L) cpu_cycle_count++;					\
			atmp.A = D.W + M_READ(PC.A); atmp.B.B = 0; PC.W.PC++;		\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = DB;	\
			a.A += Y.W;

#endif

#define O_dixl(a)	if (D.B.L) cpu_cycle_count++;							\
			atmp.A = D.W + M_READ(PC.A); atmp.B.B = 0; PC.W.PC++;				\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = M_READ(atmp.A+2);	\
			a.A += Y.W
			
#define O_dxi(a)	if (D.B.L) cpu_cycle_count++;					\
			atmp.A = D.W + M_READ(PC.A) + X.W; atmp.B.B = 0; PC.W.PC++;	\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = DB

#ifdef NATIVE_MODE

#define O_dxx(a)	if (D.B.L) cpu_cycle_count++;				\
			a.W.L = (M_READ(PC.A) + D.W + X.W); a.B.B = 0; PC.W.PC++

#define O_dxy(a)	if (D.B.L) cpu_cycle_count++;				\
			a.W.L = (M_READ(PC.A) + D.W + Y.W); a.B.B = 0; PC.W.PC++

#ifdef SHORT_X

#define O_axx(a)	a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = DB;	\
			atmp.A = a.A; a.A += X.W;					\
			if (atmp.B.H != a.B.H) cpu_cycle_count++;			\
			PC.W.PC+=2

#define O_axy(a)	a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = DB;	\
			atmp.A = a.A; a.A += Y.W;					\
			if (atmp.B.H != a.B.H) cpu_cycle_count++;			\
			PC.W.PC+=2
#else

#define O_axx(a)	a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = DB;	\
			a.A += X.W;							\
			PC.W.PC+=2

#define O_axy(a)	a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = DB;	\
			a.A += Y.W;							\
			PC.W.PC+=2
#endif

#else

#define O_dxx(a)	if (D.B.L) cpu_cycle_count++;					\
			a.W.L = (M_READ(PC.A) + D.W + X.W); a.B.H = 0; a.B.B = 0; PC.W.PC++;

#define O_dxy(a)	if (D.B.L) cpu_cycle_count++;					\
			a.W.L = (M_READ(PC.A) + D.W + Y.W); a.B.H = 0; a.B.B = 0; PC.W.PC++;

#define O_axx(a)	a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = DB;	\
			atmp.A = a.A; a.W.L += X.W;					\
			if (atmp.B.H != a.B.H) cpu_cycle_count++;			\
			PC.W.PC+=2

#define O_axy(a)	a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = DB;	\
			atmp.A = a.A; a.W.L += Y.W;					\
			if (atmp.B.H != a.B.H) cpu_cycle_count++;			\
			PC.W.PC+=2

#endif

#define O_alxx(a)	a.B.L = M_READ(PC.A); a.B.H = M_READ(PC.A+1); a.B.B = M_READ(PC.A+2); a.A += X.W; \
			PC.W.PC += 3;

#define O_pcr(a)	wtmp.B.L = M_READ(PC.A); PC.W.PC++;		\
			a.W.L = PC.W.PC + (offset_s) wtmp.B.L; a.B.B = PC.B.PB;

#define O_pcrl(a)	wtmp.B.L = M_READ(PC.A); wtmp.B.H = M_READ(PC.A+1); PC.W.PC += 2;	\
			a.W.L = PC.W.PC + (offset_l) wtmp.W; a.B.B = PC.B.PB;

#define O_ai(a)		atmp.B.L = M_READ(PC.A); atmp.B.H = M_READ(PC.A+1); atmp.B.B = 0; PC.W.PC += 2;	\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = PC.B.PB;

#define O_ail(a)	atmp.B.L = M_READ(PC.A); atmp.B.H = M_READ(PC.A+1); atmp.B.B = 0; PC.W.PC += 2;	\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = M_READ(atmp.A+2)

#define O_di(a)		if (D.B.L) cpu_cycle_count++;					\
			atmp.A = M_READ(PC.A) + D.W; PC.W.PC++;				\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = DB

#define O_dil(a)	if (D.B.L) cpu_cycle_count++;							\
			atmp.A = M_READ(PC.A) + D.W; PC.W.PC++;						\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = M_READ(atmp.A+2)

#define O_axi(a)	atmp.B.L = M_READ(PC.A); atmp.B.H = M_READ(PC.A+1);				\
			atmp.A += X.W; atmp.B.B = 0;						 \
			PC.W.PC += 2; a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = PC.B.PB

#define O_sr(a)		a.W.L = M_READ(PC.A) + S.W; a.B.B = 0; PC.W.PC++

#define O_srix(a)	atmp.W.L = M_READ(PC.A) + S.W; atmp.B.B = 0; PC.W.PC++;				\
			a.B.L = M_READ(atmp.A); a.B.H = M_READ(atmp.A+1); a.B.B = DB; a.A += Y.W
