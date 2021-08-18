/* =============================================================================
 *  libz80 - Z80 emulation library
 * =============================================================================
 *
 * (C) Gabriel Gambetta (gabriel.gambetta@gmail.com) 2000 - 2012
 *
 * Version 2.1.0
 *
 * -----------------------------------------------------------------------------
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

#ifndef _Z80_H_
#define _Z80_H_

#include "stdio.h"

typedef unsigned short ushort;
typedef unsigned char byte;


/** Function type to emulate data read. */
typedef byte (*Z80DataIn) 	(int param, ushort address);


/** Function type to emulate data write. */
typedef void (*Z80DataOut)	(int param, ushort address, byte data);


/** 
 * A Z80 register set.
 * An union is used since we want independent access to the high and low bytes of the 16-bit registers.
 */
typedef union 
{
	/** Word registers. */
	struct
	{
		ushort AF, BC, DE, HL, IX, IY, SP;
	} wr;
	
	/** Byte registers. Note that SP can't be accessed partially. */
	struct
	{
		byte F, A, C, B, E, D, L, H, IXl, IXh, IYl, IYh;
	} br;
} Z80Regs;


/** The Z80 flags */
typedef enum
{
	F_C  =   1,	/**< Carry */
	F_N  =   2, /**< Sub / Add */
	F_PV =   4, /**< Parity / Overflow */
	F_3  =   8, /**< Reserved */
	F_H  =  16, /**< Half carry */
	F_5  =  32, /**< Reserved */
	F_Z  =  64, /**< Zero */
	F_S  = 128  /**< Sign */
} Z80Flags;


/** A Z80 execution context. */
typedef struct
{
	Z80Regs	R1;		/**< Main register set (R) */
	Z80Regs R2;		/**< Alternate register set (R') */
	ushort	PC;		/**< Program counter */
	ushort	M1PC;		/** PC at beginning of instruction fetch */
	byte	R;		/**< Refresh */
	byte	I;
	byte	IFF1;	/**< Interrupt Flipflop 1 */
	byte	IFF2;	/**< Interrupt Flipflop 2 */
	byte	IM;		/**< Instruction mode */
	byte	M1;		/**< M1 line state (only for ifetch) */
	
	Z80DataIn	memRead;
	Z80DataOut	memWrite;
	int			memParam;
	
	Z80DataIn	ioRead;
	Z80DataOut	ioWrite;
	int			ioParam;
	
	byte		halted;
	unsigned	tstates;

	/* Below are implementation details which may change without
	 * warning; they should not be relied upon by any user of this
	 * library.
	 */

	/* If true, an NMI has been requested. */

	byte nmi_req;

	/* If true, the NMI line is low - used to manage edge triggering */
	byte nmi_low;

	/* If true, a maskable interrupt has been requested. */

	byte int_req;

	/* If true, defer checking maskable interrupts for one
	 * instruction.  This is used to keep an interrupt from happening
	 * immediately after an IE instruction. */

	byte defer_int;

	/* When a maskable interrupt has been requested, the interrupt
	 * vector.  For interrupt mode 1, it's the opcode to execute.  For
	 * interrupt mode 2, it's the LSB of the interrupt vector address.
	 * Not used for interrupt mode 0.
	 */

	byte int_vector;

	/* If true, then execute the opcode in int_vector. */

	byte exec_int_vector;

	void (*trace)(unsigned int memparam);

} Z80Context;


/** Execute the next instruction. */
void Z80Execute (Z80Context* ctx);

/** Execute enough instructions to use at least tstates cycles.
 * Returns the number of tstates actually executed.  Note: Resets
 * ctx->tstates.*/
unsigned Z80ExecuteTStates(Z80Context* ctx, unsigned tstates);

/** Decode the next instruction to be executed.
 * dump and decode can be NULL if such information is not needed
 *
 * @param dump A buffer which receives the hex dump
 * @param decode A buffer which receives the decoded instruction
 */
void Z80Debug (Z80Context* ctx, char* dump, char* decode);

/** Resets the processor. */
void Z80RESET (Z80Context* ctx);

/** Generates a hardware interrupt.
 * Some interrupt modes read a value from the data bus; this value must be provided in this function call, even
 * if the processor ignores that value in the current interrupt mode.
 *
 * @param value The value to read from the data bus
 */
void Z80INT (Z80Context* ctx, byte value);
/** Clears a pending hardware interrupt.
 * On some platforms the interrupt line may be triggered and then set
 * back before the CPU has interrupts enabled.
 */
void Z80NOINT(Z80Context* ctx);


/** Lower the NMI line (edge trigger is managed internally) */
void Z80NMI (Z80Context* ctx);

/** Raise the NMI line */
void Z80NMI_Clear (Z80Context* ctx);


#endif
