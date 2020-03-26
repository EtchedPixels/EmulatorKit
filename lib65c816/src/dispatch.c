/*
 * lib65816/dispatch.c Release 1p1
 * See LICENSE for more details.
 *
 * Code originally from XGS: Apple IIGS Emulator (dispatch.c)
 *
 * Originally written and Copyright (C)1996 by Joshua M. Thompson
 * Copyright (C) 2006 by Samuel A. Falvo II
 *
 * Modified for greater portability and virtual hardware independence.
 */

#define CPU_DISPATCH

#include <lib65816/cpu.h>
#include "cpumicro.h"
#include <stdio.h>

dualw   A;  /* Accumulator               */
dualw   D;  /* Direct Page Register      */
byte    P;  /* Processor Status Register */
int     E;  /* Emulation Mode Flag       */
dualw   S;  /* Stack Pointer             */
dualw   X;  /* X Index Register          */
dualw   Y;  /* Y Index Register          */
byte    DB; /* Data Bank Register        */

union {
#ifdef WORDS_BIGENDIAN
    struct { byte Z,PB,H,L; } B;
    struct { word16 Z,PC; } W;
#else
    struct { byte L,H,PB,Z; } B;
    struct { word16 PC,Z; } W;
#endif
    word32  A;
} PC;

duala       atmp,opaddr;
dualw       wtmp,otmp,operand;
int         a1,a2,a3,a4,o1,o2,o3,o4;

#ifdef OLDCYCLES
byte        *cpu_curr_cycle_table;
#endif
void        (**cpu_curr_opcode_table)();

extern int  cpu_reset,cpu_abort,cpu_nmi,cpu_irq,cpu_stop,cpu_wait,cpu_trace;
extern int  cpu_update_period;

extern void (*cpu_opcode_table[1300])();

#ifdef OLDCYCLES
/* Base cycle counts for all possible 1300 opcodes (260 opcodes x 5 modes).     */
/* The opcode handlers may add additional cycles to handle special cases such   */
/* a non-page-aligned direct page register or taking a branch.          */

byte    cpu_cycle_table[1300] =
{
    8, 6, 8, 4, 5, 3, 5, 6, 3, 2, 2, 4, 6, 4, 6, 5, /* e=0, m=1, x=1 */
    2, 5, 5, 7, 5, 4, 6, 6, 2, 4, 2, 2, 6, 4, 7, 5,
    6, 6, 8, 4, 3, 3, 5, 6, 4, 2, 2, 5, 4, 4, 6, 5,
    2, 5, 5, 7, 4, 4, 6, 6, 2, 4, 2, 2, 4, 4, 7, 5,
    7, 6, 2, 4, 7, 3, 5, 6, 3, 2, 2, 3, 3, 4, 6, 5,
    2, 5, 5, 7, 7, 4, 6, 6, 2, 4, 3, 2, 4, 4, 7, 5,
    6, 6, 6, 4, 3, 3, 5, 6, 4, 2, 2, 6, 5, 4, 6, 5,
    2, 5, 5, 7, 4, 4, 6, 6, 2, 4, 4, 2, 6, 4, 7, 5,
    2, 6, 3, 4, 3, 3, 3, 6, 2, 2, 2, 3, 4, 4, 4, 5,
    2, 6, 5, 7, 4, 4, 4, 6, 2, 5, 2, 2, 4, 5, 5, 5,
    2, 6, 2, 4, 3, 3, 3, 6, 2, 2, 2, 4, 4, 4, 4, 5,
    2, 5, 5, 7, 4, 4, 4, 6, 2, 4, 2, 2, 4, 4, 4, 5,
    2, 6, 3, 4, 3, 3, 5, 6, 2, 2, 2, 3, 4, 4, 4, 5,
    2, 5, 5, 7, 6, 4, 6, 6, 2, 4, 3, 3, 6, 4, 7, 5,
    2, 6, 3, 4, 3, 3, 5, 6, 2, 2, 2, 3, 4, 4, 6, 5,
    2, 5, 5, 7, 5, 4, 6, 6, 2, 4, 4, 2, 6, 4, 7, 5,
    0, 0, 0, 0,

    8, 6, 8, 4, 5, 3, 5, 6, 3, 2, 2, 4, 6, 4, 6, 5, /* e=0, m=1, x=0 */
    2, 6, 5, 7, 5, 4, 6, 6, 2, 5, 2, 2, 6, 5, 7, 5,
    6, 6, 8, 4, 3, 3, 5, 6, 4, 2, 2, 5, 4, 4, 6, 5,
    2, 6, 5, 7, 4, 4, 6, 6, 2, 5, 2, 2, 5, 5, 7, 5,
    7, 6, 2, 4, 0, 3, 5, 6, 4, 2, 2, 3, 3, 4, 6, 5,
    2, 6, 5, 7, 0, 4, 6, 6, 2, 5, 4, 2, 4, 5, 7, 5,
    6, 6, 6, 4, 3, 3, 5, 6, 5, 2, 2, 6, 5, 4, 6, 5,
    2, 6, 5, 7, 4, 4, 6, 6, 2, 5, 5, 2, 6, 5, 7, 5,
    2, 6, 3, 4, 4, 3, 4, 6, 2, 2, 2, 3, 5, 4, 5, 5,
    2, 6, 5, 7, 5, 4, 5, 6, 2, 5, 2, 2, 4, 5, 5, 5,
    3, 6, 3, 4, 4, 3, 4, 6, 2, 2, 2, 4, 5, 4, 5, 5,
    2, 6, 5, 7, 5, 4, 5, 6, 2, 5, 2, 2, 5, 5, 5, 5,
    3, 6, 3, 4, 4, 3, 6, 6, 2, 2, 2, 3, 5, 4, 6, 5,
    2, 6, 5, 7, 6, 4, 8, 6, 2, 5, 4, 3, 6, 5, 7, 5,
    3, 6, 3, 4, 4, 3, 6, 6, 2, 2, 2, 3, 5, 4, 6, 5,
    2, 6, 5, 7, 5, 4, 8, 6, 2, 5, 5, 2, 6, 5, 7, 5,
    0, 0, 0, 0,

    8, 7, 8, 5, 7, 4, 7, 7, 3, 3, 2, 4, 8, 5, 8, 6, /* e=0, m=0, x=1 */
    2, 6, 6, 8, 7, 5, 8, 7, 2, 5, 2, 2, 8, 5, 9, 6,
    6, 7, 8, 5, 4, 4, 7, 7, 4, 3, 2, 5, 5, 5, 8, 6,
    2, 6, 6, 8, 5, 5, 8, 7, 2, 5, 2, 2, 5, 5, 9, 6,
    7, 7, 2, 5, 0, 4, 7, 7, 4, 3, 2, 3, 3, 5, 8, 6,
    2, 6, 6, 8, 0, 5, 8, 7, 2, 5, 3, 2, 4, 5, 9, 6,
    6, 7, 6, 5, 4, 4, 7, 7, 5, 3, 2, 6, 5, 5, 8, 6,
    2, 6, 6, 8, 5, 5, 8, 7, 2, 5, 4, 2, 6, 5, 9, 6,
    2, 7, 3, 5, 3, 4, 3, 7, 2, 3, 2, 3, 4, 5, 4, 6,
    2, 6, 6, 8, 4, 5, 4, 7, 2, 5, 2, 2, 5, 5, 5, 6,
    2, 7, 2, 5, 3, 4, 3, 7, 2, 3, 2, 4, 4, 5, 4, 6,
    2, 6, 6, 8, 4, 5, 4, 7, 2, 5, 2, 2, 4, 5, 4, 6,
    2, 7, 3, 5, 3, 4, 7, 7, 2, 3, 2, 3, 4, 5, 8, 6,
    2, 6, 6, 8, 6, 5, 8, 7, 2, 5, 3, 3, 6, 5, 9, 6,
    2, 7, 3, 5, 3, 4, 7, 7, 2, 3, 2, 3, 4, 5, 8, 6,
    2, 6, 6, 8, 5, 5, 8, 7, 2, 5, 4, 2, 6, 5, 9, 6,
    0, 0, 0, 0,

    8, 7, 8, 5, 7, 4, 7, 7, 3, 3, 2, 4, 8, 5, 8, 6, /* e=0, m=0, x=0 */
    2, 7, 6, 8, 7, 5, 8, 7, 2, 6, 2, 2, 8, 6, 9, 6,
    6, 7, 8, 5, 4, 4, 7, 7, 4, 3, 2, 5, 5, 5, 8, 6,
    2, 7, 6, 8, 5, 5, 8, 7, 2, 6, 2, 2, 6, 6, 9, 6,
    7, 7, 2, 5, 0, 4, 7, 7, 3, 3, 2, 3, 3, 5, 8, 6,
    2, 7, 6, 8, 0, 5, 8, 7, 2, 6, 4, 2, 4, 6, 9, 6,
    6, 7, 6, 5, 4, 4, 7, 7, 4, 3, 2, 6, 5, 5, 8, 6,
    2, 7, 6, 8, 5, 5, 8, 7, 2, 6, 5, 2, 6, 6, 9, 6,
    2, 7, 3, 5, 4, 4, 4, 7, 2, 3, 2, 3, 5, 5, 5, 6,
    2, 7, 6, 8, 5, 5, 5, 7, 2, 6, 2, 2, 5, 6, 6, 6,
    3, 7, 3, 5, 4, 4, 4, 7, 2, 3, 2, 4, 5, 5, 5, 6,
    2, 7, 6, 8, 5, 5, 5, 7, 2, 6, 2, 2, 5, 6, 5, 6,
    3, 7, 3, 5, 4, 4, 7, 7, 2, 3, 2, 3, 5, 5, 8, 6,
    2, 7, 6, 8, 6, 5, 8, 7, 2, 6, 4, 3, 6, 6, 9, 6,
    3, 7, 3, 5, 4, 4, 7, 7, 2, 3, 2, 3, 5, 5, 8, 6,
    2, 7, 6, 8, 5, 5, 8, 7, 2, 6, 5, 2, 6, 6, 9, 6,
    0, 0, 0, 0,

    8, 6, 8, 4, 5, 3, 5, 6, 3, 2, 2, 4, 6, 4, 6, 5, /* e=1, m=1, x=1 */
    2, 5, 5, 7, 5, 4, 6, 6, 2, 4, 2, 2, 6, 4, 7, 5,
    6, 6, 8, 4, 3, 3, 5, 6, 4, 2, 2, 5, 4, 4, 6, 5,
    2, 5, 5, 7, 4, 4, 6, 6, 2, 4, 2, 2, 4, 4, 7, 5,
    7, 6, 2, 4, 0, 3, 5, 6, 3, 2, 2, 3, 3, 4, 6, 5,
    2, 5, 5, 7, 0, 4, 6, 6, 2, 4, 3, 2, 4, 4, 7, 5,
    6, 6, 6, 4, 3, 3, 5, 6, 4, 2, 2, 6, 5, 4, 6, 5,
    2, 5, 5, 7, 4, 4, 6, 6, 2, 4, 4, 2, 6, 4, 7, 5,
    2, 6, 3, 4, 3, 3, 3, 6, 2, 2, 2, 3, 4, 4, 4, 5,
    2, 5, 5, 7, 4, 4, 4, 6, 2, 4, 2, 2, 4, 4, 4, 5,
    2, 6, 2, 4, 3, 3, 3, 6, 2, 2, 2, 4, 4, 4, 4, 5,
    2, 5, 5, 7, 4, 4, 4, 6, 2, 4, 2, 2, 4, 4, 4, 5,
    2, 6, 3, 4, 3, 3, 5, 6, 2, 2, 2, 3, 4, 4, 6, 5,
    2, 5, 5, 7, 6, 4, 6, 6, 2, 4, 3, 3, 6, 4, 7, 5,
    2, 6, 3, 4, 3, 3, 5, 6, 2, 2, 2, 3, 4, 4, 6, 5,
    2, 5, 5, 7, 5, 4, 6, 6, 2, 4, 4, 2, 6, 4, 7, 5,
    0, 0, 0, 0
};
#endif

void CPU_run(void)
{
    word32  last_update,next_update;
    int opcode;

    cpu_cycle_count = 0;
    last_update = 0;
    next_update = cpu_update_period;
    E = 1;
    F_setM(1);
    F_setX(1);
    CPU_modeSwitch();

dispatch:
    if (cpu_cycle_count >= next_update) goto update;
update_resume:
#ifdef DEBUG
    if (cpu_trace) goto debug;
debug_resume:
#endif
    if (cpu_reset) goto reset;
    if (cpu_stop) goto dispatch;
    if (cpu_abort) goto abort;
    if (cpu_nmi) goto nmi;
    if (cpu_irq) goto irq;
irq_return:
    if (cpu_wait) { cpu_cycle_count++; goto dispatch; }
    opcode = M_READ_OPCODE(PC.A);
    PC.W.PC++;

#ifdef OLDCYCLES
    cpu_cycle_count += cpu_curr_cycle_table[opcode];
#endif
    (**cpu_curr_opcode_table[opcode])();

    goto dispatch;

/* Special cases. Since these don't happen a lot more often than they   */
/* do happen, accessing them this way means most of the time the    */
/* generated code is _not_ branching. Only during the special cases do  */
/* we take the branch penalty (if there is one).            */

update:
    E_UPDATE(cpu_cycle_count);
    last_update = cpu_cycle_count;
    next_update = last_update + cpu_update_period;
    goto update_resume;

#ifdef DEBUG
debug:
    CPU_debug();
    goto debug_resume;
#endif
reset:
    (**cpu_curr_opcode_table[256])();
    goto dispatch;
abort:
    (**cpu_curr_opcode_table[257])();
    goto dispatch;
nmi:
    (**cpu_curr_opcode_table[258])();
    goto dispatch;
irq:
    if (P & 0x04) goto irq_return;
    (**cpu_curr_opcode_table[259])();
    goto dispatch;

}

/* Recalculate opcode_offset based on the new processor mode */

void CPU_modeSwitch(void) {

    int opcode_offset;

    if (E) {
        opcode_offset = 1040;
    } else {
        if (F_getX) {
            X.B.H = 0;
            Y.B.H = 0;
        }
        opcode_offset = ((~P >> 4) & 0x03) * 260;
    }
#ifdef OLDCYCLES
    cpu_curr_cycle_table = cpu_cycle_table + opcode_offset;
#endif
    cpu_curr_opcode_table = cpu_opcode_table + opcode_offset;
}
