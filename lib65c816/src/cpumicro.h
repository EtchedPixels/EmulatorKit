/*
 * lib65816/cpumicro.h Release 1p1
 * See LICENSE for more details.
 *
 * Code originally from XGS: Apple IIGS Emulator (cpumicro.h)
 *
 * Originally written and Copyright (C)1996 by Joshua M. Thompson
 * Copyright (C) 2006 by Samuel A. Falvo II
 *
 * Modified for greater portability and virtual hardware independence.
 */

/* This file defines "micro operation" macros; that is, very low-level	*/
/* operations that are independent of the state of the E, M, or X bits.	*/

/* Macros for setting/clearing program status register bits */

#define F_setN(v)	if (v) P |= 0x80; else P &= ~0x80
#define F_setV(v)	if (v) P |= 0x40; else P &= ~0x40
#define F_setM(v)	if (v) P |= 0x20; else P &= ~0x20
#define F_setX(v)	if (v) P |= 0x10; else P &= ~0x10
#define F_setB(v)	if (v) P |= 0x10; else P &= ~0x10
#define F_setD(v)	if (v) P |= 0x08; else P &= ~0x08
#define F_setI(v)	if (v) P |= 0x04; else P &= ~0x04
#define F_setZ(v)	if (v) P |= 0x02; else P &= ~0x02
#define F_setC(v)	if (v) P |= 0x01; else P &= ~0x01

/* Macros for testing program status register bits */

#define F_getN	((P & 0x80)? 1:0)
#define F_getV	((P & 0x40)? 1:0)
#define F_getM	((P & 0x20)? 1:0)
#define F_getX	((P & 0x10)? 1:0)
#define F_getD	((P & 0x08)? 1:0)
#define F_getI	((P & 0x04)? 1:0)
#define F_getZ	((P & 0x02)? 1:0)
#define F_getC	((P & 0x01)? 1:0)

/*------- Routines that operate on an 8/16-bit value  -------*/

#define C_SETF8(v)	F_setN(v & 0x80);	\
			F_setZ(!v)

#define C_SETF16(v)	F_setN(v & 0x8000);	\
			F_setZ(!v)

#define C_LDA8(v)	A.B.L = v;	\
			C_SETF8(v)

#define C_LDA16(v)	A.W = v;	\
			C_SETF16(v)

#define C_LDX8(v)	X.B.L = v;	\
			C_SETF8(v)

#define C_LDX16(v)	X.W = v;	\
			C_SETF16(v)

#define C_LDY8(v)	Y.B.L = v;	\
			C_SETF8(v)

#define C_LDY16(v)	Y.W = v;	\
			C_SETF16(v)

#define C_INC8(v)	v++;		\
			C_SETF8(v)

#define C_INC16(v)	v++;		\
			C_SETF16(v)

#define C_DEC8(v)	v--;		\
			C_SETF8(v)

#define C_DEC16(v)	v--;		\
			C_SETF16(v)

#define C_ASL8(v)	F_setC(v & 0x80);	\
			v = v << 1;		\
			C_SETF8(v)

#define C_ASL16(v)	F_setC(v & 0x8000);	\
			v = v << 1;		\
			C_SETF16(v)

#define C_LSR8(v)	F_setC(v & 0x01);	\
			v = v >> 1;		\
			C_SETF8(v)

#define C_LSR16(v)	F_setC(v & 0x0001);	\
			v = v >> 1;		\
			C_SETF16(v)

#define C_ROL8(v)	wtmp.B.L = P & 0x01;		\
			F_setC(v & 0x80);		\
			v = (v << 1) | wtmp.B.L;	\
			C_SETF8(v)

#define C_ROL16(v)	wtmp.W = P & 0x01;	\
			F_setC(v & 0x8000);	\
			v = (v << 1) | wtmp.W;	\
			C_SETF16(v)

#define C_ROR8(v)	wtmp.B.L = (P & 0x01) << 7;	\
			F_setC(v & 0x01);		\
			v = (v >> 1) | wtmp.B.L;	\
			C_SETF8(v)

#define C_ROR16(v)	wtmp.W = (P & 0x01) << 15;	\
			F_setC(v & 0x0001);		\
			v = (v >> 1) | wtmp.W; 		\
			C_SETF16(v)

#define C_TSB8(v)	F_setZ(!(v & A.B.L));	\
			v |= A.B.L;
			
#define C_TSB16(v)	F_setZ(!(v & A.W));	\
			v |= A.W;
			
#define C_TRB8(v)	F_setZ(!(v & A.B.L));	\
			v &= ~A.B.L;
			
#define C_TRB16(v)	F_setZ(!(v & A.W));	\
			v &= ~A.W;
			
#define C_AND8(v)	A.B.L &= v;	\
			C_SETF8(A.B.L)

#define C_AND16(v)	A.W &= v;	\
			C_SETF16(A.W)

#define C_ORA8(v)	A.B.L |= v;	\
			C_SETF8(A.B.L)

#define C_ORA16(v)	A.W |= v;	\
			C_SETF16(A.W)

#define C_EOR8(v)	A.B.L ^= v;	\
			C_SETF8(A.B.L)

#define C_EOR16(v)	A.W ^= v;	\
			C_SETF16(A.W)

#define C_BIT8(v)	F_setN(v & 0x80);	\
			F_setV(v & 0x40);	\
			F_setZ(!(v & A.B.L));	\

#define C_BIT16(v)	F_setN(v & 0x8000);	\
			F_setV(v & 0x4000);	\
			F_setZ(!(v & A.W));	\

#define C_ADC8(v)	if (F_getD) {								\
				a1 = A.B.L & 0x0F;						\
				a2 = (A.B.L >> 4) & 0x0F;					\
				o1 = v & 0x0F;							\
				o2 = (v >> 4) & 0x0F;						\
				a1 += (o1 + F_getC);						\
				a2 += o2;							\
				if (a1>9) {							\
					a1 -= 10;						\
					a2++;							\
				}								\
				if (a2>9) {							\
					a2 -= 10;						\
					F_setC(1);						\
				} else {							\
					F_setC(0);						\
				}								\
				wtmp.W = (a2 << 4) | a1;					\
			} else {								\
				wtmp.W = A.B.L + v + F_getC;					\
				F_setC(wtmp.B.H);						\
			}									\
			F_setV(~(A.B.L ^ v) & (A.B.L ^ wtmp.B.L) & 0x80);			\
			A.B.L = wtmp.B.L;							\
			C_SETF8(A.B.L);

#define C_ADC16(v)	if (F_getD) {								\
				a1 = A.W & 0x0F;						\
				a2 = (A.W >> 4) & 0x0F;						\
				a3 = (A.W >> 8) & 0x0F;						\
				a4 = (A.W >> 12) & 0x0F;					\
				o1 = v & 0x0F;							\
				o2 = (v >> 4) & 0x0F;						\
				o3 = (v >> 8) & 0x0F;						\
				o4 = (v >> 12) & 0x0F;						\
				a1 += (o1 + F_getC);						\
				a2 += o2;							\
				a3 += o3;							\
				a4 += o4;							\
				if (a1>9) {							\
					a1 -= 10;						\
					a2++;							\
				}								\
				if (a2>9) {							\
					a2 -= 10;						\
					a3++;							\
				}								\
				if (a3>9) {							\
					a3 -= 10;						\
					a4++;							\
				}								\
				if (a4>9) {							\
					a4 -= 10;						\
					F_setC(1);						\
				} else {							\
					F_setC(0);						\
				}								\
				atmp.A = (a4 << 12) | (a3 << 8) | (a2 << 4) | a1;		\
			} else {								\
				atmp.A = A.W + v + F_getC;					\
				F_setC(atmp.W.H);						\
			}									\
			F_setV(~(A.W ^ v) & (A.W ^ atmp.W.L) & 0x8000);				\
			A.W = atmp.W.L;								\
			C_SETF16(A.W);

#define C_SBC8(v)	if (F_getD) {								\
				a1 = A.B.L & 0x0F;						\
				a2 = (A.B.L >> 4) & 0x0F;					\
				o1 = v & 0x0F;							\
				o2 = (v >> 4) & 0x0F;						\
				a1 -= (o1 + !F_getC);						\
				a2 -= o2;							\
				if (a1<0) {							\
					a1 += 10;						\
					a2--;							\
				}								\
				if (a2<0) {							\
					a2 += 10;						\
					F_setC(0);						\
				} else {							\
					F_setC(1);						\
				}								\
				wtmp.W = (a2 << 4) | a1;					\
			} else {								\
				wtmp.W = (A.B.L - v) - !F_getC;					\
				F_setC(!wtmp.B.H);						\
			}									\
			F_setV((A.B.L ^ v) & (A.B.L ^ wtmp.B.L) & 0x80);			\
			A.B.L = wtmp.B.L;							\
			C_SETF8(A.B.L);

#define C_SBC16(v)	if (F_getD) {								\
				a1 = A.W & 0x0F;						\
				a2 = (A.W >> 4) & 0x0F;						\
				a3 = (A.W >> 8) & 0x0F;						\
				a4 = (A.W >> 12) & 0x0F;					\
				o1 = v & 0x0F;							\
				o2 = (v >> 4) & 0x0F;						\
				o3 = (v >> 8) & 0x0F;						\
				o4 = (v >> 12) & 0x0F;						\
				a1 -= (o1 + !F_getC);						\
				a2 -= o2;							\
				a3 -= o3;							\
				a4 -= o4;							\
				if (a1<0) {							\
					a1 += 10;						\
					a2--;							\
				}								\
				if (a2<0) {							\
					a2 += 10;						\
					a3--;							\
				}								\
				if (a3<0) {							\
					a3 += 10;						\
					a4--;							\
				}								\
				if (a4<0) {							\
					a4 += 10;						\
					F_setC(0);						\
				} else {							\
					F_setC(1);						\
				}								\
				atmp.A = (a4 << 12) | (a3 << 8) | (a2 << 4) | a1;		\
			} else {								\
				atmp.A = (A.W - v) - !F_getC;					\
				F_setC(!atmp.W.H);						\
			}									\
			F_setV((A.W ^ v) & (A.W ^ atmp.W.L) & 0x8000);				\
			A.W = atmp.W.L;								\
			C_SETF16(A.W);

#define C_CMP8(v)	wtmp.W = A.B.L - v;	\
			F_setC(!wtmp.B.H);	\
			C_SETF8(wtmp.B.L)

#define C_CMP16(v)	atmp.A = A.W - v;	\
			F_setC(!atmp.W.H);	\
			C_SETF16(atmp.W.L)

#define C_CPX8(v)	wtmp.W = X.B.L - v;	\
			F_setC(!wtmp.B.H);	\
			C_SETF8(wtmp.B.L)

#define C_CPX16(v)	atmp.A = X.W - v;	\
			F_setC(!atmp.W.H);	\
			C_SETF16(atmp.W.L)

#define C_CPY8(v)	wtmp.W = Y.B.L - v;	\
			F_setC(!wtmp.B.H);	\
			C_SETF8(wtmp.B.L)

#define C_CPY16(v)	atmp.A = Y.W - v;	\
			F_setC(!atmp.W.H);	\
			C_SETF16(atmp.W.L)
