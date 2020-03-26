#ifndef LIB65816_CPU_H
#define LIB65816_CPU_H

#include <stdint.h>

/*
 * lib65816/cpu.h Release 1p1
 * See LICENSE for more details.
 *
 * Code originally from XGS: Apple IIGS Emulator (gscpu.h)
 *
 * Originally written and Copyright (C)1996 by Joshua M. Thompson
 * Copyright (C) 2006 by Samuel A. Falvo II
 *
 * Modified for greater portability and virtual hardware independence.
 */

#include <lib65816/config.h>


/* Type definitions for convenience.  These are not intended to be used
 * outside the scope of lib65816, since it might conflict with other
 * libraries type definitions.
 */

typedef unsigned char       byte;
typedef signed char         sbyte;

#if SIZEOF_LONG == 4
typedef unsigned long       word32;
typedef signed long         sword32;
#elif SIZEOF_INT == 4
typedef unsigned int        word32;
typedef signed int          sword32;
#endif

#if SIZEOF_SHORT == 2
typedef unsigned short      word16;
typedef signed short        sword16;
#elif SIZEOF_INT == 2
typedef unsigned int        word16;
typedef signed int          sword16;
#endif

typedef signed char         offset_s;   /* short offset */
typedef signed short        offset_l;   /* long offset  */



/*
 * Union definition of a 16-bit value that can also be 
 * accessed as its component 8-bit values. Useful for
 * registers, which change sized based on teh settings
 * the M and X program status register bits.
 */

typedef union {
#ifdef WORDS_BIGENDIAN
    struct { byte   H,L; } B;
#else
    struct { byte   L,H; } B;
#endif
    word16  W;
} dualw;

/* Same as above but for addresses. */

typedef union {
#ifdef WORDS_BIGENDIAN
    struct { byte   Z,B,H,L; } B;
    struct { word16 H,L; } W;
#else
    struct { byte   L,H,B,Z; } B;
    struct { word16 L,H; } W;
#endif
    word32  A;
} duala;



/* Definitions of the 65816 registers, in case you want
 * to access these from your own routines (such as from
 * a WDM opcode handler routine.
 *
 * ATTENTION: DO NOT DEPEND ON THESE -- THESE WILL EVENTUALLY BE REPLACED
 * WITH A STRUCTURE THAT MAINTAINS ALL CPU STATE.
 */

extern dualw    A;  /* Accumulator           */
extern dualw    D;  /* Direct Page Register      */
extern byte     P;  /* Processor Status Register */
extern int      E;  /* Emulation Mode Flag       */
extern dualw    S;  /* Stack Pointer             */
extern dualw    X;  /* X Index Register          */
extern dualw    Y;  /* Y Index Register          */
extern byte     DB; /* Data Bank Register        */

#ifndef CPU_DISPATCH

extern union {      /* Program Counter       */
#ifdef WORDS_BIGENDIAN
    struct { byte Z,PB,H,L; } B;
    struct { word16 Z,PC; } W;
#else
    struct { byte L,H,PB,Z; } B;
    struct { word16 PC,Z; } W;
#endif
    word32  A;
} PC;

#endif



/* Current cycle count */

#if defined ( __sparc__ ) && defined ( __GNUC__ )
register word32 cpu_cycle_count asm ("g5");
#else
extern word32   cpu_cycle_count;
#endif



/* These are the core memory access macros used in the 65816 emulator.
 * Set these to point to routines which handle your emulated machine's
 * memory access (generally these routines will check for access to
 * memory-mapped I/O and things of that nature.)
 *
 * The SYNC pin is useful to trap OS calls, whereas the VP pin is
 * needed to emulate hardware which modifies the vector addresses.
 */

#define EMUL_PIN_SYNC 1 // much more work to provide VPD and VPA
#define EMUL_PIN_VP   2
#define M_READ(a)         read65c816(a, 0)
#define DB_READ(a)        read65c816(a, 1)
#define M_READ_OPCODE(a)  read65c816(a, 0)
#define M_READ_VECTOR(a)  read65c816(a, 0)
#define M_WRITE(a,v)      write65c816((a),(v))


/* Set this macro to your emulator's "update" routine. Your update
 * routine would probably do things like update hardware sprites,
 * and check for user keypresses. CPU_run() calls this routine
 * periodically to make sure the rest of your emulator gets time
 * to run.
 *
 * v is the number of CPU clock cycles that have elapsed since the last
 * call.
 */

#define E_UPDATE(v)     system_process()



/* Set this macro to your emulator's routine for handling the WDM
 * pseudo-opcode. Useful for trapping certain emulated machine
 * functions and emulating them in fast C code.
 *
 * v is the operand byte immediately following the WDM opcode.
 */

#define E_WDM(v)        wdm();



/* Set the 65816 emulator's update period (the number of cycles
 * elapsed between calls to the E_UPDATE routine.)
 */

void CPU_setUpdatePeriod(word32 period);

/* Set the 65816 emulator's trace mode to off (0) or on (nonzero) */

void CPU_setTrace(int mode);

/* Send a reset to the 65816 emulator. You should do this at    */
/* startup before you call CPU_run().               */

void CPU_reset(void);

/* Send an abort to the 65816 emulator. */

void CPU_abort(void);

/* Send a non-maskable interrupt to the 65816 emulator */

void CPU_nmi(void);

/* Send a standard (maskable) interrupt to the 65816 emulator.  Up to 32
 * IRQ sources are supported. */

void CPU_addIRQ( word32 mask );

/* Clear a previously sent interrupt (if one is still pending) */

void CPU_clearIRQ( word32 mask );

/* This routine never returns; it sits in a loop processing opcodes */
/* until the emulator exits. Periodic calls to the emulator update  */
/* macro are made to allow the rest of the emulator to function.    */

void CPU_run(void);

/* Internal routine called when the mode bits (e/m/x) change */

void CPU_modeSwitch(void);

/* Internal routine to print the next instruction in trace mode */

void CPU_debug(void);


/* Platform routines */

extern uint8_t read65c816(uint32_t addr, uint8_t mode);
extern void write65c816(uint32_t addr, uint8_t value);
extern void system_process(void);
extern void wdm(void);

#endif /* _CPU_H */

