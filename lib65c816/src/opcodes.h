/*
 * lib65816/opcodes.h Release 1p1
 * See LICENSE for more details.
 *
 * Code originally from XGS: Apple IIGS Emulator (opcodes.h)
 *
 * Originally written and Copyright (C)1996 by Joshua M. Thompson
 * Copyright (C) 2006 by Samuel A. Falvo II
 *
 * Modified for greater portability and virtual hardware independence.
 */

#include <lib65816/cpu.h>
#include "cpumacro.h"
#include "cpumicro.h"
#include "cycles.h"

extern byte	opcode;
extern int	opcode_offset;
extern duala	atmp,opaddr;
extern dualw	wtmp,otmp,operand;
extern int	a1,a2,a3,a4,o1,o2,o3,o4;

extern int	cpu_reset,cpu_abort,cpu_nmi,cpu_irq,cpu_stop,cpu_wait,cpu_trace;
extern int	cpu_update_period;

#define BEGIN_CPU_FUNC(command) XCPU(CPUMODE,command)
#define XCPU(mode,command) XXCPU(mode,command)
#ifdef OLDCYCLES
#define XXCPU(mode,command) void mode ## _ ## command (void); \
			    void mode ## _ ## command (void) {
#else
#define XXCPU(mode,command) void mode ## _ ## command (void); \
			    void mode ## _ ## command (void) { \
	cpu_cycle_count += mode ## _ ## command ## _cycles;
#endif

#define END_CPU_FUNC }

/* This file contains all 260 of the opcode subroutines */

BEGIN_CPU_FUNC(opcode_0x00)					/* BRK s */
    PC.W.PC++;
#ifdef NATIVE_MODE
	S_PUSH(PC.B.PB);
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH(P);
	F_setD(0);
	F_setI(1);
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFE6);
	PC.B.H = M_READ_VECTOR(0xFFE7);
#else
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH(P | 0x30);
	F_setD(0);
	F_setI(1);
	F_setB(0);
	DB = 0;
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFFE);
	PC.B.H = M_READ_VECTOR(0xFFFF);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x01)
	O_dxi(opaddr);					/* ORA (d,x) */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x02)					/* COP s */
    PC.W.PC++;
#ifdef NATIVE_MODE
	S_PUSH(PC.B.PB);
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH(P);
	F_setD(0);
	F_setI(1);
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFE4);
	PC.B.H = M_READ_VECTOR(0xFFE5);
#else
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH((byte) ((P & ~0x10) | 0x20));
	F_setD(0);
	F_setI(1);
	DB = 0;
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFF4);
	PC.B.H = M_READ_VECTOR(0xFFF5);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x03)
	O_sr(opaddr);					/* ORA d,s */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x04)
	O_d(opaddr);					/* TSB d */
	C_TSB(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x05)
	O_d(opaddr);					/* ORA d */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x06)
	O_d(opaddr);					/* ASL d */
	C_ASL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x07)
	O_dil(opaddr);					/* ORA [d] */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x08)
#ifdef NATIVE_MODE
	S_PUSH(P);					/* PHP s */
#else
    S_PUSH(P | 0x30);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x09)
#ifdef SHORT_M						/* ORA # */
	O_i8(operand);
	C_ORA8(operand.B.L);
#else
	O_i16(operand);
	C_ORA16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x0A)
#ifdef SHORT_M						/* ASL A */
	C_ASL8(A.B.L);
#else
	C_ASL16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x0B)
	S_PUSH(D.B.H);					/* PHD s */
	S_PUSH(D.B.L);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x0C)
	O_a(opaddr);					/* TSB a */
	C_TSB(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x0D)
	O_a(opaddr);					/* ORA a */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x0E)
	O_a(opaddr);					/* ASL a */
	C_ASL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x0F)
	O_al(opaddr);					/* ORA al */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x10)
	O_pcr(opaddr);					/* BPL r */
	if (!F_getN) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x11)
	O_dix(opaddr);					/* ORA (d),y */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x12)
	O_di(opaddr);					/* ORA (d) */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x13)
	O_srix(opaddr);					/* ORA (d,s),y */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x14)
	O_d(opaddr);					/* TRB d */
	C_TRB(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x15)
	O_dxx(opaddr);					/* ORA d,x */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x16)
	O_dxx(opaddr);					/* ASL d,x */
	C_ASL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x17)
	O_dixl(opaddr);					/* ORA [d],y */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x18)
	F_setC(0);					/* CLC i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x19)
	O_axy(opaddr);					/* ORA a,y */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x1A)
#ifdef SHORT_M						/* INC A */
	C_INC8(A.B.L);
#else
	C_INC16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x1B)
#ifdef NATIVE_MODE					/* TCS i */
	S.W = A.W;
#else
	S.B.L = A.B.L;
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x1C)
	O_a(opaddr);					/* TRB a */
	C_TRB(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x1D)
	O_axx(opaddr);					/* ORA a,x */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x1E)
	O_axx(opaddr);					/* ASL a,x */
	C_ASL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x1F)
	O_alxx(opaddr);					/* ORA al,x */
	C_ORA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x20)
	O_a(opaddr);					/* JSR a */
	PC.W.PC--;
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	PC.W.PC = opaddr.W.L;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x21)
	O_dxi(opaddr);					/* AND (d,x) */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x22)
	O_al(opaddr);					/* JSL al */
	S_PUSH(PC.B.PB);
	PC.W.PC--;
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	PC.A = opaddr.A;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x23)
	O_sr(opaddr);					/* AND d,s */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x24)
	O_d(opaddr);					/* BIT d */
	C_BIT(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x25)
	O_d(opaddr);					/* AND d */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x26)
	O_d(opaddr);					/* ROL d */
	C_ROL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x27)
	O_dil(opaddr);					/* AND [d] */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x28)
#ifdef NATIVE_MODE
	S_PULL(P);					/* PLP s */
#else
    byte tmpP;
    S_PULL(tmpP);
    P = (tmpP & ~0x30) | (P & 0x30);
#endif
	CPU_modeSwitch();
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x29)
#ifdef SHORT_M						/* AND # */
	O_i8(operand);
	C_AND8(operand.B.L);
#else
	O_i16(operand);
	C_AND16(operand.W);
#endif
END_CPU_FUNC
BEGIN_CPU_FUNC(opcode_0x2A)
#ifdef SHORT_M						/* ROL A */
	C_ROL8(A.B.L);
#else
	C_ROL16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x2B)
	S_PULL(D.B.L);					/* PLD s */
	S_PULL(D.B.H);
	C_SETF16(D.W);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x2C)
	O_a(opaddr);					/* BIT a */
	C_BIT(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x2D)
	O_a(opaddr);					/* AND a */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x2E)
	O_a(opaddr);					/* ROL a */
	C_ROL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x2F)
	O_al(opaddr);					/* AND al */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x30)
	O_pcr(opaddr);					/* BMI r */
	if (F_getN) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x31)
	O_dix(opaddr);					/* AND (d),y */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x32)
	O_di(opaddr);					/* AND (d) */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x33)
	O_srix(opaddr);					/* AND (d,s),y */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x34)
	O_dxx(opaddr);					/* BIT d,x */
	C_BIT(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x35)
	O_dxx(opaddr);					/* AND d,x */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x36)
	O_dxx(opaddr);					/* ROL d,x */
	C_ROL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x37)
	O_dixl(opaddr);					/* AND [d],y */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x38)
	F_setC(1);					/* SEC i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x39)
	O_axy(opaddr);					/* AND a,y */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x3A)
#ifdef SHORT_M						/* DEC A */
	C_DEC8(A.B.L);
#else
	C_DEC16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x3B)
	C_LDA16(S.W);					/* TSC i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x3C)
	O_axx(opaddr);					/* BIT a,x */
	C_BIT(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x3D)
	O_axx(opaddr);					/* AND a,x */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x3E)
	O_axx(opaddr);					/* ROL a,x */
	C_ROL(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x3F)
	O_alxx(opaddr);					/* AND al,x */
	C_AND(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x40)
	S_PULL(P);					/* RTI */
	S_PULL(PC.B.L);
	S_PULL(PC.B.H);
#ifdef NATIVE_MODE
	S_PULL(PC.B.PB);
#else
	P |= 0x30;
	cpu_cycle_count--;
#endif
	CPU_modeSwitch();
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x41)
	O_dxi(opaddr);					/* EOR (d,x) */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x42)
	O_i8(operand);
	E_WDM(operand.B.L);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x43)
	O_sr(opaddr);					/* EOR d,s */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x44)
	DB = M_READ(PC.A);				/* MVP xyc */
	operand.B.L = M_READ(PC.A+1);
	if (A.W != 0xFFFF) {
		M_WRITE((DB << 16) | Y.W,M_READ((operand.B.L << 16) | X.W));
		A.W--;
		X.W--;
		Y.W--;
		PC.W.PC--;
	} else {
		PC.W.PC += 2;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x45)
	O_d(opaddr);					/* EOR d */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x46)
	O_d(opaddr);					/* LSR d */
	C_LSR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x47)
	O_dil(opaddr);					/* EOR [d] */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x48)
#ifdef SHORT_M						/* PHA */
	S_PUSH(A.B.L);
#else
	S_PUSH(A.B.H);
	S_PUSH(A.B.L);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x49)
#ifdef SHORT_M						/* EOR # */
	O_i8(operand);
	C_EOR8(operand.B.L);
#else
	O_i16(operand);
	C_EOR16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x4A)
#ifdef SHORT_M						/* LSR A */
	C_LSR8(A.B.L);
#else
	C_LSR16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x4B)
	S_PUSH(PC.B.PB);				/* PHK */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x4C)
	O_a(opaddr);					/* JMP a */
	PC.W.PC = opaddr.W.L;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x4D)
	O_a(opaddr);					/* EOR a */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x4E)
	O_a(opaddr);					/* LSR a */
	C_LSR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x4F)
	O_al(opaddr);					/* EOR al */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x50)
	O_pcr(opaddr);					/* BVC r */
	if (!F_getV) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x51)
	O_dix(opaddr);					/* EOR (d),y */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x52)
	O_di(opaddr);					/* EOR (d) */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x53)
	O_srix(opaddr);					/* EOR (d,s),y */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x54)
	DB = M_READ(PC.A);				/* MVN xyc */
	operand.B.L = M_READ(PC.A+1);
	if (A.W != 0xFFFF) {
		M_WRITE((DB << 16) | Y.W,M_READ((operand.B.L << 16) | X.W));
		A.W--;
		X.W++;
		Y.W++;
		PC.W.PC--;
	} else {
		PC.W.PC += 2;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x55)
	O_dxx(opaddr);					/* EOR d,x */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x56)
	O_dxx(opaddr);					/* LSR d,x */
	C_LSR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x57)
	O_dixl(opaddr);					/* EOR [d],y */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x58)
	F_setI(0);					/* CLI i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x59)
	O_axy(opaddr);					/* EOR a,y */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x5A)
#ifdef SHORT_X						/* PHY s */
	S_PUSH(Y.B.L);
#else
	S_PUSH(Y.B.H);
	S_PUSH(Y.B.L);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x5B)
	D.W = A.W;					/* TCD i */
	C_SETF16(D.W);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x5C)
	O_al(opaddr);					/* JMP al */
	PC.A = opaddr.A;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x5D)
	O_axx(opaddr);					/* EOR a,x */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x5E)
	O_axx(opaddr);					/* LSR a,x */
	C_LSR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x5F)
	O_alxx(opaddr);					/* EOR al,x */
	C_EOR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x60)
	S_PULL(PC.B.L);					/* RTS s */
	S_PULL(PC.B.H);
	PC.W.PC++;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x61)
	O_dxi(opaddr);					/* ADC (d,x) */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x62)
	O_pcrl(opaddr);					/* PER s */
	S_PUSH(opaddr.B.H);
	S_PUSH(opaddr.B.L);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x63)
	O_sr(opaddr);					/* ADC d,s */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x64)
	O_d(opaddr);					/* STZ d */
	C_STZ(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x65)
	O_d(opaddr);					/* ADC d */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x66)
	O_d(opaddr);					/* ROR d */
	C_ROR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x67)
	O_dil(opaddr);					/* ADC [d] */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x68)
#ifdef SHORT_M						/* PLA s */
	S_PULL(A.B.L);
	C_SETF8(A.B.L);
#else
	S_PULL(A.B.L);
	S_PULL(A.B.H);
	C_SETF16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x69)
#ifdef SHORT_M						/* ADC # */
	O_i8(operand);
	C_ADC8(operand.B.L);
#else
	O_i16(operand);
	C_ADC16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x6A)
#ifdef SHORT_M						/* ROR A */
	C_ROR8(A.B.L);
#else
	C_ROR16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x6B)
	S_PULL(PC.B.L);					/* RTL s */
	S_PULL(PC.B.H);
	S_PULL(PC.B.PB);
	PC.W.PC++;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x6C)
	O_ai(opaddr);					/* JMP (a) */
	PC.W.PC = opaddr.W.L;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x6D)
	O_a(opaddr);					/* ADC a */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x6E)
	O_a(opaddr);					/* ROR a */
	C_ROR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x6F)
	O_al(opaddr);					/* ADC al */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x70)
	O_pcr(opaddr);					/* BVS r */
	if (F_getV) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x71)
	O_dix(opaddr);					/* ADC (d),y */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x72)
	O_di(opaddr);					/* ADC (d) */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x73)
	O_srix(opaddr);					/* ADC (d,s),y */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x74)
	O_dxx(opaddr);					/* STZ d,x */
	C_STZ(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x75)
	O_dxx(opaddr);					/* ADC d,x */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x76)
	O_dxx(opaddr);					/* ROR d,x */
	C_ROR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x77)
	O_dixl(opaddr);					/* ADC [d],y */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x78)
	F_setI(1);					/* SEI i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x79)
	O_axy(opaddr);					/* ADC a,y */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x7A)
#ifdef SHORT_X						/* PLY */
	S_PULL(Y.B.L);
	C_SETF8(Y.B.L);
#else
	S_PULL(Y.B.L);
	S_PULL(Y.B.H);
	C_SETF16(Y.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x7B)
	C_LDA16(D.W);					/* TDC i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x7C)
	O_axi(opaddr);					/* JMP (a,x) */
	PC.W.PC = opaddr.W.L;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x7D)
	O_axx(opaddr);					/* ADC a,x */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x7E)
	O_axx(opaddr);					/* ROR a,x */
	C_ROR(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x7F)
	O_alxx(opaddr);					/* ADC al,x */
	C_ADC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x80)
	O_pcr(opaddr);					/* BRA r */
#ifndef NATIVE_MODE
	if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
	PC.W.PC = opaddr.W.L;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x81)
	O_dxi(opaddr);					/* STA (d,x) */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x82)
	O_pcrl(opaddr);					/* BRL rl */
	PC.W.PC = opaddr.W.L;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x83)
	O_sr(opaddr);					/* STA d,s */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x84)
	O_d(opaddr);					/* STY d */
	C_STY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x85)
	O_d(opaddr);					/* STA d */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x86)
	O_d(opaddr);					/* STX d */
	C_STX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x87)
	O_dil(opaddr);					/* STA [d] */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x88)
#ifdef SHORT_X						/* DEY i */
	C_DEC8(Y.B.L);
#else
	C_DEC16(Y.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x89)
#ifdef SHORT_M						/* BIT # */
	O_i8(operand);
	F_setZ(!(A.B.L & operand.B.L));
#else
	O_i16(operand);
	F_setZ(!(A.W & operand.W));
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x8A)
#ifdef SHORT_M						/* TXA i */
	C_LDA8(X.B.L);
#else
	C_LDA16(X.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x8B)
	S_PUSH(DB);					/* PHB */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x8C)
	O_a(opaddr);					/* STY a */
	C_STY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x8D)
	O_a(opaddr);					/* STA a */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x8E)
	O_a(opaddr);					/* STX a */
	C_STX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x8F)
	O_al(opaddr);					/* STA al */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x90)
	O_pcr(opaddr);					/* BCC r */
	if (!F_getC) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x91)
	O_dix(opaddr);					/* STA (d),y */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x92)
	O_di(opaddr);					/* STA (d) */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x93)
	O_srix(opaddr);					/* STA (d,s),y */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x94)
	O_dxx(opaddr);					/* STY d,x */
	C_STY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x95)
	O_dxx(opaddr);					/* STA d,x */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x96)
	O_dxy(opaddr);					/* STX d,y */
	C_STX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x97)
	O_dixl(opaddr);					/* STA [d],y */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x98)
#ifdef SHORT_M						/* TYA i */
	C_LDA8(Y.B.L);
#else
	C_LDA16(Y.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x99)
	O_axy(opaddr);					/* STA a,y */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x9A)
#ifdef NATIVE_MODE					/* TXS i */
	S.W = X.W;
#else
	S.B.L = X.B.L;
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x9B)
#ifdef SHORT_X						/* TXY i */
	C_LDY8(X.B.L);
#else
	C_LDY16(X.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x9C)
	O_a(opaddr);					/* STZ a */
	C_STZ(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x9D)
	O_axx(opaddr);					/* STA a,x */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x9E)
	O_axx(opaddr);					/* STZ a,x */
	C_STZ(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0x9F)
	O_alxx(opaddr);					/* STA al,x */
	C_STA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA0)
#ifdef SHORT_X						/* LDY # */
	O_i8(operand);
	C_LDY8(operand.B.L);
#else
	O_i16(operand);
	C_LDY16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA1)
	O_dxi(opaddr);					/* LDA (d,x) */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA2)
#ifdef SHORT_X						/* LDX # */
	O_i8(operand);
	C_LDX8(operand.B.L);
#else
	O_i16(operand);
	C_LDX16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA3)
	O_sr(opaddr);					/* LDA d,s */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA4)
	O_d(opaddr);					/* LDY d */
	C_LDY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA5)
	O_d(opaddr);					/* LDA d */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA6)
	O_d(opaddr);					/* LDX d */
	C_LDX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA7)
	O_dil(opaddr);					/* LDA [d] */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA8)
#ifdef SHORT_X						/* TAY i */
	C_LDY8(A.B.L);
#else
	C_LDY16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xA9)
#ifdef SHORT_M						/* LDA # */
	O_i8(operand);
	C_LDA8(operand.B.L);
#else
	O_i16(operand);
	C_LDA16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xAA)
#ifdef SHORT_X						/* TAX i */
	C_LDX8(A.B.L);
#else
	C_LDX16(A.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xAB)
	S_PULL(DB);					/* PLB s */
	C_SETF8(DB);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xAC)
	O_a(opaddr);					/* LDY a */
	C_LDY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xAD)
	O_a(opaddr);					/* LDA a */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xAE)
	O_a(opaddr);					/* LDX a */
	C_LDX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xAF)
	O_al(opaddr);					/* LDA al */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB0)
	O_pcr(opaddr);					/* BCS r */
	if (F_getC) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB1)
	O_dix(opaddr);					/* LDA (d),y */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB2)
	O_di(opaddr);					/* LDA (d) */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB3)
	O_srix(opaddr);					/* LDA (d,s),y */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB4)
	O_dxx(opaddr);					/* LDY d,x */
	C_LDY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB5)
	O_dxx(opaddr);					/* LDA d,x */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB6)
	O_dxy(opaddr);					/* LDX d,y */
	C_LDX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB7)
	O_dixl(opaddr);					/* LDA [d],y */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB8)
	F_setV(0);					/* CLV i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xB9)
	O_axy(opaddr);					/* LDA a,y */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xBA)
#ifdef SHORT_X						/* TSX i */
	C_LDX8(S.B.L);
#else
	C_LDX16(S.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xBB)
#ifdef SHORT_X						/* TYX i */
	C_LDX8(Y.B.L);
#else
	C_LDX16(Y.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xBC)
	O_axx(opaddr);					/* LDY a,x */
	C_LDY(opaddr);
END_CPU_FUNC
BEGIN_CPU_FUNC(opcode_0xBD)
	O_axx(opaddr);					/* LDA a,x */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xBE)
	O_axy(opaddr);					/* LDX a,y */
	C_LDX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xBF)
	O_alxx(opaddr);					/* LDA al,x */
	C_LDA(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC0)
#ifdef SHORT_X						/* CPY # */
	O_i8(operand);
	C_CPY8(operand.B.L);
#else
	O_i16(operand);
	C_CPY16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC1)
	O_dxi(opaddr);					/* CMP (d,x) */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC2)
	O_i8(operand);					/* REP # */
#ifdef NATIVE_MODE
	P &= ~operand.B.L;
#else
    P &= ~(operand.B.L & ~0x30);
#endif
	CPU_modeSwitch();
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC3)
	O_sr(opaddr);					/* CMP d,s */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC4)
	O_d(opaddr);					/* CPY d */
	C_CPY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC5)
	O_d(opaddr);					/* CMP d */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC6)
	O_d(opaddr);					/* DEC d */
	C_DEC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC7)
	O_dil(opaddr);					/* CMP [d] */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC8)
#ifdef SHORT_X						/* INY */
	C_INC8(Y.B.L);
#else
	C_INC16(Y.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xC9)
#ifdef SHORT_M						/* CMP # */
	O_i8(operand);
	C_CMP8(operand.B.L);
#else
	O_i16(operand);
	C_CMP16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xCA)
#ifdef SHORT_X						/* DEX i */
	C_DEC8(X.B.L);
#else
	C_DEC16(X.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xCB)
	cpu_wait = 1;					/* WAI */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xCC)
	O_a(opaddr);					/* CPY a */
	C_CPY(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xCD)
	O_a(opaddr);					/* CMP a */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xCE)
	O_a(opaddr);					/* DEC a */
	C_DEC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xCF)
	O_al(opaddr);					/* CMP al */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD0)
	O_pcr(opaddr);					/* BNE r */
	if (!F_getZ) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD1)
	O_dix(opaddr);					/* CMP (d),y */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD2)
	O_di(opaddr);					/* CMP (d) */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD3)
	O_srix(opaddr);					/* CMP (d,s),y */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD4)
	O_d(opaddr);					/* PEI d */
	operand.B.L = M_READ(opaddr.A+1);
	S_PUSH(operand.B.L);
	operand.B.L = M_READ(opaddr.A);
	S_PUSH(operand.B.L);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD5)
	O_dxx(opaddr);					/* CMP d,x */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD6)
	O_dxx(opaddr);					/* DEC d,x */
	C_DEC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD7)
	O_dixl(opaddr);					/* CMP [d],y */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD8)
	F_setD(0);					/* CLD i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xD9)
	O_axy(opaddr);					/* CMP a,y */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xDA)
#ifdef SHORT_X						/* PHX */
	S_PUSH(X.B.L);
#else
	S_PUSH(X.B.H);
	S_PUSH(X.B.L);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xDB)
	cpu_stop = 1;					/* STP */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xDC)
	O_ail(opaddr);					/* JML (a) */
	PC.A = opaddr.A;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xDD)
	O_axx(opaddr);					/* CMP a,x */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xDE)
	O_axx(opaddr);					/* DEC a,x */
	C_DEC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xDF)
	O_alxx(opaddr);					/* CMP al,x */
	C_CMP(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE0)
#ifdef SHORT_X						/* CPX # */
	O_i8(operand);
	C_CPX8(operand.B.L);
#else
	O_i16(operand);
	C_CPX16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE1)
	O_dxi(opaddr);					/* SBC (d,x) */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE2)
	O_i8(operand);					/* SEP # */
#ifdef NATIVE_MODE
	P |= operand.B.L;
#else
    P |= (operand.B.L & ~0x30);
#endif
	CPU_modeSwitch();
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE3)
	O_sr(opaddr);					/* SBC d,s */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE4)
	O_d(opaddr);					/* CPX d */
	C_CPX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE5)
	O_d(opaddr);					/* SBC d */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE6)
	O_d(opaddr);					/* INC d */
	C_INC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE7)
	O_di(opaddr);					/* SBC [d] */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE8)
#ifdef SHORT_X						/* INX */
	C_INC8(X.B.L);
#else
	C_INC16(X.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xE9)
#ifdef SHORT_M						/* SBC # */
	O_i8(operand);
	C_SBC8(operand.B.L);
#else
	O_i16(operand);
	C_SBC16(operand.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xEA)
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xEB)
	operand.B.L = A.B.H;				/* XBA i */
	A.B.H = A.B.L;
	A.B.L = operand.B.L;
	C_SETF8(A.B.L);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xEC)
	O_a(opaddr);					/* CPX a */
	C_CPX(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xED)
	O_a(opaddr);					/* SBC a */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xEE)
	O_a(opaddr);					/* INC a */
	C_INC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xEF)
	O_al(opaddr);					/* SBC al */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF0)
	O_pcr(opaddr);					/* BEQ r */
	if (F_getZ) {
#ifndef NATIVE_MODE
		if (PC.B.H != opaddr.B.H) cpu_cycle_count++;
#endif
		PC.W.PC = opaddr.W.L;
		cpu_cycle_count++;
	}
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF1)
	O_dix(opaddr);					/* SBC (d),y */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF2)
	O_di(opaddr);					/* SBC (d) */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF3)
	O_srix(opaddr);					/* SBC (d,s),y */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF4)
	O_i16(operand);					/* PEA s */
	S_PUSH(operand.B.H);
	S_PUSH(operand.B.L);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF5)
	O_dxx(opaddr);					/* SBC d,x */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF6)
	O_dxx(opaddr);					/* INC d,x */
	C_INC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF7)
	O_dixl(opaddr);					/* SBC [d],y */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF8)
	F_setD(1);					/* SED i */
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xF9)
	O_axy(opaddr);					/* SBC a,y */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xFA)
#ifdef SHORT_X						/* PLX s */
	S_PULL(X.B.L);
	C_SETF8(X.B.L);
#else
	S_PULL(X.B.L);
	S_PULL(X.B.H);
	C_SETF16(X.W);
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xFB)
	E = F_getC;
#ifdef NATIVE_MODE					/* XCE i */
	F_setC(0);
#else
	F_setC(1);
#endif
	if (E) {
		P |= 0x30;
		S.B.H = 0x01;
		X.B.H = 0x00;
		Y.B.H = 0x00;
	}
	CPU_modeSwitch();
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xFC)
	O_axi(opaddr);					/* JSR (a,x) */
	PC.W.PC--;
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	PC.W.PC = opaddr.W.L;
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xFD)
	O_axx(opaddr);					/* SBC a,x */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xFE)
	O_axx(opaddr);					/* INC a,x */
	C_INC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(opcode_0xFF)
	O_alxx(opaddr);					/* SBC al,x */
	C_SBC(opaddr);
END_CPU_FUNC

BEGIN_CPU_FUNC(reset)
	cpu_reset = 0;
	cpu_abort = 0;
	cpu_nmi = 0;
	cpu_irq = 0;
	cpu_stop = 0;
	cpu_wait = 0;
	P = 0x34;
	E = 1;
	D.W = 0;
	DB = 0;
	PC.B.PB = 0;
	S.W = 0x01FF;
	A.W = 0;
	X.W = 0;
	Y.W = 0;
	PC.B.L = M_READ_VECTOR(0xFFFC);
	PC.B.H = M_READ_VECTOR(0xFFFD);
	CPU_modeSwitch();
END_CPU_FUNC

BEGIN_CPU_FUNC(abort)
	cpu_wait = 0;
#ifdef NATIVE_MODE
	S_PUSH(PC.B.PB);
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH(P);
	F_setD(0);
	F_setI(1);
	PC.B.PB = 0;
	PC.B.L = M_READ_VECTOR(0xFFE8);
	PC.B.H = M_READ_VECTOR(0xFFE9);
	cpu_cycle_count += 8;
#else
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH((byte) ((P & ~0x10) | 0x20));
	F_setD(0);
	F_setI(1);
	DB = 0;
	PC.B.PB = 0;
	PC.B.L = M_READ_VECTOR(0xFFF8);
	PC.B.H = M_READ_VECTOR(0xFFF9);
	cpu_cycle_count += 7;
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(nmi)
	cpu_nmi = 0;
	cpu_wait = 0;
#ifdef NATIVE_MODE
	S_PUSH(PC.B.PB);
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH(P);
	F_setD(0);
	F_setI(1);
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFEA);
	PC.B.H = M_READ_VECTOR(0xFFEB);
	cpu_cycle_count += 8;
#else
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH((byte) ((P & ~0x10) | 0x20));
	F_setD(0);
	F_setI(1);
	DB = 0;
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFFA);
	PC.B.H = M_READ_VECTOR(0xFFFB);
	cpu_cycle_count += 7;
#endif
END_CPU_FUNC

BEGIN_CPU_FUNC(irq)
	cpu_irq = 0;
	cpu_wait = 0;
#ifdef NATIVE_MODE
	S_PUSH(PC.B.PB);
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH(P);
	F_setD(0);
	F_setI(1);
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFEE);
	PC.B.H = M_READ_VECTOR(0xFFEF);
	cpu_cycle_count += 8;
#else
	S_PUSH(PC.B.H);
	S_PUSH(PC.B.L);
	S_PUSH((byte) ((P & ~0x10) | 0x20));
	F_setD(0);
	F_setI(1);
	F_setB(1);
	DB = 0;
	PC.B.PB = 0x00;
	PC.B.L = M_READ_VECTOR(0xFFFE);
	PC.B.H = M_READ_VECTOR(0xFFFF);
	cpu_cycle_count += 7;
#endif
END_CPU_FUNC
