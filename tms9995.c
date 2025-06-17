/* license:BSD-3-Clause
   copyright-holders:Michael Zapf */

/*
 *	De-mamed and turned back into C for the RC2014 emulator set. Any bugs
 *	in this version should be assumed to be mine, Alan Cox 2022.
 */

/*
    Texas Instruments TMS9995

                    +----------------+
              XTAL1 | 1     \/     40| A15,CRUOUT
        XTAL2,CLKIN | 2            39| A14
             CLKOUT | 3            38| A13
                 D7 | 4            37| A12
                 D6 | 5            36| A11
                 D5 | 6            35| A10
                 D4 | 7            34| A9
                 D3 | 8            33| A8
                 D2 | 9            32| A7
                Vcc |10            31| Vss
                 D1 |11            30| A6
                 D0 |12            29| A5
              CRUIN |13            28| A4
          /INT4,/EC |14            27| A3
              /INT1 |15            26| A2
          IAQ,HOLDA |16            25| A1
              /DBIN |17            24| A0
              /HOLD |18            23| READY
        /WE,/CRUCLK |19            22| /RESET
             /MEMEN |20            21| /NMI
                    +----------------+

      XTAL1    in   Crystal input pin for internal oscillator
      XTAL2    in   Crystal input pin for internal oscillator, or
      CLKIN    in   Input pin for external oscillator
     CLKOUT   out   Clock output signal (1:4 of the input signal frequency)
      CRUIN    in   CRU input data
      /INT4    in   Interrupt level 4 input
        /EC    in   Event counter
      /INT1    in   Interrupt level 1 input
        IAQ   out   Instruction acquisition
      HOLDA   out   Hold acknowledge
        /WE   out   Data available for memory write
    /CRUCLK   out   Communication register unit clock output
     /MEMEN   out   Address bus contains memory address
       /NMI    in   Non-maskable interrupt (/LOAD on TMS9900)
     /RESET    in   Reset interrupt
      READY    in   Memory/External CRU device ready for access
     CRUOUT   out   Communication register unit data output

        Vcc   +5V   supply
        Vss    0V   Ground reference

     A0-A15   out   Address bus
      D0-D7 in/out  Data bus

     Note that Texas Instruments' bit numberings define bit 0 as the
     most significant bit (different to most other systems). Also, the
     system uses big-endian memory organisation: Storing the word 0x1234 at
     address 0x0000 means that the byte 0x12 is stored at 0x0000 and byte 0x34
     is stored at 0x0001.

     The TMS9995 is a 16 bit microprocessor like the TMS9900, operating on
     16-bit words and using 16-bit opcodes. Memory transfer of 16-bit words
     is achieved by a transfer of the most significant byte, followed by
     the least significant byte.

     The 8-bit databus width allows the processor to exchange single bytes with
     the external memory.

     See tms9900.c for some more details on the cycle-precise implementation.

     This implementation also features all control lines and the instruction
     prefetch mechanism. Prefetching is explicitly triggered within the
     microprograms. The TMS9995 specification does not reveal the exact
     operations during the microprogram execution, so we have to look at the
     required cycle numbers to guess what is happening.

     Auto wait state:

     In order to enable automatic wait state creation, the READY line must be
     cleared on reset time. A good position to do this is MACHINE_RESET in
     the driver.


     References (see comments below)
     ----------
     [1] Texas Instruments 9900 Microprocessor series: TMS9995 16-bit Microcomputer

     Michael Zapf, June 2012
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tms9995.h"

#define NOPRG -1

/* tms9995 ST register bits. */
enum
{
	ST_LH = 0x8000,     // Logical higher (unsigned comparison)
	ST_AGT = 0x4000,    // Arithmetical greater than (signed comparison)
	ST_EQ = 0x2000,     // Equal
	ST_C = 0x1000,      // Carry
	ST_OV = 0x0800,     // Overflow (when using signed operations)
	ST_OP = 0x0400,     // Odd parity (used with byte operations)
	ST_X = 0x0200,      // XOP
	ST_OE = 0x0020,     // Overflow interrupt enabled
	ST_IM = 0x000f      // Interrupt mask
};

enum
{
	PENDING_NMI = 1,
	PENDING_MID = 2,
	PENDING_LEVEL1 = 4,
	PENDING_OVERFLOW = 8,
	PENDING_DECR = 16,
	PENDING_LEVEL4 = 32
};

static const ophandler s_microoperation[45];

static void tms9995_pulse_clock(struct tms9995 *tms, int count);
static void tms9995_service_interrupt(struct tms9995 *tms);
static void tms9995_int_prefetch_and_decode(struct tms9995 *tms);
static void tms9995_prefetch_and_decode(struct tms9995 *tms);
static void tms9995_decode(struct tms9995 *tms, uint16_t inst);
static void tms9995_word_read(struct tms9995 *tms);
static void tms9995_command_completed(struct tms9995 *tms);
static void tms9995_trigger_decrementer(struct tms9995 *tms);
static void tms9995_build_command_lookup_table(struct tms9995 *tms);
static void tms9995_disassemble(struct tms9995 *tms);

/****************************************************************************
    Some small helpers
****************************************************************************/


/* FIXME: 9537 rule ?? */
static bool is_onchip(struct tms9995 *tms, uint16_t addr)
{
	if ((addr & 0xFF00) == 0xF000 && addr < 0xF0FC)
		return true;
	if ((addr & 0xFFFC) == 0xFFFC)
		return true;
	/* FIXME: decrementer */
	return false;
}

/****************************************************************************
    Constructor
****************************************************************************/

struct tms9995 *tms9995_create(bool is_mp9537, bool bstep)
{
	struct tms9995 *tms = malloc(sizeof(struct tms9995));
	if (tms == NULL)
		return NULL;
	memset(tms, 0, sizeof(*tms));
	tms->mp9537 = is_mp9537;
#if 0
		tms->external_operation(*this),
		tms->clock_out_line(*this),
		tms->holda_line(*this)
#endif
	tms->check_overflow = !bstep;
	// Set up the lookup table for command decoding
	tms9995_build_command_lookup_table(tms); return tms;
}


void tms9995_trace(struct tms9995 *tms, bool onoff)
{
	tms->trace = onoff;
}

void tms9995_itrace(struct tms9995 *tms, bool onoff)
{
	tms->itrace = onoff;
}

enum {
	TMS9995_PC=0, TMS9995_WP, TMS9995_STATUS, TMS9995_IR,
	TMS9995_R0, TMS9995_R1, TMS9995_R2, TMS9995_R3,
	TMS9995_R4, TMS9995_R5, TMS9995_R6, TMS9995_R7,
	TMS9995_R8, TMS9995_R9, TMS9995_R10, TMS9995_R11,
	TMS9995_R12, TMS9995_R13, TMS9995_R14, TMS9995_R15
};

void tms9995_device_start(struct tms9995 *tms)
{
	// Clear the interrupt flags
	tms->int_pending = 0;

	tms->mid_flag = false;
	tms->mid_active = false;
	tms->nmi_active = false;
	tms->int_overflow = false;

	tms->reset = false;

	tms->idle_state = false;

	tms->source_value = 0;

	tms->index = 0;


	if (tms->itrace) fprintf(stderr, "Variant = %s, Overflow int = %s\n", tms->mp9537? "MP9537 (no on-chip RAM)" : "standard (with on-chip RAM)", tms->check_overflow? "check" : "no check");
}

char const *const tms9995_s_statename[20] =
{
	"PC",  "WP",  "ST",  "IR",
	"R0",  "R1",  "R2",  "R3",
	"R4",  "R5",  "R6",  "R7",
	"R8",  "R9",  "R10", "R11",
	"R12", "R13", "R14", "R15"
};

/*
    Provide access to the workspace registers via the debugger. We have to
    take care whether this is in onchip RAM or outside.
*/
uint16_t tms9995_read_workspace_register_debug(struct tms9995 *tms, int reg)
{
	uint16_t value;

	int addrb = (tms->WP + (reg << 1)) & 0xfffe;

	if (is_onchip(tms, addrb))
	{
		value = (tms->onchip_memory[addrb & 0x00fe]<<8) | tms->onchip_memory[(addrb & 0x00fe) + 1];
	}
	else
	{
		value = tms9995_readb_debug(tms, addrb) << 8;
		value |= tms9995_readb_debug(tms, addrb + 1);
	}
	return value;
}

/**************************************************************************
    Microprograms for the CPU instructions

    The actions which are specific to the respective instruction are
    invoked by repeated calls of ALU_xxx; each call increases a state
    variable so that on the next call, the next part can be processed.
    This saves us a lot of additional functions.
**************************************************************************/

/*
    Define the indices for the micro-operation table. This is done for the sake
    of a simpler microprogram definition as an uint8_t[].
*/
enum
{
	PREFETCH,
	PREFETCH_NO_INT,
	MEMORY_READ,
	MEMORY_WRITE,
	WORD_READ,
	WORD_WRITE,
	OPERAND_ADDR,
	INCREG,
	INDX,
	SET_IMM,
	RETADDR,
	RETADDR1,
	CRU_INPUT,
	CRU_OUTPUT,
	ABORT,
	END,

	ALU_NOP,
	ALU_ADD_S_SXC,
	ALU_B,
	ALU_BLWP,
	ALU_C,
	ALU_CI,
	ALU_CLR_SETO,
	ALU_DIV,
	ALU_DIVS,
	ALU_EXTERNAL,
	ALU_F3,
	ALU_IMM_ARITHM,
	ALU_JUMP,
	ALU_LDCR,
	ALU_LI,
	ALU_LIMIWP,
	ALU_LSTWP,
	ALU_MOV,
	ALU_MPY,
	ALU_RTWP,
	ALU_SBO_SBZ,
	ALU_SHIFT,
	ALU_SINGLE_ARITHM,
	ALU_STCR,
	ALU_STSTWP,
	ALU_TB,
	ALU_X,
	ALU_XOP,
	ALU_INT
};

#define MICROPROGRAM(_MP) \
	static const uint8_t _MP[] =

/*
    Cycles:
    XXXX 1 => needs one cycle
    xxxx 1 (1) => needs one cycle when accessing internal memory, two for external mem
    PREFETCH 0 (1) => occurs during the last step in parallel, needs one more when fetching from outside
    DECODE not shown here; assumed to happen during the next memory cycle; if there is none,
    add another cycle

    OPERAND_ADDR x => needs x cycles for address derivation; see the separate table

    Prefetch always needs 1 or 2 cycles; the previous command occurs in parallel
    to the prefetch, so we assign a 0 to the previous microprogram step
*/

MICROPROGRAM(operand_address_derivation)
{
	RETADDR, 0, 0, 0,                           // Register direct                  0
	WORD_READ, RETADDR, 0, 0,                   // Register indirect                1 (1)
	WORD_READ, RETADDR, 0, 0,                   // Symbolic                         1 (1)
	WORD_READ, INCREG, WORD_WRITE, RETADDR1,    // Reg indirect auto-increment      3 (1) (1)
	WORD_READ, INDX, WORD_READ, RETADDR         // Indexed                          3 (1) (1)
};

MICROPROGRAM(add_s_sxc_mp)
{
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1)
	OPERAND_ADDR,           // y
	MEMORY_READ,            // 1 (1)
	ALU_ADD_S_SXC,          // 0 (see above, occurs in parallel with PREFETCH)
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1) + decode in parallel (0)
	END
};

MICROPROGRAM(b_mp)
{
	OPERAND_ADDR,           // x
	ALU_NOP,                // 1 Don't read, just use the address
	ALU_B,                  // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1 Don't save the return address
	END
};

MICROPROGRAM(bl_mp)
{
	OPERAND_ADDR,           // x
	ALU_NOP,                // 1 Don't read, just use the address
	ALU_B,                  // 0 Re-use the alu operation from B
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	MEMORY_WRITE,           // 1 (1) Write R11
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(blwp_mp)
{
	OPERAND_ADDR,           // x Determine source address
	MEMORY_READ,            // 1 (1)
	ALU_BLWP,               // 1 Got new WP, save it; increase address, save
	MEMORY_WRITE,           // 1 (1) save old ST to new R15
	ALU_BLWP,               // 1
	MEMORY_WRITE,           // 1 (1) save old PC to new R14
	ALU_BLWP,               // 1
	MEMORY_WRITE,           // 1 (1) save old WP to new R13
	ALU_BLWP,               // 1 retrieve address
	MEMORY_READ,            // 1 (1) Read new PC
	ALU_BLWP,               // 0 Set new PC
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(c_mp)
{
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1)
	OPERAND_ADDR,           // y
	MEMORY_READ,            // 1 (1)
	ALU_C,                  // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1 decode
	END
};

MICROPROGRAM(ci_mp)
{
	MEMORY_READ,            // 1 (1) (reg)
	SET_IMM,                // 0 belongs to next cycle
	MEMORY_READ,            // 1 (1) (imm)
	ALU_CI,                 // 0 set status
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1 decode
	END
};

MICROPROGRAM(coc_czc_mp)
{
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1)
	ALU_F3,                 // 0
	MEMORY_READ,            // 1 (1)
	ALU_F3,                 // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1 decode
	END
};

MICROPROGRAM(clr_seto_mp)
{
	OPERAND_ADDR,           // x
	ALU_NOP,                // 1
	ALU_CLR_SETO,           // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(divide_mp)     // TODO: Verify cycles on the real machine
{
	OPERAND_ADDR,           // x Address of divisor S in Q=W1W2/S
	MEMORY_READ,            // 1 (1) Get S
	ALU_DIV,                // 1
	MEMORY_READ,            // 1 (1) Get W1
	ALU_DIV,                // 1 Check for overflow; skip next instruction if not
	ABORT,                  // 1
	MEMORY_READ,            // 1 (1) Get W2
	ALU_DIV,                // d Calculate quotient
	MEMORY_WRITE,           // 1 (1) Write quotient to &W1
	ALU_DIV,                // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1) Write remainder to &W2
	END
};

MICROPROGRAM(divide_signed_mp)  // TODO: Verify cycles on the real machine
{
	OPERAND_ADDR,           // x Address of divisor S in Q=W1W2/S
	MEMORY_READ,            // 1 (1) Get S
	ALU_DIVS,               // 1
	MEMORY_READ,            // 1 (1) Get W1
	ALU_DIVS,               // 1
	MEMORY_READ,            // 1 (1) Get W2
	ALU_DIVS,               // 1 Check for overflow, skip next instruction if not
	ABORT,                  // 1
	ALU_DIVS,               // d Calculate quotient
	MEMORY_WRITE,           // 1 (1) Write quotient to &W1
	ALU_DIVS,               // 0
	PREFETCH,               // 1
	MEMORY_WRITE,           // 1 (1) Write remainder to &W2
	END
};

MICROPROGRAM(external_mp)
{
	ALU_NOP,                // 1
	ALU_NOP,                // 1
	ALU_NOP,                // 1
	ALU_NOP,                // 1
	ALU_NOP,                // 1
	ALU_EXTERNAL,           // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(imm_arithm_mp)
{
	MEMORY_READ,            // 1 (1)
	SET_IMM,                // 0
	MEMORY_READ,            // 1 (1)
	ALU_IMM_ARITHM,         // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(jump_mp)
{
	ALU_NOP,                // 1
	ALU_JUMP,               // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(ldcr_mp)       // TODO: Verify cycles
{
	ALU_LDCR,               // 1
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1) Get source data
	ALU_LDCR,               // 1 Save it, point to R12
	WORD_READ,              // 1 (1) Get R12
	ALU_LDCR,               // 1 Prepare CRU operation
	CRU_OUTPUT,             // c
	ALU_NOP,                // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(li_mp)
{
	SET_IMM,                // 0
	MEMORY_READ,            // 1 (1)
	ALU_LI,                 // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(limi_lwpi_mp)
{
	SET_IMM,                // 0
	MEMORY_READ,            // 1 (1)
	ALU_NOP,                // 1
	ALU_LIMIWP,             // 0 lwpi, 1 limi
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(lst_lwp_mp)
{
	MEMORY_READ,            // 1 (1)
	ALU_NOP,                // 1
	ALU_LSTWP,              // 0 lwp, 1 lst
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(mov_mp)
{
	OPERAND_ADDR,           // 0
	MEMORY_READ,            // 1 (1)
	OPERAND_ADDR,           // 0
	ALU_MOV,                // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(multiply_mp)
{
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1)
	ALU_MPY,                // 1
	MEMORY_READ,            // 1 (1)
	ALU_MPY,                // 17
	MEMORY_WRITE,           // 1 (1)
	ALU_MPY,                // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(rtwp_mp)
{
	ALU_RTWP,               // 1
	MEMORY_READ,            // 1 (1)
	ALU_RTWP,               // 0
	MEMORY_READ,            // 1 (1)
	ALU_RTWP,               // 0
	MEMORY_READ,            // 1 (1)
	ALU_RTWP,               // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(sbo_sbz_mp)
{
	ALU_SBO_SBZ,            // 1 Set address = &R12
	WORD_READ,              // 1 (1) Read R12
	ALU_SBO_SBZ,            // 1 Add offset
	CRU_OUTPUT,             // 1 output via CRU
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(shift_mp)
{
	MEMORY_READ,            // 1 (1)
	ALU_SHIFT,              // 2 skip next operation if count != 0
	MEMORY_READ,            // 1 (1) if count=0 we must read R0
	ALU_SHIFT,              // c  do the shift
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(single_arithm_mp)
{
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1)
	ALU_SINGLE_ARITHM,      // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(stcr_mp)       // TODO: Verify on real machine
{
	ALU_STCR,               // 1      Check for byte operation
	OPERAND_ADDR,           // x     Source operand
	ALU_STCR,               // 1      Save, set R12
	WORD_READ,              // 1 (1) Read R12
	ALU_STCR,               // 1
	CRU_INPUT,              // c
	ALU_STCR,               // 13
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(stst_stwp_mp)
{
	ALU_STSTWP,             // 0
	ALU_NOP,                // 1
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(tb_mp)
{
	ALU_TB,                 // 1
	WORD_READ,              // 1 (1)
	ALU_TB,                 // 1
	CRU_INPUT,              // 2
	ALU_TB,                 // 0
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(x_mp)
{
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1)
	ALU_X,                  // 1
	END                     // should not be reached
};

MICROPROGRAM(xop_mp)
{
	OPERAND_ADDR,           // x     Determine source address
	ALU_XOP,                // 1     Save it; determine XOP number
	MEMORY_READ,            // 1 (1) Read new WP
	ALU_XOP,                // 1
	MEMORY_WRITE,           // 1 (1) save source address to new R11
	ALU_XOP,                // 1
	MEMORY_WRITE,           // 1 (1) save old ST to new R15
	ALU_XOP,                // 1
	MEMORY_WRITE,           // 1 (1) save old PC to new R14
	ALU_XOP,                // 1
	MEMORY_WRITE,           // 1 (1) save old WP to new R13
	ALU_XOP,                // 1
	MEMORY_READ,            // 1 (1) Read new PC
	ALU_XOP,                // 0 set new PC, set X flag
	PREFETCH,               // 1 (1)
	ALU_NOP,                // 1
	ALU_NOP,                // 1
	END
};

MICROPROGRAM(xor_mp)
{
	OPERAND_ADDR,           // x
	MEMORY_READ,            // 1 (1)
	ALU_F3,                 // 0
	MEMORY_READ,            // 1 (1)
	ALU_F3,                 // 0
	PREFETCH,               // 1 (1)
	MEMORY_WRITE,           // 1 (1)
	END
};

MICROPROGRAM(int_mp)
{
	ALU_INT,                // 1
	MEMORY_READ,            // 1 (1)
	ALU_INT,                // 2
	MEMORY_WRITE,           // 1 (1)
	ALU_INT,                // 1
	MEMORY_WRITE,           // 1 (1)
	ALU_INT,                // 1
	MEMORY_WRITE,           // 1 (1)
	ALU_INT,                // 1
	MEMORY_READ,            // 1 (1)
	ALU_INT,                // 0
	PREFETCH_NO_INT,        // 1 (1)  (prefetch happens in parallel to the previous operation)
	ALU_NOP,                // 1 (+decode in parallel; actually performed right after prefetch)
	ALU_NOP,                // 1
	END
};


/*****************************************************************************
    CPU instructions
*****************************************************************************/

/*
    Available instructions
    MID is not a real instruction but stands for an invalid operation which
    triggers a "macro instruction detect" interrupt. Neither is INTR which
    indicates an interrupt handling in progress.
*/
enum
{
	MID=0, A, AB, ABS, AI, ANDI, B, BL, BLWP, C,
	CB, CI, CKOF, CKON, CLR, COC, CZC, DEC, DECT, DIV,
	DIVS, IDLE, INC, INCT, INV, JEQ, JGT, JH, JHE, JL,
	JLE, JLT, JMP, JNC, JNE, JNO, JOC, JOP, LDCR, LI,
	LIMI, LREX, LST, LWP, LWPI, MOV, MOVB, MPY, MPYS, NEG,
	ORI, RSET, RTWP, S, SB, SBO, SBZ, SETO, SLA, SOC,
	SOCB, SRA, SRC, SRL, STCR, STST, STWP, SWPB, SZC, SZCB,
	TB, X, XOP, XOR, INTR, OPAD
};

static const char opname[][5] =
{   "MID ", "A   ", "AB  ", "ABS ", "AI  ", "ANDI", "B   ", "BL  ", "BLWP", "C   ",
	"CB  ", "CI  ", "CKOF", "CKON", "CLR ", "COC ", "CZC ", "DEC ", "DECT", "DIV ",
	"DIVS", "IDLE", "INC ", "INCT", "INV ", "JEQ ", "JGT ", "JH  ", "JHE ", "JL  ",
	"JLE ", "JLT ", "JMP ", "JNC ", "JNE ", "JNO ", "JOC ", "JOP ", "LDCR", "LI  ",
	"LIMI", "LREX", "LST ", "LWP ", "LWPI", "MOV ", "MOVB", "MPY ", "MPYS", "NEG ",
	"ORI ", "RSET", "RTWP", "S   ", "SB  ", "SBO ", "SBZ ", "SETO", "SLA ", "SOC ",
	"SOCB", "SRA ", "SRC ", "SRL ", "STCR", "STST", "STWP", "SWPB", "SZC ", "SZCB",
	"TB  ", "X   ", "XOP ", "XOR ", "*int", "*oad"
};

/*
    Formats:

          0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    ----+------------------------------------------------+
    1   | Opcode | B | Td |  RegNr     | Ts |    RegNr   |
        +--------+---+----+------------+----+------------+
    2   |  Opcode               |      Displacement      |
        +-----------------------+------------------------+
    3   |  Opcode         |  RegNr     | Ts |    RegNr   |
        +-----------------+------------+----+------------+
    4   |  Opcode         |  Count     | Ts |    RegNr   |
        +-----------------+------------+----+------------+
    5   |  Opcode               |  Count    |    RegNr   |
        +-----------------------+-----------+------------+
    6   |  Opcode                      | Ts |    RegNr   |
        +------------------------------+----+------------+
    7   |  Opcode                         |0| 0| 0| 0| 0 |
        +---------------------------------+-+--+--+--+---+
    8   |  Opcode                         |0|    RegNr   |
        +---------------------------------+-+------------+
    9   |  Opcode         |   Reg/Nr   | Ts |    RegNr   |
        +-----------------+------------+----+------------+
    10  |  Opcode                      | Ts |    RegNr   |   (DIVS, MPYS)
        +------------------------------+----+------------+
    11  |  Opcode                           |    RegNr   |   (LST, LWP)
        +-----------------------------------+------------+
*/

/*
    Defines the number of bits from the left which are significant for the
    command in the respective format.
*/
static const int format_mask_len[] =
{
	0, 4, 8, 6, 6, 8, 10, 16, 12, 6, 10, 12
};

const tms9995_instruction s_command[] =
{
	// Base opcode list
	// Opcode, ID, format, microprg
	{ 0x0080, LST, 11, lst_lwp_mp },
	{ 0x0090, LWP, 11, lst_lwp_mp },
	{ 0x0180, DIVS, 10, divide_signed_mp },
	{ 0x01C0, MPYS, 10, multiply_mp },
	{ 0x0200, LI, 8, li_mp },
	{ 0x0220, AI, 8, imm_arithm_mp },
	{ 0x0240, ANDI, 8, imm_arithm_mp },
	{ 0x0260, ORI, 8, imm_arithm_mp },
	{ 0x0280, CI, 8, ci_mp },
	{ 0x02a0, STWP, 8, stst_stwp_mp },
	{ 0x02c0, STST, 8, stst_stwp_mp },
	{ 0x02e0, LWPI, 8, limi_lwpi_mp },
	{ 0x0300, LIMI, 8, limi_lwpi_mp },
	{ 0x0340, IDLE, 7, external_mp },
	{ 0x0360, RSET, 7, external_mp },
	{ 0x0380, RTWP, 7, rtwp_mp },
	{ 0x03a0, CKON, 7, external_mp },
	{ 0x03c0, CKOF, 7, external_mp },
	{ 0x03e0, LREX, 7, external_mp },
	{ 0x0400, BLWP, 6, blwp_mp },
	{ 0x0440, B, 6, b_mp },
	{ 0x0480, X, 6, x_mp },
	{ 0x04c0, CLR, 6, clr_seto_mp },
	{ 0x0500, NEG, 6, single_arithm_mp },
	{ 0x0540, INV, 6, single_arithm_mp },
	{ 0x0580, INC, 6, single_arithm_mp },
	{ 0x05c0, INCT, 6, single_arithm_mp },
	{ 0x0600, DEC, 6, single_arithm_mp },
	{ 0x0640, DECT, 6, single_arithm_mp },
	{ 0x0680, BL, 6, bl_mp },
	{ 0x06c0, SWPB, 6, single_arithm_mp },
	{ 0x0700, SETO, 6, clr_seto_mp },
	{ 0x0740, ABS, 6, single_arithm_mp },
	{ 0x0800, SRA, 5, shift_mp },
	{ 0x0900, SRL, 5, shift_mp },
	{ 0x0a00, SLA, 5, shift_mp },
	{ 0x0b00, SRC, 5, shift_mp },
	{ 0x1000, JMP, 2, jump_mp },
	{ 0x1100, JLT, 2, jump_mp },
	{ 0x1200, JLE, 2, jump_mp },
	{ 0x1300, JEQ, 2, jump_mp },
	{ 0x1400, JHE, 2, jump_mp },
	{ 0x1500, JGT, 2, jump_mp },
	{ 0x1600, JNE, 2, jump_mp },
	{ 0x1700, JNC, 2, jump_mp },
	{ 0x1800, JOC, 2, jump_mp },
	{ 0x1900, JNO, 2, jump_mp },
	{ 0x1a00, JL, 2, jump_mp },
	{ 0x1b00, JH, 2, jump_mp },
	{ 0x1c00, JOP, 2, jump_mp },
	{ 0x1d00, SBO, 2, sbo_sbz_mp },
	{ 0x1e00, SBZ, 2, sbo_sbz_mp },
	{ 0x1f00, TB, 2, tb_mp },
	{ 0x2000, COC, 3, coc_czc_mp },
	{ 0x2400, CZC, 3, coc_czc_mp },
	{ 0x2800, XOR, 3, xor_mp },
	{ 0x2c00, XOP, 3, xop_mp },
	{ 0x3000, LDCR, 4, ldcr_mp },
	{ 0x3400, STCR, 4, stcr_mp },
	{ 0x3800, MPY, 9, multiply_mp },
	{ 0x3c00, DIV, 9, divide_mp },
	{ 0x4000, SZC, 1, add_s_sxc_mp },
	{ 0x5000, SZCB, 1, add_s_sxc_mp },
	{ 0x6000, S, 1, add_s_sxc_mp },
	{ 0x7000, SB, 1, add_s_sxc_mp },
	{ 0x8000, C, 1, c_mp },
	{ 0x9000, CB, 1, c_mp },
	{ 0xa000, A, 1, add_s_sxc_mp },
	{ 0xb000, AB, 1, add_s_sxc_mp },
	{ 0xc000, MOV, 1, mov_mp },
	{ 0xd000, MOVB, 1, mov_mp },
	{ 0xe000, SOC, 1, add_s_sxc_mp },
	{ 0xf000, SOCB, 1, add_s_sxc_mp },

// Special entries for interrupt and the address derivation subprogram; not in lookup table
	{ 0x0000, INTR, 1, int_mp},
	{ 0x0000, OPAD, 1, operand_address_derivation }
};

/*
    Create a B-tree for looking up the commands. Each node can carry up to
    16 entries, indexed by 4 consecutive bits in the opcode.

    See tms9900.c for a detailed description.

    Would actually probably be better to just binary search the
    above with masks precomputed
*/

static struct tms_decode *decode_root;

/*
 *	Make a new empty tree node
 */
static struct tms_decode *new_decode(void)
{
	struct tms_decode *d = malloc(sizeof(struct tms_decode));
	unsigned int i;

	if (d == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	for (i = 0; i < 16; i++) {
		d->index[i] = NOPRG;
		d->next_digit[i] = NULL;
	}
	return d;
}

/*
 *	Insert an instruction into the decode tree
 */
static void insert_decode(const tms9995_instruction *inst, unsigned int inum)
{
	uint16_t opcode = inst->opcode;
	unsigned int bitcount = 4;
	struct tms_decode *decode = decode_root;
	unsigned int i;
	unsigned int n;
	unsigned int idx;

	n = format_mask_len[inst->format];

	while(bitcount < n) {
		idx = (opcode >> 12) & 0x0F;
		if (decode->next_digit[idx] == NULL)
			decode->next_digit[idx] = new_decode();
		decode = decode->next_digit[idx];
		bitcount += 4;
		opcode <<= 4;
	}
	idx = (opcode >> 12) & 0x0F;
	/* Insert the terminals */
	n = 1 << (bitcount - n);
	for (i = 0; i < n; i++)
	{
//		fprintf(stderr, "opcode=%04x at position %d\n", inst->opcode, idx);
		decode->index[idx + i] = inum;
	}

}

/*
 *	Build the decode tree
 */
static void tms9995_build_command_lookup_table(struct tms9995 *tms)
{
	int inum = 0;
	const tms9995_instruction *inst = s_command;

	/* Built already ? */
	if (decode_root)
		return;

	decode_root = new_decode();

	while(inst->opcode != 0) {
		insert_decode(inst, inum);
		inst++;
		inum++;
	}
	// Save the index to these two special microprograms
	tms->interrupt_mp_index = inum++;
	tms->operand_address_derivation_index = inum;
}

/*
    Main execution loop

    For each invocation of execute_run, a number of loop iterations has been
    calculated before (tms->icount). Each loop iteration is one clock cycle.
    The loop must be executed for the number of times that corresponds to the
    time until the next timer event.
*/
void tms9995_execute_run(struct tms9995 *tms, unsigned int cycles)
{

	tms->icount = cycles;

	if (tms->reset) tms9995_service_interrupt(tms);

	if (tms->itrace) fprintf(stderr, "calling execute_run for %d cycles\n", tms->icount);
	do
	{
		// Normal operation
		if (tms->check_ready && tms->ready == false)
		{
			// We are in a wait state
			if (tms->itrace) fprintf(stderr, "wait\n");
			// The clock output should be used to change the state of an outer
			// device which operates the READY line
			tms9995_pulse_clock(tms, 1);
		}
		else
		{
			if (tms->check_hold && tms->hold_requested)
			{
				tms9995_set_hold_state(tms, true);
				if (tms->itrace) fprintf(stderr, "HOLD state\n");
				tms9995_pulse_clock(tms, 1);
			}
			else
			{
				tms9995_set_hold_state(tms, false);

				tms->check_ready = false;

				if (tms->itrace) fprintf(stderr, "main loop, operation %s, MPC = %d\n", opname[tms->command], tms->MPC);
				uint8_t* program = (uint8_t *)s_command[tms->index].prog;
				s_microoperation[program[tms->MPC]](tms);

				// For multi-pass operations where the MPC should not advance
				// or when we have put in a new microprogram
				tms->pass--;
				if (tms->pass<=0)
				{
					tms->pass = 1;
					tms->MPC++;
				}
			}
		}
	} while (tms->icount>0 && !tms->reset);

	if (tms->itrace) fprintf(stderr, "cycles expired; will return soon.\n");
}

/**************************************************************************/

/*
    Interrupt input
    output
        tms->nmi_state
        tms->irq_level
        flag[2], flag[4]
*/
void tms9995_execute_set_input(struct tms9995 *tms, int irqline, bool state)
{
	if (irqline == INT_9995_RESET)
	{
		if (state)
		{
			if (tms->itrace)
				fprintf(stderr, "RESET interrupt line; READY=%d\n", tms->ready_bufd);
			tms9995_reset_line(tms, true);
		}
	}
	else
	{
		if (irqline == INPUT_LINE_NMI)
		{
			tms->nmi_active = (state==true);
			if (tms->itrace) fprintf(stderr, "NMI interrupt line state=%d\n", state);
		}
		else
		{
			if (irqline == INT_9995_INT1)
			{
				// *active means that the signal is still present on the input.
				// The latch can only be reset when this signal is clear.
				tms->int1_active = (state==true);
				if (tms->itrace) fprintf(stderr, "Line INT1 state=%d\n", state);
				// Latch the INT
				if (state==true)
				{
					if (tms->itrace) fprintf(stderr, "Latch INT1\n");
					tms->flag[2] = true;
				}
			}
			else
			{
				if (irqline == INT_9995_INT4)
				{
					if (tms->itrace) fprintf(stderr, "Line INT4/EC state=%d\n", state);
					if (tms->flag[0]==false)
					{
						tms->int4_active = (state==true);
						if (tms->itrace) fprintf(stderr, "set as interrupt\n");
						// Latch the INT
						if (state==true)
						{
							if (tms->itrace) fprintf(stderr, "Latch INT4\n");
							tms->flag[4] = true;
						}
					}
					else
					{
						if (tms->itrace) fprintf(stderr, "set as event count\n");
						tms9995_trigger_decrementer(tms);
					}
				}
				else
				{
					fprintf(stderr, "tms9995: Accessed invalid interrupt line %d\n", irqline);
				}
			}
		}
	}
}

/*
    Triggers a RESET.
*/
void tms9995_reset_line(struct tms9995 *tms, bool state)
{
	if (state)
	{
		tms->reset = true;     // for the main loop
		tms->log_interrupt = false;   // only for debugging
		tms->request_auto_wait_state = false;
		tms->hold_requested = false;
		memset(tms->flag, 0, sizeof(tms->flag));
	}
}

/*
    Issue a pulse on the clock line.
*/
static void tms9995_pulse_clock(struct tms9995 *tms, int count)
{
	int i;
	for (i = 0; i < count; i++)
	{
		tms->ready = tms->ready_bufd && !tms->request_auto_wait_state;                // get the latched READY state
		tms->icount--;                         // This is the only location where we count down the cycles.

		if (tms->itrace) {
			if (tms->check_ready)
				fprintf(stderr, "tms9995_pulse_clock, READY=%d, auto_wait=%d\n", tms->ready_bufd? 1:0, tms->auto_wait? 1:0);
			else
				fprintf(stderr, "tms9995_pulse_clock\n");
		}

		tms->request_auto_wait_state = false;
		if (tms->flag[0] == false && tms->flag[1] == true)
		{
			// Section 2.3.1.2.2: "by decreasing the count in the Decrementing
			// Register by one for each fourth CLKOUT cycle"
			tms->decrementer_clkdiv = (tms->decrementer_clkdiv+1)%4;
			if (tms->decrementer_clkdiv==0) tms9995_trigger_decrementer(tms);
		}
	}
}

/*
    Enter the hold state.
*/
void tms9995_hold_line(struct tms9995 *tms, bool state)
{
	tms->hold_requested = state;
	if (tms->itrace) fprintf(stderr, "set HOLD = %d\n", state);
}

/*
    Signal READY to the CPU. When cleared, the CPU enters wait states. This
    becomes effective on a clock pulse.
*/
void tms9995_ready_line(struct tms9995 *tms, bool state)
{
	if (state != tms->ready_bufd)
	{
		if (tms->reset)
		{
			if (tms->itrace) fprintf(stderr, "Ignoring READY=%d change due to pending RESET\n", state);
		}
		else
		{
			tms->ready_bufd = state;
			if (tms->itrace) fprintf(stderr, "set READY = %d\n", tms->ready_bufd? 1 : 0);
		}
	}
}

/*
    When the divide operations fail, we get to this operation.
*/
static void tms9995_abort_operation(struct tms9995 *tms)
{
	tms9995_int_prefetch_and_decode(tms); // do not forget to prefetch
	// And don't forget that prefetch is a 2-pass operation, so this method
	// will be called a second time. Only when the lowbyte has been fetched,
	// continue with the next step
	if (tms->mem_phase==1) tms9995_command_completed(tms);
}

/*
    Enter or leave the hold state. We only operate the HOLDA line when there is a change.
*/
void tms9995_set_hold_state(struct tms9995 *tms, bool state)
{
	tms->hold_state = state;
}

/*
    Decode the instruction. This is done in parallel to other operations
    so we just do it together with the prefetch.
*/
static void tms9995_decode(struct tms9995 *tms, uint16_t inst)
{
	int ix = 0;
	struct tms_decode *table = decode_root;
	uint16_t opcode = inst;
	bool complete = false;
	int program_index;

	tms->mid_active = false;

	while (!complete)
	{
		ix = (opcode >> 12) & 0x000f;
		if (tms->itrace) fprintf(stderr, "Check next hex digit of instruction %x\n", ix);
		if (table->next_digit[ix] != NULL)
		{
			table = table->next_digit[ix];
			opcode = opcode << 4;
		}
		else complete = true;
	}

	program_index = table->index[ix];
	if (program_index == NOPRG)
	{
		// not found
		if (tms->trace) fprintf(stderr, "Undefined opcode %04x at logical address %04x, will trigger MID\n", inst, tms->PC);
		tms->pre_IR = 0;
		tms->pre_command = MID;
	}
	else
	{
		const tms9995_instruction *decoded = s_command + program_index;

		tms->pre_IR = inst;
		tms->pre_command = decoded->id;
		tms->pre_index = program_index;
		tms->pre_byteop = ((decoded->format == 1) && ((inst & 0x1000)!=0));
		if (tms->itrace) fprintf(stderr, "Command decoded as id %d, %s, base opcode %04x\n", decoded->id, opname[decoded->id], decoded->opcode);
		tms->pass = 1;
	}
}

/*
    Fetch the next instruction and check pending interrupts before.
    Getting an instruction is a normal memory access (plus an asserted IAQ line),
    so this is subject to wait state handling. We have to allow for a two-pass
    handling.
*/
static void tms9995_int_prefetch_and_decode(struct tms9995 *tms)
{
	int intmask = tms->ST & 0x000f;

	if (tms->mem_phase == 1)
	{
		// Check interrupt lines
		if (tms->nmi_active)
		{
			if (tms->itrace) fprintf(stderr, "Checking interrupts ... NMI active\n");
			tms->int_pending |= PENDING_NMI;
			tms->idle_state = false;
			tms->PC = (tms->PC + 2) & 0xfffe;     // we have not prefetched the next instruction
			return;
		}
		else
		{
			tms->int_pending = 0;
			// If the current command is XOP or BLWP, ignore the interrupt
			if (tms->command != XOP && tms->command != BLWP)
			{
				// The actual interrupt trigger is an OR of the latch and of
				// the interrupt line (for INT1 and INT4); see [1],
				// section 2.3.2.1.3
				if ((tms->int1_active || tms->flag[2]) && intmask >= 1) tms->int_pending |= PENDING_LEVEL1;
				if (tms->int_overflow && intmask >= 2) tms->int_pending |= PENDING_OVERFLOW;
				if (tms->flag[3] && intmask >= 3) tms->int_pending |= PENDING_DECR;
				if ((tms->int4_active || tms->flag[4]) && intmask >= 4) tms->int_pending |= PENDING_LEVEL4;
			}

			if (tms->int_pending!=0)
			{
				if (tms->idle_state)
				{
					tms->idle_state = false;
					if (tms->itrace) fprintf(stderr, "Interrupt occurred, terminate IDLE state\n");
				}
				tms->PC = tms->PC + 2;        // PC must be advanced (see flow chart), but no prefetch
				if (tms->itrace) fprintf(stderr, "Interrupts pending; no prefetch; advance PC to %04x\n", tms->PC);
				return;
			}
			else
			{
				// No pending interrupts
				if (tms->idle_state)
				{
					if (tms->itrace) fprintf(stderr, "IDLE state\n");
					// We are IDLE, stay in the loop and do not advance the PC
					tms->pass = 2;
					tms9995_pulse_clock(tms, 1);
					return;
				}
			}
		}
	}

	// We reach this point in phase 1 if there is no interrupt and in all other phases
	tms9995_prefetch_and_decode(tms);
}

/*
    The actual prefetch operation, but without the interrupt check. This one is
    needed when we complete the interrupt handling and need to get the next
    instruction. According to the flow chart in [1], the prefetch after the
    interrupt handling ignores other pending interrupts.
*/
static void tms9995_prefetch_and_decode(struct tms9995 *tms)
{
	if (tms->mem_phase==1)
	{
		// Fetch next instruction
		// Save these values; they have been computed during the current instruction execution
		tms->address_copy = tms->address;
		tms->value_copy = tms->current_value;
		tms->iaq = true;
		tms->address = tms->PC;
		if (tms->itrace) fprintf(stderr, "** Prefetching new instruction at %04x **\n", tms->PC);
	}

	tms9995_word_read(tms); // changes tms->mem_phase

	if (tms->mem_phase==1)
	{
		// We're back in phase 1, i.e. the whole prefetch is done
		tms9995_decode(tms, tms->current_value);    // This is for free; in reality it is in parallel with the next memory operation
		tms->address = tms->address_copy;     // restore tms->address
		tms->current_value = tms->value_copy; // restore tms->current_value
		tms->PC = (tms->PC + 2) & 0xfffe;     // advance PC
		tms->iaq = false;
		if (tms->itrace) fprintf(stderr, "++ Prefetch done ++\n");
	}
}

/*
    Used by the normal command completion as well as by the X operation. We
    assume that we have a fully decoded operation which was previously
    prefetched.
*/
static void tms9995_next_command(struct tms9995 *tms)
{
	// Copy the prefetched results
	tms->IR = tms->pre_IR;
	tms->command = tms->pre_command;
	tms->index = tms->pre_index;
	tms->byteop = tms->pre_byteop;

	tms->inst_state = 0;

	if (tms->command == MID)
	{
		tms->mid_flag = true;
		tms->mid_active = true;
		tms9995_service_interrupt(tms);
	}
	else
	{
		tms->get_destination = false;
		// This is a preset for opcodes which do not need an opcode address derivation
		tms->address = tms->WP + ((tms->IR & 0x000f)<<1);
		tms->MPC = -1;
		if (tms->itrace) {
			fprintf(stderr, "===== %04x: Op=%04x (%s)\n", tms->PC-2, tms->IR, opname[tms->command]);

			// Mark logged address as interrupt service
			if (tms->log_interrupt)
				fprintf(stderr, "i%04x\n", tms->PC-2);
			else
				fprintf(stderr, "%04x\n", tms->PC-2);
		}

		tms->PC_debug = tms->PC - 2;
		if (tms->trace)
			tms9995_disassemble(tms);
		tms->first_cycle = tms->icount;
	}
}

/*
    End of command execution
*/
static void tms9995_command_completed(struct tms9995 *tms)
{
	// Pseudo state at the end of the current instruction cycle sequence
	if (tms->itrace)
	{
		// logerror("+++++ Instruction %04x (%s) completed", IR, opname[tms->command]);
		int cycles =  tms->first_cycle - tms->icount;
		// Avoid nonsense values due to expired and resumed main loop
		// if (cycles > 0 && cycles < 10000) logerror(", consumed %d cycles", cycles);
		// logerror(" +++++\n");
		if (cycles > 0 && cycles < 10000)
			fprintf(stderr, "tms9995: %04x %s [%02d]\n", tms->PC_debug, opname[tms->command], cycles);
		else
			fprintf(stderr, "tms9995: %04x %s [ ?]\n", tms->PC_debug, opname[tms->command]);
	}

	if (tms->int_pending != 0)
		tms9995_service_interrupt(tms);
	else
	{
		if ((tms->ST & ST_OE)!=0 && (tms->ST & ST_OV)!=0 && (tms->ST & 0x000f)>2)
			tms9995_service_interrupt(tms);
		else
			tms9995_next_command(tms);
	}
}

/*
    Handle pending interrupts.
*/
static void tms9995_service_interrupt(struct tms9995 *tms)
{
	int vectorpos;

	if (tms->reset)
	{
		vectorpos = 0;
		tms->intmask = 0;  // clear interrupt mask

		tms->nmi_state = false;
		tms->hold_requested = false;
		tms->hold_state = false;
		tms->mem_phase = 1;
		tms->check_hold = true;
		tms->word_access = false;
		tms->int1_active = false;
		tms->int4_active = false;
		tms->decrementer_clkdiv = 0;

		tms->pass = 0;

		memset(tms->flag, 0, sizeof(tms->flag));

		tms->ST = 0;

		// The auto-wait state generation is turned on when the READY line is cleared
		// on RESET.
		tms->auto_wait = !tms->ready_bufd;
		if (tms->itrace)
			fprintf(stderr, "RESET; automatic wait state creation is %s\n", tms->auto_wait? "enabled":"disabled");
		// We reset the READY flag, or the CPU will not start
		tms->ready_bufd = true;
	}
	else
	{
		if (tms->mid_active)
		{
			vectorpos = 0x0008;
			tms->intmask = 0x0001;
			tms->PC = (tms->PC + 2) & 0xfffe;
			if (tms->itrace) fprintf(stderr, "** MID pending\n");
			tms->mid_active = false;
		}
		else
		{
			if ((tms->int_pending & PENDING_NMI)!=0)
			{
				vectorpos = 0xfffc;
				tms->int_pending &= ~PENDING_NMI;
				tms->intmask = 0;
				if (tms->itrace) fprintf(stderr, "** NMI pending\n");
			}
			else
			{
				if ((tms->int_pending & PENDING_LEVEL1)!=0)
				{
					vectorpos = 0x0004;
					tms->int_pending &= ~PENDING_LEVEL1;
					// Latches must be reset when the interrupt is serviced
					// Since the latch is edge-triggered, we should be allowed
					// to clear it right here, without considering the line state
					tms->flag[2] = false;
					tms->intmask = 0;
					if (tms->itrace) fprintf(stderr, "** INT1 pending\n");
				}
				else
				{
					if ((tms->int_pending & PENDING_OVERFLOW)!=0)
					{
						vectorpos = 0x0008;
						tms->int_pending &= ~PENDING_OVERFLOW;
						tms->intmask = 0x0001;
						if (tms->itrace) fprintf(stderr, "** OVERFL pending\n");
					}
					else
					{
						if ((tms->int_pending & PENDING_DECR)!=0)
						{
							vectorpos = 0x000c;
							tms->intmask = 0x0002;
							tms->int_pending &= ~PENDING_DECR;
							tms->flag[3] = false;
							if (tms->itrace) fprintf(stderr, "** DECR pending\n");
						}
						else
						{
							vectorpos = 0x0010;
							tms->intmask = 0x0003;
							tms->int_pending &= ~PENDING_LEVEL4;
							// See above for clearing the latch
							tms->flag[4] = false;
							if (tms->itrace) fprintf(stderr, "** INT4 pending\n");
						}
					}
				}
			}
		}
	}

	if (tms->itrace)
		fprintf(stderr, "*** triggered an interrupt with vector %04x/%04x\n", vectorpos, vectorpos+2);

	// just for debugging purposes
	if (!tms->reset) tms->log_interrupt = true;

	// The microinstructions will do the context switch
	tms->address = vectorpos;

	tms->index = tms->interrupt_mp_index;
	tms->inst_state = 0;
	tms->byteop = false;
	tms->command = INTR;

	tms->pass = tms->reset? 1 : 2;
	tms->from_reset = tms->reset;

	if (tms->reset)
	{
		tms->IR = 0x0000;
		tms->reset = false;
	}
	tms->MPC = 0;
	tms->first_cycle = tms->icount;
	tms->check_ready = false;      // set to default
}

/*
    Read memory. This method expects as input tms->address, and delivers the value
    in tms->current_value. For a single byte read, the byte is put into the high byte.
    This method uses the tms->pass variable to achieve a two-pass handling for
    getting the complete word (high byte, low byte).

    input:
        tms->address
        tms->lowbyte
    output:
        tms->current_value

    tms->address is unchanged

    Make sure that tms->lowbyte is false on the first call.
*/
void tms9995_mem_read(struct tms9995 *tms)
{
	// First determine whether the memory is inside the CPU
	// On-chip memory is F000 ... F0F9, F0FA-FFF9 = off-chip, FFFA/B = Decrementer
	// FFFC-FFFF = NMI vector (on-chip)
	// There is a variant of the TMS9995 with no on-chip RAM which was used
	// for the TI-99/8 (9537).

	if ((tms->address & 0xfffe)==0xfffa && !tms->mp9537)
	{
		if (tms->itrace) fprintf(stderr, "read dec=%04x\n", tms->decrementer_value);
		// Decrementer mapped into the address space
		tms->current_value = tms->decrementer_value;
		if (tms->byteop)
		{
			// When reading FFFB, return the lower byte
			if ((tms->address & 1)==1) tms->current_value <<= 8;
			tms->current_value &= 0xff00;
		}
		tms9995_pulse_clock(tms, 1);
		return;
	}

	if (is_onchip(tms, tms->address))
	{
		uint16_t intaddr;

		// If we have a word access, we have to align the address
		// This is the case for word operations and for certain phases of
		// byte operations (e.g. when retrieving the index register)
		if (tms->word_access || !tms->byteop) tms->address &= 0xfffe;

		if (tms->itrace)
			fprintf(stderr, "read onchip memory (single pass, address %04x)\n", tms->address);

		// Ignore the READY state
		tms->check_ready = false;

		// We put fffc-ffff back into the f000-f0ff area
		intaddr = tms->address & 0x00fe;

		// An on-chip memory access is also visible to the outside world ([1], 2.3.1.2)
		// but only on word boundary, as only full words are read.

		// Always read a word from internal memory
		tms->current_value = (tms->onchip_memory[intaddr] << 8) | tms->onchip_memory[intaddr + 1];

		if (!tms->word_access && tms->byteop)
		{
			if ((tms->address & 1)==1) tms->current_value = tms->current_value << 8;
			tms->current_value &= 0xff00;
		}
		tms9995_pulse_clock(tms, 1);
	}
	else
	{
		// This is an off-chip access
		tms->check_ready = true;
		uint8_t value;
		uint16_t address = tms->address;

		switch (tms->mem_phase)
		{
		case 1:
			// Set address
			// If this is a word access, 4 passes, else 2 passes
			if (tms->word_access || !tms->byteop)
			{
				tms->pass = 4;
				// For word accesses, we always start at the even address
				address &= 0xfffe;
			}
			else tms->pass = 2;

			tms->check_hold = false;
			if (tms->itrace) fprintf(stderr, "set address bus %04x\n", tms->address & 0xfffe);
			tms->request_auto_wait_state = tms->auto_wait;
			tms9995_pulse_clock(tms, 1);
			break;
		case 2:
			// Sample the value on the data bus (high byte)
			if (tms->word_access || !tms->byteop) address &= 0xfffe;
			value = tms9995_readb(tms, address);
			if (tms->itrace)
				fprintf(stderr, "memory read byte %04x -> %02x\n", tms->address & 0xfffe, value);
			tms->current_value = (value << 8) & 0xff00;
			break;
		case 3:
			// Set address + 1 (unless byte command)
			if (tms->itrace) fprintf(stderr, "set address bus %04x\n", tms->address | 1);
			tms->request_auto_wait_state = tms->auto_wait;
			tms9995_pulse_clock(tms, 1);
			break;
		case 4:
			// Read low byte
			value = tms9995_readb(tms, tms->address | 1);
			tms->current_value |= value;
			if (tms->itrace)
				fprintf(stderr, "memory read byte %04x -> %02x, complete word = %04x\n", tms->address | 1, value, tms->current_value);
			tms->check_hold = true;
			break;
		}

		tms->mem_phase = (tms->mem_phase % 4) +1;

		// Reset to 1 when we are done
		if (tms->pass==1)
		{
			tms->mem_phase = 1;
			tms->check_hold = true;
		}
	}
}

/*
    Read a word. This is independent of the byte flag of the instruction.
    We need this variant especially when we have to retrieve a register value
    in indexed addressing within a byte-oriented operation.
*/
static void tms9995_word_read(struct tms9995 *tms)
{
	tms->word_access = true;
	tms9995_mem_read(tms);
	tms->word_access = false;
}

/*
    Write memory. This method expects as input tms->address and tms->current_value.
    For a single byte write, the byte to be written is expected to be in the
    high byte of tms->current_value.
    This method uses the tms->pass variable to achieve a two-pass handling for
    writing the complete word (high byte, low byte).

    input:
        tms->address
        tms->lowbyte
        tms->current_value

    output:
        -
    tms->address is unchanged

    Make sure that tms->lowbyte is false on the first call.
*/
static void tms9995_mem_write(struct tms9995 *tms)
{
	if ((tms->address & 0xfffe)==0xfffa && !tms->mp9537)
	{
		if (tms->byteop)
		{
			// According to [1], section 2.3.1.2.2:
			// "The decrementer should always be accessed as a full word. [...]
			// Writing a single byte to either of the bytes of the decrementer
			// will result in the data byte being written into the byte specifically addressed
			// and random bits being written into the other byte of the decrementer."

			// So we just don't care about the low byte.
			if (tms->address == 0xfffb) tms->current_value >>= 8;

			// dito: "This also loads the Decrementing Register with the same count."
			tms->starting_count_storage_register = tms->decrementer_value = tms->current_value;
		}
		else
		{
			tms->starting_count_storage_register = tms->decrementer_value = tms->current_value;
		}
		if (tms->itrace) fprintf(stderr, "Setting dec=%04x [tms->PC=%04x]\n", tms->current_value, tms->PC);
		tms9995_pulse_clock(tms, 1);
		return;
	}

	if (is_onchip(tms, tms->address))
	{
		// If we have a word access, we have to align the address
		// This is the case for word operations and for certain phases of
		// byte operations (e.g. when retrieving the index register)
		if (tms->word_access || !tms->byteop) tms->address &= 0xfffe;

		if (tms->itrace)
			fprintf(stderr, "write to onchip memory (single pass, address %04x, value=%04x)\n", tms->address, tms->current_value);

		// An on-chip memory access is also visible to the outside world ([1], 2.3.1.2)
		// but only on word boundary
		// This would normally be a no-op as no actual write is done

		tms->check_ready = false;
		tms->onchip_memory[tms->address & 0x00ff] = (tms->current_value >> 8) & 0xff;
		if (tms->word_access || !tms->byteop)
		{
			tms->onchip_memory[(tms->address & 0x00ff)+1] = tms->current_value & 0xff;
		}
		tms9995_pulse_clock(tms, 1);
	}
	else
	{
		// This is an off-chip access
		uint16_t address = tms->address;
		tms->check_ready = true;
		switch (tms->mem_phase)
		{
		case 1:
			// Set address
			// If this is a word access, 4 passes, else 2 passes
			if (tms->word_access || !tms->byteop)
			{
				tms->pass = 4;
				address &= 0xfffe;
			}
			else tms->pass = 2;

			tms->check_hold = false;
			if (tms->itrace)
				fprintf(stderr, "memory write byte %04x <- %02x\n", address, (tms->current_value >> 8)&0xff);
			tms9995_writeb(tms, address, (tms->current_value >> 8)&0xff);
			tms->request_auto_wait_state = tms->auto_wait;
			tms9995_pulse_clock(tms, 1);
			break;

		case 2:
			// no action here, just wait for READY
			break;
		case 3:
			// Set address + 1 (unless byte command)
			if (tms->itrace) fprintf(stderr, "memory write byte %04x <- %02x\n", tms->address | 1, tms->current_value & 0xff);
			tms9995_writeb(tms, tms->address | 1, tms->current_value & 0xff);
			tms->request_auto_wait_state = tms->auto_wait;
			tms9995_pulse_clock(tms, 1);
			break;
		case 4:
			// no action here, just wait for READY
			break;
		}

		tms->mem_phase = (tms->mem_phase % 4) +1;

		// Reset to 1 when we are done
		if (tms->pass==1)
		{
			tms->mem_phase = 1;
			tms->check_hold = true;
		}
	}
}

/*
    Write a word. This is independent of the byte flag of the instruction.
*/
static void tms9995_word_write(struct tms9995 *tms)
{
	tms->word_access = true;
	tms9995_mem_write(tms);
	tms->word_access = false;
}

/*
    Returns from the operand address derivation.
*/
static void tms9995_return_with_address(struct tms9995 *tms)
{
	// Return from operand address derivation
	// The result should be in tms->address
	tms->index = tms->caller_index;
	tms->MPC = tms->caller_MPC; // will be increased on return
	tms->address = tms->current_value + tms->address_add;
	if (tms->itrace) fprintf(stderr, "+++ return from operand address derivation +++\n");
	// no clock pulse
}

/*
    Returns from the operand address derivation, but using the saved address.
    This is required when we use the auto-increment feature.
*/
static void tms9995_return_with_address_copy(struct tms9995 *tms)
{
	// Return from operand address derivation
	tms->index = tms->caller_index;
	tms->MPC = tms->caller_MPC; // will be increased on return
	tms->address = tms->address_saved;
	if (tms->itrace) fprintf(stderr, "+++ return from operand address derivation (auto inc) +++\n");
	// no clock pulse
}

/*
    CRU support code
    See common explanations in tms9900.c

    The TMS9995 CRU address space is larger than the CRU space of the TMS9900:
    0000-fffe (even addresses) instead of 0000-1ffe. Unlike the TMS9900, the
    9995 uses the data bus lines D0-D2 to indicate external operations.

    Internal CRU locations (read/write)
    -----------------------------------
    1EE0 Flag 0     Decrementer as event counter
    1EE2 Flag 1     Decrementer enable
    1EE4 Flag 2     Level 1 interrupt present (read only, also set when interrupt mask disallows interrupts)
    1EE6 Flag 3     Level 3 interrupt present (see above)
    1EE8 Flag 4     Level 4 interrupt present (see above)
    ...
    1EFE Flag 15
    1FDA MID flag (only indication, does not trigger when set)

    The TMS9995 allows for wait states during external CRU access. Therefore
    we do iterations for each bit, checking every time for the READY line
    in the main loop.

        (write)
        tms->cru_output
        tms->cru_address
        tms->cru_value
        tms->count

*/

#define CRUREADMASK 0xfffe
#define CRUWRITEMASK 0xfffe

static void tms9995_cru_output_operation(struct tms9995 *tms)
{
	if (tms->itrace) fprintf(stderr, "CRU output operation, address %04x, value %d\n", tms->cru_address, tms->cru_value & 0x01);

	if (tms->cru_address == 0x1fda)
	{
		// [1], section 2.3.3.2.2: "setting the MID flag to one with a CRU instruction
		// will not cause the MID interrupt to be requested."
		tms->check_ready = false;
		tms->mid_flag = (tms->cru_value & 0x01);
	}
	else
	{
		if ((tms->cru_address & 0xffe0) == 0x1ee0)
		{
			tms->check_ready = false;
			// FLAG2, FLAG3, and FLAG4 are read-only
			if (tms->itrace) fprintf(stderr, "set CRU address %04x to %d\n", tms->cru_address, tms->cru_value&1);
			if ((tms->cru_address != 0x1ee4) && (tms->cru_address != 0x1ee6) && (tms->cru_address != 0x1ee8))
				tms->flag[(tms->cru_address>>1)&0x000f] = (tms->cru_value & 0x01);
		}
		else
		{
			// External access
			tms->check_ready = true;
		}
	}

	// All CRU write operations are visible to the outside world, even when we
	// have internal access. This makes it possible to assign special
	// functions to the internal flag bits which are realized outside
	// of the CPU. However, no wait states are generated for internal
	// accesses. ([1], section 2.3.3.2)

	tms9995_write_cru(tms, tms->cru_address & CRUWRITEMASK, (tms->cru_value & 0x01));
	tms->cru_value >>= 1;
	tms->cru_address = (tms->cru_address + 2) & 0xfffe;
	tms->count--;

	// Repeat this operation
	tms->pass = (tms->count > 0)? 2 : 1;
	tms9995_pulse_clock(tms, 2);
}

/*
    Input: (read)
        tms->cru_multi_first
        tms->cru_address
    Output:
        tms->cru_value (right-shifted; i.e. first bit is LSB of the 16 bit word,
        also for byte operations)
*/

static void tms9995_cru_input_operation(struct tms9995 *tms)
{
	bool crubit;

	if (tms->cru_first_read)
	{
		tms->cru_value = 0;
		tms->cru_first_read = false;
		tms->pass = tms->count;
	}

	// Read a single CRU bit
	/* CHECK FFFF or FFFE ? */
	crubit = tms9995_read_cru(tms, tms->cru_address & 0xFFFF);
	tms->cru_value = (tms->cru_value >> 1) & 0x7fff;

	// During internal reading, the CRUIN line will be ignored. We emulate this
	// by overwriting the bit which we got from outside. Also, READY is ignored.
	if (tms->cru_address == 0x1fda)
	{
		crubit = tms->mid_flag;
		tms->check_ready = false;
	}
	else
	{
		if ((tms->cru_address & 0xffe0)==0x1ee0)
		{
			crubit = tms->flag[(tms->cru_address>>1)&0x000f];
			tms->check_ready = false;
		}
		else
		{
			tms->check_ready = true;
		}
	}

	if (tms->itrace) fprintf(stderr, "CRU input operation, address %04x, value %d\n", tms->cru_address, crubit ? 1 : 0);

	if (crubit)
		tms->cru_value |= 0x8000;

	tms->cru_address = (tms->cru_address + 2) & 0xfffe;

	if (tms->pass == 1)
	{
		// This is the final shift. For both byte and word length transfers,
		// the first bit is always tms->cru_value & 0x0001.
		tms->cru_value >>= (16 - tms->count);
	}
	tms9995_pulse_clock(tms, 2);
}

/*
    Decrementer.
*/
static void tms9995_trigger_decrementer(struct tms9995 *tms)
{
	if (tms->starting_count_storage_register>0) // null will turn off the decrementer
	{
		tms->decrementer_value--;
		if (tms->itrace) fprintf(stderr, "dec=%04x\n", tms->decrementer_value);
		if (tms->decrementer_value==0)
		{
			tms->decrementer_value = tms->starting_count_storage_register;
			if (tms->flag[1]==true)
			{
				if (tms->itrace) fprintf(stderr, "decrementer flags interrupt\n");
				tms->flag[3] = true;
			}
		}
	}
}

/*
    This is a switch to a subprogram. In terms of cycles
    it does not take any time; execution continues with the first instruction
    of the subprogram.

    input:
        tms->get_destination
        tms->decoded[tms->instindex]
        tms->WP
        tms->current_value
        tms->address
    output:
        tms->source_value  = tms->current_value before invocation
        tms->current_value = tms->address
        tms->address_add   = 0
        tms->lowbyte       = false
        tms->get_destination = true
        tms->regnumber     = register number
        tms->address       = address of register
    */
static void tms9995_operand_address_subprogram(struct tms9995 *tms)
{
	uint16_t ircopy = tms->IR;
	if (tms->get_destination) ircopy = ircopy >> 6;

	// Save the return program and position
	tms->caller_index = tms->index;
	tms->caller_MPC = tms->MPC;

	tms->index = tms->operand_address_derivation_index;
	tms->MPC = (ircopy & 0x0030) >> 2;
	tms->regnumber = (ircopy & 0x000f);
	tms->address = (tms->WP + (tms->regnumber<<1)) & 0xffff;

	tms->source_value = tms->current_value;   // will be overwritten when reading the destination
	tms->current_value = tms->address;        // needed for first case

	if (tms->MPC==8) // Symbolic
	{
		if (tms->regnumber != 0)
		{
			if (tms->itrace) fprintf(stderr, "indexed addressing\n");
			tms->MPC = 16; // indexed
		}
		else
		{
			if (tms->itrace) fprintf(stderr, "symbolic addressing\n");
			tms->address = tms->PC;
			tms->PC = (tms->PC + 2) & 0xfffe;
		}
	}

	tms->get_destination = true;
	tms->mem_phase = 1;
	tms->address_add = 0;
	tms->MPC--;      // will be increased in the mail loop
	if (tms->itrace) fprintf(stderr, "*** Operand address derivation; address=%04x; index=%d\n", tms->address, tms->MPC+1);
}

/*
    Used for register auto-increment. We have to save the address read from the
    register content so that we can return it at the end.
*/
static void tms9995_increment_register(struct tms9995 *tms)
{
	tms->address_saved = tms->current_value;  // need a special return so we do not lose the value
	tms->current_value += tms->byteop? 1 : 2;
	tms->address = (tms->WP + (tms->regnumber<<1)) & 0xffff;
	tms->mem_phase = 1;
	tms9995_pulse_clock(tms, 1);
}

/*
    Used for indexed addressing. We store the contents of the index register
    in tms->address_add which is set to 0 by default. Then we set the address
    pointer to the PC location and advance it.
*/
static void tms9995_indexed_addressing(struct tms9995 *tms)
{
	tms->address_add = tms->current_value;
	tms->address = tms->PC;
	tms->PC = (tms->PC + 2) & 0xfffe;
	tms->mem_phase = 1;
	tms9995_pulse_clock(tms, 1);
}

static void tms9995_set_immediate(struct tms9995 *tms)
{
	// Need to determine the register address
	tms->address_saved = tms->WP + ((tms->IR & 0x000f)<<1);
	tms->address = tms->PC;
	tms->source_value = tms->current_value;       // needed for AI, ANDI, ORI
	tms->PC = (tms->PC + 2) & 0xfffe;
	tms->mem_phase = 1;
}

/**************************************************************************
    Status bit operations
**************************************************************************/

static void tms9995_set_status_bit(struct tms9995 *tms, int bit, bool state)
{
	if (state) tms->ST |= bit;
	else tms->ST &= ~bit;
	tms->int_overflow = (tms->check_overflow && bit == ST_OV  && ((tms->ST & ST_OE)!=0) && state == true);
}

static void tms9995_set_status_parity(struct tms9995 *tms, uint8_t value)
{
	int count = 0;
	for (int i=0; i < 8; i++)
	{
		if ((value & 0x80)!=0) count++;
		value <<= 1;
	}
	tms9995_set_status_bit(tms, ST_OP, (count & 1)!=0);
}

static void tms9995_compare_and_set_lae(struct tms9995 *tms, uint16_t value1, uint16_t value2)
{
	tms9995_set_status_bit(tms, ST_EQ, value1 == value2);
	tms9995_set_status_bit(tms, ST_LH, value1 > value2);
	tms9995_set_status_bit(tms, ST_AGT, (int16_t)value1 > (int16_t)value2);
}

/**************************************************************************
    ALU operations. The activities as implemented here are performed
    during the internal operations of the CPU, according to the current
    instruction.

    Some ALU operations are followed by the prefetch operation. In fact,
    this prefetch happens in parallel to the ALU operation. In these
    situations we do not pulse the clock here but leave this to the prefetch
    operation.
**************************************************************************/

static void tms9995_alu_nop(struct tms9995 *tms)
{
	// Do nothing (or nothing that is externally visible)
	tms9995_pulse_clock(tms, 1);
	return;
}

static void tms9995_alu_add_s_sxc(struct tms9995 *tms)
{
	// We have the source operand value in tms->source_value and the destination
	// value in tms->current_value
	// The destination address is still in tms->address
	// Prefetch will not change tms->current_value and tms->address

	uint32_t dest_new = 0;

	switch (tms->command)
	{
	case A:
	case AB:
		// When adding, a carry occurs when we exceed the 0xffff value.
		dest_new = tms->current_value + tms->source_value;
		tms9995_set_status_bit(tms, ST_C, (dest_new & 0x10000) != 0);

		// If the result has a sign bit that is different from both arguments, we have an overflow
		// (i.e. getting a negative value from two positive values and vice versa)
		tms9995_set_status_bit(tms, ST_OV, ((dest_new ^ tms->current_value) & (dest_new ^ tms->source_value) & 0x8000)!=0);
		break;
	case S:
	case SB:
		dest_new = tms->current_value + ((~tms->source_value) & 0xffff) + 1;
		// Subtraction means adding the 2s complement, so the carry bit
		// is set whenever adding the 2s complement exceeds ffff
		// In fact the CPU adds the one's complement, then adds a one. This
		// explains why subtracting 0 sets the carry bit.
		tms9995_set_status_bit(tms, ST_C, (dest_new & 0x10000) != 0);

		// If the arguments have different sign bits and the result has a
		// sign bit different from the destination value, we have an overflow
		// e.g. value1 = 0x7fff, value2 = 0xffff; value1-value2 = 0x8000
		// or   value1 = 0x8000, value2 = 0x0001; value1-value2 = 0x7fff
		// value1 is the destination value
		tms9995_set_status_bit(tms, ST_OV, (tms->current_value ^ tms->source_value) & (tms->current_value ^ dest_new) & 0x8000);
		break;
	case SOC:
	case SOCB:
		dest_new = tms->current_value | tms->source_value;
		break;

	case SZC:
	case SZCB:
		dest_new = tms->current_value & ~tms->source_value;
		break;
	}

	tms->current_value = (uint16_t)(dest_new & 0xffff);

	tms9995_compare_and_set_lae(tms, ((uint16_t)(dest_new & 0xffff)),0);
	if (tms->byteop)
	{
		tms9995_set_status_parity(tms, (uint8_t)(dest_new>>8));
	}
	if (tms->itrace) fprintf(stderr, "ST = %04x (val=%04x)\n", tms->ST, tms->current_value);
	// No clock pulse (will be done by prefetch)
}

/*
    Branch / Branch and link. We put the source address into the PC after
    copying the PC into tms->current_value. The address is R11. The B instruction
    will just ignore these settings, but BL will use them.
*/
static void tms9995_alu_b(struct tms9995 *tms)
{
	tms->current_value = tms->PC;
	tms->PC = tms->address & 0xfffe;
	tms->address = tms->WP + 22;
}

/*
    Branch and load workspace pointer. This is a branch to a subprogram with
    context switch.
*/
static void tms9995_alu_blwp(struct tms9995 *tms)
{
	int n = 1;
	switch (tms->inst_state)
	{
	case 0:
		// new WP in tms->current_value
		tms->value_copy = tms->WP;
		tms->WP = tms->current_value & 0xfffe;
		tms->address_saved = tms->address + 2;
		tms->address = tms->WP + 30;
		tms->current_value = tms->ST;
		break;
	case 1:
		tms->current_value = tms->PC;
		tms->address = tms->address - 2;
		break;
	case 2:
		tms->current_value = tms->value_copy;     // old WP
		tms->address = tms->address - 2;
		break;
	case 3:
		tms->address = tms->address_saved;
		break;
	case 4:
		tms->PC = tms->current_value & 0xfffe;
		n = 0;
		if (tms->itrace) fprintf(stderr, "Context switch (blwp): WP=%04x, PC=%04x, ST=%04x\n", tms->WP, tms->PC, tms->ST);
		break;
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, n);
}

/*
    Compare is similar to add, s, soc, szc, but we do not write a result.
*/
static void tms9995_alu_c(struct tms9995 *tms)
{
	// We have the source operand value in tms->source_value and the destination
	// value in tms->current_value
	// The destination address is still in tms->address
	// Prefetch will not change tms->current_value and tms->address
	if (tms->byteop)
	{
		tms9995_set_status_parity(tms, (uint8_t)(tms->source_value>>8));
	}
	tms9995_compare_and_set_lae(tms, tms->source_value, tms->current_value);
	if (tms->itrace) fprintf(stderr, "ST = %04x (val1=%04x, val2=%04x)\n", tms->ST, tms->source_value, tms->current_value);
}

/*
    Compare with immediate value.
*/
static void tms9995_alu_ci(struct tms9995 *tms)
{
	// We have the register value in tms->source_value, the register address in tms->address_saved
	// and the immediate value in tms->current_value
	tms9995_compare_and_set_lae(tms, tms->source_value, tms->current_value);
	if (tms->itrace) fprintf(stderr, "ST = %04x (val1=%04x, val2=%04x)\n", tms->ST, tms->source_value, tms->current_value);
}

static void tms9995_alu_clr_seto(struct tms9995 *tms)
{
	if (tms->itrace) fprintf(stderr, "clr/seto: Setting values for address %04x\n", tms->address);
	switch (tms->command)
	{
	case CLR:
		tms->current_value = 0;
		break;
	case SETO:
		tms->current_value = 0xffff;
		break;
	}
	// No clock pulse, as next instruction is prefetch
}

/*
    Unsigned division.
*/
static void tms9995_alu_divide(struct tms9995 *tms)
{
	int n=1;
	uint32_t uval32;

	bool overflow = true;
	uint16_t value1;

	switch (tms->inst_state)
	{
	case 0:
		tms->source_value = tms->current_value;
		// Set address of register
		tms->address = tms->WP + ((tms->IR >> 5) & 0x001e);
		tms->address_copy = tms->address;
		break;
	case 1:
		// Value of register is in tms->current_value
		// We have an overflow when the quotient cannot be stored in 16 bits
		// This is the case when the dividend / divisor >= 0x10000,
		// or equivalently, dividend / 0x10000 >= divisor

		// Check overflow for unsigned DIV
		if (tms->current_value < tms->source_value)   // also if source=0
		{
			tms->MPC++;  // skip the abort
			overflow = false;
		}
		tms9995_set_status_bit(tms, ST_OV, overflow);
		tms->value_copy = tms->current_value;         // Save the high word
		tms->address = tms->address + 2;
		break;
	case 2:
		// W2 is in tms->current_value
		uval32 = (tms->value_copy << 16) | tms->current_value;
		// Calculate
		// The number of ALU cycles depends on the number of steps in
		// the division algorithm. The number of cycles is between 1 and 16
		// As in TMS9900, this is a guess; it depends on the actual algorithm
		// used in the chip.

		tms->current_value = uval32 / tms->source_value;
		tms->value_copy = uval32 % tms->source_value;
		tms->address = tms->address_copy;

		value1 = tms->value_copy & 0xffff;
		while (value1 != 0)
		{
			value1 = (value1 >> 1) & 0xffff;
			n++;
		}

		break;
	case 3:
		// now write the remainder
		tms->current_value = tms->value_copy;
		tms->address = tms->address + 2;
		break;
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, n);
}

/*
    Signed Division
    We cannot handle this by the same ALU operation because we can NOT decide
    whether there is an overflow before we have retrieved the whole 32 bit
    word. Also, the overflow detection is pretty complicated for signed
    division when done before the actual calculation.
*/
static void tms9995_alu_divide_signed(struct tms9995 *tms)
{
	int n=1;
	bool overflow;
	uint16_t w1, w2, dwait;
	int16_t divisor;
	int32_t dividend;

	switch (tms->inst_state)
	{
	case 0:
		// Got the source value (divisor)
		tms->source_value = tms->current_value;
		tms->address = tms->WP;  // DIVS always uses R0,R1
		break;
	case 1:
		// Value of register is in tms->current_value
		tms->value_copy = tms->current_value;
		tms->address += 2;
		break;
	case 2:
		// Now we have the dividend low word in tms->current_value,
		// the dividend high word in tms->value_copy, and
		// the divisor in tms->source_value.

		w1 = tms->value_copy;
		w2 = tms->current_value;
		divisor = tms->source_value;
		dividend = w1 << 16 | w2;

		// Now check for overflow
		// We need to go for four cases
		// if the divisor is not 0 anyway
		if (divisor != 0)
		{
			if (dividend >= 0)
			{
				if (divisor > 0)
				{
					overflow = (dividend > ((divisor<<15) - 1));
				}
				else
				{
					overflow = (dividend > (((-divisor)<<15) + (-divisor) - 1));
				}
			}
			else
			{
				if (divisor > 0)
				{
					overflow = ((-dividend) > ((divisor<<15) + divisor - 1));
				}
				else
				{
					overflow = ((-dividend) > (((-divisor)<<15) - 1));
				}
			}
		}
		else
		{
			overflow = true; // divisor is 0
		}
		tms9995_set_status_bit(tms, ST_OV, overflow);
		if (!overflow) tms->MPC++;       // Skip the next microinstruction when there is no overflow
		break;
	case 3:
		// We are here because there was no overflow
		dividend = tms->value_copy << 16 | tms->current_value;
		// Do the calculation
		tms->current_value =  (uint16_t)(dividend / (int16_t)tms->source_value);
		tms->value_copy = (uint16_t)(dividend % (int16_t)tms->source_value);
		tms->address = tms->WP;

		// As we have not implemented the real division algorithm we must
		// simulate the number of steps required for calculating the result.
		// This is just a guess.
		dwait = tms->value_copy;
		while (dwait != 0)
		{
			dwait = (dwait >> 1) & 0xffff;
			n++;
		}
		// go write the quotient into R0
		break;
	case 4:
		// Now write the remainder
		tms->current_value = tms->value_copy;
		tms->address += 2;
		n = 0;
		break;
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, n);
}

/*
    External operations.
*/
static void tms9995_alu_external(struct tms9995 *tms)
{
	// Call some possibly attached external device
	// A specific bit pattern is put on the data bus, and the CRUOUT line is
	// pulsed. In our case we use a special callback function since we cannot
	// emulate this behavior in this implementation.

	// Opcodes         D012 value
	// -----------------vvv------
	// IDLE = 0000 0011 0100 0000
	// RSET = 0000 0011 0110 0000
	// CKON = 0000 0011 1010 0000
	// CKOF = 0000 0011 1100 0000
	// LREX = 0000 0011 1110 0000

	// Only IDLE has a visible effect on the CPU without external support: the
	// CPU will stop execution until an interrupt occurs. CKON, CKOF, LREX have
	// no effect without external support. Neither has RSET, it does *not*
	// cause a reset of the CPU or of the remaining computer system.
	// It only clears the interrupt mask and outputs the
	// external code on the data bus. A special line decoder could then trigger
	// a reset from outside.

	if (tms->command == IDLE)
	{
		if (tms->itrace)
			fprintf(stderr, "Entering IDLE state\n");
		tms->idle_state = true;
	}

	if (tms->command == RSET)
	{
		tms->ST &= 0xfff0;
		if (tms->itrace)
			fprintf(stderr, "RSET, new ST = %04x\n", tms->ST);
	}

#if 0
	if (!tms->external_operation.isnull(tms))
		tms->external_operation((tms->IR >> 5) & 0x07, 1, 0xff);
#endif
}

/*
    Logical compare and XOR
*/
static void tms9995_alu_f3(struct tms9995 *tms)
{
	switch (tms->inst_state)
	{
	case 0:
		// We have the contents of the source in tms->current_value and its address
		// in tms->address
		tms->source_value = tms->current_value;
		// Get register address
		tms->address = tms->WP + ((tms->IR >> 5) & 0x001e);
		break;
	case 1:
		// Register contents -> tms->current_value
		// Source contents -> tms->source_value
		if (tms->command == COC)
		{
			tms9995_set_status_bit(tms, ST_EQ, (tms->current_value & tms->source_value) == tms->source_value);
		}
		else
		{
			if (tms->command == CZC)
			{
				tms9995_set_status_bit(tms, ST_EQ, (~tms->current_value & tms->source_value) == tms->source_value);
			}
			else
			{
				// XOR
				// The workspace register address is still in tms->address
				tms->current_value = (tms->current_value ^ tms->source_value);
				tms9995_compare_and_set_lae(tms, tms->current_value, 0);
			}
		}
		if (tms->itrace) fprintf(stderr, "ST = %04x\n", tms->ST);
		break;
	}
	tms->inst_state++;
}

/*
    Handles AI, ANDI, ORI.
*/
static void tms9995_alu_imm_arithm(struct tms9995 *tms)
{
	uint32_t dest_new = 0;

	// We have the register value in tms->source_value, the register address in tms->address_saved
	// and the immediate value in tms->current_value
	switch (tms->command)
	{
	case AI:
		dest_new = tms->current_value + tms->source_value;
		tms9995_set_status_bit(tms, ST_C, (dest_new & 0x10000) != 0);

		// If the result has a sign bit that is different from both arguments, we have an overflow
		// (i.e. getting a negative value from two positive values and vice versa)
		tms9995_set_status_bit(tms, ST_OV, ((dest_new ^ tms->current_value) & (dest_new ^ tms->source_value) & 0x8000)!=0);
		break;
	case ANDI:
		dest_new = tms->current_value & tms->source_value;
		break;
	case ORI:
		dest_new = tms->current_value | tms->source_value;
		break;
	}

	tms->current_value = (uint16_t)(dest_new & 0xffff);
	tms9995_compare_and_set_lae(tms, tms->current_value, 0);
	tms->address = tms->address_saved;
	if (tms->itrace) fprintf(stderr, "ST = %04x (val=%04x)\n", tms->ST, tms->current_value);
}

/*
    Handles all jump instructions.
*/
static void tms9995_alu_jump(struct tms9995 *tms)
{
	bool cond = false;
	int8_t displacement = (tms->IR & 0xff);

	switch (tms->command)
	{
	case JMP:
		cond = true;
		break;
	case JLT:   // LAECOP == x00xxx
		cond = ((tms->ST & (ST_AGT | ST_EQ))==0);
		break;
	case JLE:   // LAECOP == 0xxxxx
		cond = ((tms->ST & ST_LH)==0);
		break;
	case JEQ:   // LAECOP == xx1xxx
		cond = ((tms->ST & ST_EQ)!=0);
		break;
	case JHE:   // LAECOP == 1x0xxx, 0x1xxx
		cond = ((tms->ST & (ST_LH | ST_EQ)) != 0);
		break;
	case JGT:   // LAECOP == x1xxxx
		cond = ((tms->ST & ST_AGT)!=0);
		break;
	case JNE:   // LAECOP == xx0xxx
		cond = ((tms->ST & ST_EQ)==0);
		break;
	case JNC:   // LAECOP == xxx0xx
		cond = ((tms->ST & ST_C)==0);
		break;
	case JOC:   // LAECOP == xxx1xx
		cond = ((tms->ST & ST_C)!=0);
		break;
	case JNO:   // LAECOP == xxxx0x
		cond = ((tms->ST & ST_OV)==0);
		break;
	case JL:    // LAECOP == 0x0xxx
		cond = ((tms->ST & (ST_LH | ST_EQ)) == 0);
		break;
	case JH:    // LAECOP == 1xxxxx
		cond = ((tms->ST & ST_LH)!=0);
		break;
	case JOP:   // LAECOP == xxxxx1
		cond = ((tms->ST & ST_OP)!=0);
		break;
	}

	if (!cond)
	{
		if (tms->itrace) fprintf(stderr, "Jump condition false\n");
	}
	else
	{
		if (tms->itrace) fprintf(stderr, "Jump condition true\n");
		tms->PC = (tms->PC + (displacement<<1)) & 0xfffe;
	}
}

/*
    Implements LDCR.
*/
static void tms9995_alu_ldcr(struct tms9995 *tms)
{
	switch (tms->inst_state)
	{
	case 0:
		tms->count = (tms->IR >> 6) & 0x000f;
		if (tms->count==0) tms->count = 16;
		tms->byteop = (tms->count<9);
		break;
	case 1:
		// We have read the byte or word into tms->current_value.
		tms9995_compare_and_set_lae(tms, tms->current_value, 0);
		if (tms->itrace) fprintf(stderr, "ST = %04x (val=%04x)\n", tms->ST, tms->current_value);

		// Parity is computed from the complete byte, even when less than
		// 8 bits are transferred (see [1]).
		if (tms->byteop)
		{
			tms->current_value = (tms->current_value>>8) & 0xff;
			tms9995_set_status_parity(tms, (uint8_t)tms->current_value);
		}
		tms->cru_value = tms->current_value;
		tms->address = tms->WP + 24;
		break;
	case 2:
		// Prepare CRU operation
		tms->cru_address = tms->current_value;
		break;
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, 1);
}

/*
    Implements LI. Almost everything has been done in the microprogram;
    this part is reached with tms->address_saved = register address,
    and tms->current_value = *tms->address;
*/
static void tms9995_alu_li(struct tms9995 *tms)
{
	// Retrieve the address of the register
	// The immediate value is still in tms->current_value
	tms->address = tms->address_saved;
	tms9995_compare_and_set_lae(tms, tms->current_value, 0);
	if (tms->itrace) fprintf(stderr, "tms->ST = %04x (val=%04x)\n", tms->ST, tms->current_value);
}

static void tms9995_alu_limi_lwpi(struct tms9995 *tms)
{
	// The immediate value is in tms->current_value
	if (tms->command == LIMI)
	{
		tms->ST = (tms->ST & 0xfff0) | (tms->current_value & 0x000f);
		if (tms->itrace) fprintf(stderr, "LIMI sets ST = %04x\n", tms->ST);
		tms9995_pulse_clock(tms, 1);     // needs one more than LWPI
	}
	else
	{
		tms->WP = tms->current_value & 0xfffe;
		if (tms->itrace) fprintf(stderr, "LWPI sets new WP = %04x\n", tms->WP);
	}
}

/*
    Load status and load workspace pointer. This is a TMS9995-specific
    operation.
*/
static void tms9995_alu_lst_lwp(struct tms9995 *tms)
{
	if (tms->command==LST)
	{
		tms->ST = tms->current_value;
		if (tms->itrace) fprintf(stderr, "new ST = %04x\n", tms->ST);
		tms9995_pulse_clock(tms, 1);
	}
	else
	{
		tms->WP = tms->current_value & 0xfffe;
		if (tms->itrace) fprintf(stderr, "new WP = %04x\n", tms->WP);
	}
}

/*
    The MOV operation on the TMS9995 is definitely more efficient than in the
    TMS9900. As we have only 8 data bus lines we can read or write bytes
    with only one cycle. The TMS9900 always has to read the memory word first
    in order to write back a complete word, also when doing byte operations.
*/
static void tms9995_alu_mov(struct tms9995 *tms)
{
	tms->current_value = tms->source_value;
	if (tms->byteop)
	{
		tms9995_set_status_parity(tms, (uint8_t)(tms->current_value>>8));
	}
	tms9995_compare_and_set_lae(tms, tms->current_value, 0);
	if (tms->itrace) fprintf(stderr, "ST = %04x (val=%04x)\n", tms->ST, tms->current_value);
	// No clock pulse, as next instruction is prefetch
}

/*
    Unsigned and signed multiplication
*/
static void tms9995_alu_multiply(struct tms9995 *tms)
{
	int n = 0;
	uint32_t result;
	int32_t results;

	if (tms->command==MPY)
	{
		switch (tms->inst_state)
		{
		case 0:
			// tms->current_value <- multiplier (source)
			tms->source_value = tms->current_value;
			// tms->address is the second multiplier (in a register)
			tms->address = ((tms->IR >> 5) & 0x001e) + tms->WP;
			n = 1;
			break;
		case 1:
			// tms->current_value <- register content
			result = (tms->source_value & 0x0000ffff) * (tms->current_value & 0x0000ffff);
			tms->current_value = (result >> 16) & 0xffff;
			tms->value_copy = result & 0xffff;
			// tms->address is still the register
			n = 17;
			break;
		case 2:
			tms->address += 2;
			tms->current_value = tms->value_copy;
			// now write the lower 16 bit.
			// If the register was R15, do not use R0 but continue writing after
			// R15's address
			break;
		}
	}
	else    // MPYS
	{
		switch (tms->inst_state)
		{
		case 0:
			// tms->current_value <- multiplier (source)
			tms->source_value = tms->current_value;
			// tms->address is the second multiplier (in R0)
			tms->address = tms->WP;
			n = 1;
			break;
		case 1:
			// tms->current_value <- register content
			results = ((int16_t)tms->source_value) * ((int16_t)tms->current_value);
			tms->current_value = (results >> 16) & 0xffff;
			tms->value_copy = results & 0xffff;
			// tms->address is still the register
			n = 16;
			break;
		case 2:
			tms->address += 2;
			tms->current_value = tms->value_copy;
			// now write the lower 16 bit.
			break;
		}
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, n);
}

static void tms9995_alu_rtwp(struct tms9995 *tms)
{
	switch (tms->inst_state)
	{
	case 0:
		tms->address = tms->WP + 30;        // R15
		tms9995_pulse_clock(tms, 1);
		break;
	case 1:
		tms->ST = tms->current_value;
		tms->address -= 2;             // R14
		break;
	case 2:
		tms->PC = tms->current_value & 0xfffe;
		tms->address -= 2;             // R13
		break;
	case 3:
		tms->WP = tms->current_value & 0xfffe;

		// Just for debugging purposes
		tms->log_interrupt = false;

		if (tms->itrace) fprintf(stderr, "Context switch (rtwp): WP=%04x, PC=%04x, ST=%04x\n", tms->WP, tms->PC, tms->ST);
		break;
	}
	tms->inst_state++;
}

static void tms9995_alu_sbo_sbz(struct tms9995 *tms)
{
	int8_t displacement;

	if (tms->inst_state==0)
	{
		tms->address = tms->WP + 24;
	}
	else
	{
		tms->cru_value = (tms->command==SBO)? 1 : 0;
		displacement = (int8_t)(tms->IR & 0xff);
		tms->cru_address = tms->current_value + (displacement<<1);
		tms->count = 1;
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, 1);
}

/*
    Perform the shift operation
*/
static void tms9995_alu_shift(struct tms9995 *tms)
{
	bool carry = false;
	bool overflow = false;
	uint16_t sign = 0;
	uint32_t value;
	int count;
	bool check_ov = false;

	switch (tms->inst_state)
	{
	case 0:
		// we have the value of the register in tms->current_value
		// Save it (we may have to read R0)
		tms->value_copy = tms->current_value;
		tms->address_saved = tms->address;
		tms->address = tms->WP;
		// store this in tms->current_value where the R0 value will be put
		tms->current_value = (tms->IR >> 4) & 0x000f;
		if (tms->current_value != 0)
		{
			// skip the next read operation
			tms->MPC++;
		}
		else
		{
			if (tms->itrace) fprintf(stderr, "Shift operation gets count from R0\n");
		}
		tms9995_pulse_clock(tms, 1);
		tms9995_pulse_clock(tms, 1);
		break;

	case 1:
		count = tms->current_value & 0x000f; // from the instruction or from R0
		if (count == 0) count = 16;

		value = tms->value_copy;

		// we are re-implementing the shift operations because we have to pulse
		// the clock at each single shift anyway.
		// Also, it is easier to implement the status bit setting.
		// Note that count is never 0
		if (tms->command == SRA) sign = value & 0x8000;

		for (int i=0; i < count; i++)
		{
			switch (tms->command)
			{
			case SRL:
			case SRA:
				carry = ((value & 1)!=0);
				value = (value >> 1) | sign;
				break;
			case SLA:
				carry = ((value & 0x8000)!=0);
				value <<= 1;
				check_ov = true;
				if (carry != ((value&0x8000)!=0)) overflow = true;
				break;
			case SRC:
				carry = ((value & 1)!=0);
				value = (value>>1) | (carry? 0x8000 : 0x0000);
				break;
			}
			tms9995_pulse_clock(tms, 1);
		}

		tms->current_value = value & 0xffff;
		tms9995_set_status_bit(tms, ST_C, carry);
		if (check_ov) tms9995_set_status_bit(tms, ST_OV, overflow); // only SLA
		tms9995_compare_and_set_lae(tms, tms->current_value, 0);
		tms->address = tms->address_saved;        // Register address
		if (tms->itrace) fprintf(stderr, "ST = %04x (val=%04x)\n", tms->ST, tms->current_value);
		break;
	}
	tms->inst_state++;
}

/*
    Handles ABS, DEC, DECT, INC, INCT, NEG, INV
*/
static void tms9995_alu_single_arithm(struct tms9995 *tms)
{
	uint32_t dest_new = 0;
	uint32_t src_val = tms->current_value & 0x0000ffff;
	uint16_t sign = 0;
	bool check_ov = true;
	bool check_c = true;

	switch (tms->command)
	{
	case ABS:
		// LAECO (from original word!)
		// O if >8000
		// C is always reset
		tms9995_set_status_bit(tms, ST_OV, tms->current_value == 0x8000);
		tms9995_set_status_bit(tms, ST_C, false);
		tms9995_compare_and_set_lae(tms, tms->current_value, 0);

		if ((tms->current_value & 0x8000)!=0)
		{
			dest_new = ((~src_val) & 0x0000ffff) + 1;
		}
		else
		{
			dest_new = src_val;
		}
		tms->current_value = dest_new & 0xffff;
		return;
	case DEC:
		// LAECO
		// Carry for result value != 0xffff
		// Overflow for result value == 0x7fff
		dest_new = src_val + 0xffff;
		sign = 0x8000;
		break;
	case DECT:
		// Carry for result value != 0xffff / 0xfffe
		// Overflow for result value = 0x7fff / 0x7ffe
		dest_new = src_val + 0xfffe;
		sign = 0x8000;
		break;
	case INC:
		// LAECO
		// Overflow for result value = 0x8000
		// Carry for result value = 0x0000
		dest_new = src_val + 1;
		break;
	case INCT:
		// LAECO
		// Overflow for result value = 0x8000 / 0x8001
		// Carry for result value = 0x0000 / 0x0001
		dest_new = src_val + 2;
		break;
	case INV:
		// LAE
		dest_new = ~src_val & 0xffff;
		check_ov = false;
		check_c = false;
		break;
	case NEG:
		// LAECO
		// Overflow occurs for value=0x8000
		// Carry occurs for value=0
		dest_new = ((~src_val) & 0x0000ffff) + 1;
		check_ov = false;
		tms9995_set_status_bit(tms, ST_OV, src_val == 0x8000);
		break;
	case SWPB:
		tms->current_value = ((tms->current_value << 8) | (tms->current_value >> 8)) & 0xffff;
		// I don't know what they are doing right now, but we lose a lot of cycles
		// according to the spec (which can indeed be proved on a real system)

		// Maybe this command is used as a forced wait between accesses to the
		// video system. Usually we have two byte writes to set an address in
		// the VDP, with a SWPB in between. Most software for the TI-99/4A using
		// the TMS9900 will run into trouble when executed on the TI-99/8 with
		// the much faster TMS9995. So the SWPB may be used to as an intentional
		// slowdown.

		// No status bits affected
		tms9995_pulse_clock(tms, 10);
		return;
	}

	if (check_ov) tms9995_set_status_bit(tms, ST_OV, ((src_val & 0x8000)==sign) && ((dest_new & 0x8000)!=sign));
	if (check_c) tms9995_set_status_bit(tms, ST_C, (dest_new & 0x10000) != 0);
	tms->current_value = dest_new & 0xffff;
	tms9995_compare_and_set_lae(tms, tms->current_value, 0);

	if (tms->itrace) fprintf(stderr, "ST = %04x (val=%04x)\n", tms->ST, tms->current_value);
	// No clock pulse, as next instruction is prefetch
}

/*
    Store CRU.
*/
static void tms9995_alu_stcr(struct tms9995 *tms)
{
	int n = 1;
	switch (tms->inst_state)
	{
	case 0:
		tms->count = (tms->IR >> 6) & 0x000f;
		if (tms->count == 0) tms->count = 16;
		tms->byteop = (tms->count < 9);
		break;
	case 1:
		tms->address_saved = tms->address;
		tms->address = tms->WP + 24;
		break;
	case 2:
		tms->cru_address = tms->current_value;
		tms->cru_first_read = true;
		break;
	case 3:
		// I don't know what is happening here, but it takes quite some time.
		// May be shift operations.
		tms->current_value = tms->cru_value;
		tms->address = tms->address_saved;
		tms9995_compare_and_set_lae(tms, tms->current_value, 0);
		n = 13;
		if (tms->byteop)
		{
			tms9995_set_status_parity(tms, (uint8_t)tms->current_value);
			tms->current_value <<= 8;
		}
		else n += 8;
		if (tms->itrace) fprintf(stderr, "ST = %04x (val=%04x)\n", tms->ST, tms->current_value);
		break;
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, n);
}


/*
    Store status and store workspace pointer. We need to determine the
    address of the register here.
*/
static void tms9995_alu_stst_stwp(struct tms9995 *tms)
{
	tms->address = tms->WP + ((tms->IR & 0x000f)<<1);
	tms->current_value = (tms->command==STST)? tms->ST : tms->WP;
}

/*
    Test CRU bit.
*/
static void tms9995_alu_tb(struct tms9995 *tms)
{
	int8_t displacement;

	switch (tms->inst_state)
	{
	case 0:
		tms->address = tms->WP + 24;
		tms9995_pulse_clock(tms, 1);
		break;
	case 1:
		displacement = (int8_t)(tms->IR & 0xff);
		tms->cru_address = tms->current_value + (displacement<<1);
		tms->cru_first_read = true;
		tms->count = 1;
		tms9995_pulse_clock(tms, 1);
		break;
	case 2:
		tms9995_set_status_bit(tms, ST_EQ, tms->cru_value!=0);
		if (tms->itrace) fprintf(stderr, "ST = %04x\n", tms->ST);
		break;
	}
	tms->inst_state++;
}

/*
    Execute. This operation is substituted after reading the word at the
    given address.
*/
static void tms9995_alu_x(struct tms9995 *tms)
{
	// We have the word in tms->current_value. This word must now be decoded
	// as if it has been acquired by the normal procedure.
	tms9995_decode(tms, tms->current_value);
	tms9995_pulse_clock(tms, 1);

	// Switch to the prefetched and decoded instruction
	tms9995_next_command(tms);
}

/*
    XOP operation.
*/
static void tms9995_alu_xop(struct tms9995 *tms)
{
	switch (tms->inst_state)
	{
	case 0:
		// we have the source address in tms->address
		tms->address_saved = tms->address;
		// Format is xxxx xxnn nnxx xxxx
		tms->address = 0x0040 + ((tms->IR & 0x03c0)>>4);
		tms9995_pulse_clock(tms, 1);
		break;
	case 1:
		// tms->current_value is new tms->WP
		tms->value_copy = tms->WP;  // store this for later
		tms->WP = tms->current_value & 0xfffe;
		tms->address = tms->WP + 0x0016; // Address of new R11
		tms->current_value = tms->address_saved;
		tms9995_pulse_clock(tms, 1);
		break;
	case 2:
		tms->address = tms->WP + 0x001e;
		tms->current_value = tms->ST;
		tms9995_pulse_clock(tms, 1);
		break;
	case 3:
		tms->address = tms->WP + 0x001c;
		tms->current_value = tms->PC;
		tms9995_pulse_clock(tms, 1);
		break;
	case 4:
		tms->address = tms->WP + 0x001a;
		tms->current_value = tms->value_copy;
		tms9995_pulse_clock(tms, 1);
		break;
	case 5:
		tms->address = 0x0042 + ((tms->IR & 0x03c0)>>4);
		tms9995_pulse_clock(tms, 1);
		break;
	case 6:
		tms->PC = tms->current_value & 0xfffe;
		tms9995_set_status_bit(tms, ST_X, true);
		if (tms->itrace) fprintf(stderr, "Context switch (xop): WP=%04x, PC=%04x, ST=%04x\n", tms->WP, tms->PC, tms->ST);
		break;
	}
	tms->inst_state++;
}

/*
    Handle an interrupt. The behavior as implemented here follows
    exactly the flowchart in [1]
*/
static void tms9995_alu_int(struct tms9995 *tms)
{
	int pulse = 1;

	switch (tms->inst_state)
	{
	case 0:
		tms->PC = (tms->PC - 2) & 0xfffe;
		tms->address_saved = tms->address;
		if (tms->itrace)
			fprintf(stderr, "interrupt service (0): Prepare to read vector\n");
		break;
	case 1:
		pulse = 2;                  // two cycles (with the one at the end)
		tms->source_value = tms->WP;        // old WP
		tms->WP = tms->current_value & 0xfffe;       // new WP
		tms->current_value = tms->ST;
		tms->address = (tms->WP + 30)&0xfffe;
		if (tms->itrace)
			fprintf(stderr, "interrupt service (1): Read new WP = %04x, save ST to %04x\n", tms->WP, tms->address);
		break;
	case 2:
		tms->address = (tms->WP + 28)&0xfffe;
		tms->current_value = tms->PC;
		if (tms->itrace)
			fprintf(stderr, "interrupt service (2): Save PC to %04x\n", tms->address);
		break;
	case 3:
		tms->address = (tms->WP + 26)&0xfffe;
		tms->current_value = tms->source_value;   // old WP
		if (tms->itrace)
			fprintf(stderr, "interrupt service (3): Save WP to %04x\n", tms->address);
		break;
	case 4:
		tms->address = (tms->address_saved + 2) & 0xfffe;
		if (tms->itrace)
			fprintf(stderr, "interrupt service (4): Read PC from %04x\n", tms->address);
		break;
	case 5:
		tms->PC = tms->current_value & 0xfffe;
		tms->ST = (tms->ST & 0xfe00) | tms->intmask;
		if (tms->itrace) fprintf(stderr, "Context switch (int): WP=%04x, PC=%04x, ST=%04x\n", tms->WP, tms->PC, tms->ST);

		if (((tms->int_pending & PENDING_MID)!=0) && tms->nmi_active)
		{
			if (tms->itrace)
				fprintf(stderr, "interrupt service (6): NMI active after context switch\n");
			tms->int_pending &= ~PENDING_MID;
			tms->address = 0xfffc;
			tms->intmask = 0;
			tms->MPC = 0;    // redo the interrupt service for the NMI
		}
		else
		{
			if (tms->from_reset)
			{
				if (tms->itrace)
					fprintf(stderr, "interrupt service (6): RESET completed\n");
				// We came from the RESET interrupt
				tms->from_reset = false;
				tms->ST &= 0x01ff;
				tms->mid_flag = false;
				tms->mid_active = false;
				// FLAG0 and FLAG1 are also set to zero after RESET ([1], sect. 2.3.1.2.2)
				for (int i=0; i < 5; i++)
					tms->flag[i] = false;
				tms->check_hold = true;
			}
		}
		pulse = 0;
		break;

		// If next instruction is MID opcode we will detect this in command_completed
	}
	tms->inst_state++;
	tms9995_pulse_clock(tms, pulse);
}

static const ophandler s_microoperation[45] =
{
	tms9995_int_prefetch_and_decode,
	tms9995_prefetch_and_decode,
	tms9995_mem_read,
	tms9995_mem_write,
	tms9995_word_read,
	tms9995_word_write,
	tms9995_operand_address_subprogram,
	tms9995_increment_register,
	tms9995_indexed_addressing,
	tms9995_set_immediate,
	tms9995_return_with_address,
	tms9995_return_with_address_copy,
	tms9995_cru_input_operation,
	tms9995_cru_output_operation,
	tms9995_abort_operation,
	tms9995_command_completed,

	tms9995_alu_nop,
	tms9995_alu_add_s_sxc,
	tms9995_alu_b,
	tms9995_alu_blwp,
	tms9995_alu_c,
	tms9995_alu_ci,
	tms9995_alu_clr_seto,
	tms9995_alu_divide,
	tms9995_alu_divide_signed,
	tms9995_alu_external,
	tms9995_alu_f3,
	tms9995_alu_imm_arithm,
	tms9995_alu_jump,
	tms9995_alu_ldcr,
	tms9995_alu_li,
	tms9995_alu_limi_lwpi,
	tms9995_alu_lst_lwp,
	tms9995_alu_mov,
	tms9995_alu_multiply,
	tms9995_alu_rtwp,
	tms9995_alu_sbo_sbz,
	tms9995_alu_shift,
	tms9995_alu_single_arithm,
	tms9995_alu_stcr,
	tms9995_alu_stst_stwp,
	tms9995_alu_tb,
	tms9995_alu_x,
	tms9995_alu_xop,
	tms9995_alu_int
};

/* Disassembler */

/* Op decode table */

static const char *opnames0[] = {
	"SZC %s,%d",
	"SZCB %s,%d",
	"S %s,%d",
	"SB %s,%d",
	"C %s,%d",
	"CB %s,%d",
	"A %s,%d",
	"AB %s,%d",
	"MOV %s,%d",
	"MOVB %s,%d",
	"SOC %s,%d",
	"SOCB %s,%d"
};

static const char *opnames1[] = {
	"COC %s,%W",
	"CZC %s,%W",
	"XOR %s,%W",
	"XOP %x,%s ",
	"LDCR %x,%s",
	"STCR %x,%s",
	"MPY %s,%W",
	"DIV %s,%W"
};

static const char *opnames2[] = {
	"JMP %8",
	"JLT %8",
	"JLE %8",
	"JEQ %8",
	"JHE %8",
	"JGT %8",
	"JNE %8",
	"JNC %8",
	"JOC %8",
	"JNO %8",
	"JL %8",
	"JH %8",
	"JOP %8",
	"SBO %8",
	"SBZ %8",
	"TB %8"
};

static const char *opnames8[] = {
	"SRA %w,%S",
	"SRL %w,%S",
	"SLA %w,%S",
	"SRC %w,%S"
};

static const char *opnames9[] = {
	"BLWP %s",
	"B %s",
	"X %s",
	"CLR %s",
	"NEG %s",
	"INV %s",
	"INC %s",
	"INCT %s",
	"DEC %s",
	"DECT %s",
	"BL %s",
	"SWPB %s",
	"SETO %s",
	"ABS %s",
	NULL,
	NULL
};

static const char *opnames10[] = {
	"LI %w,%i",
	"AI %w,%i",
	"ANDI %w,%i",
	"ORI %w,%i",
	"CI %w,%i",
	"STWP %w",
	"STST %w",
	"LWPI %i",
	"LIMI %i",
	NULL,
	"IDLE",	/* No arguments */
	"RSET",	/* No arguments */
	"RTWP",	/* No arguments */
	"CKON",	/* No arguments */
	"CKOF",	/* No arguments */
	"LREX"	/* No arguments */
};

static const char *opnames11[] = {
	NULL,
	NULL, //"BIND",
	"DIVS %s",
	"MPYS %s"
};

static const char *opnames12[] = {
	"LST %w",
	"LWP %w",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

struct opset {
	uint16_t min;
	uint16_t shift;
	const char **name;
};

struct opset ops[] = {
	{	0x4000, 12,	opnames0	},
	{	0x2000,	10,	opnames1	},
	{	0x1000, 8,	opnames2	},
	{	0x0E40, 6,	NULL		},
	{	0x0E00, 4,	NULL		},
	{	0x0C40, 6,	NULL		},
	{	0x0C10, 4,	NULL		},
	{	0x0C00, 0,	NULL		},
	{	0x0800, 8,	opnames8	},
	{	0x0400,	6,	opnames9	},
	{	0x0200,	5,	opnames10	},
	{	0x0100, 6,	opnames11	},
	{	0x0080,	4,	opnames12	},
	{	0,		}
};


static uint16_t idata[3];
static uint16_t iptr;

static struct opset *tms9995_get_op(uint16_t ip)
{
	struct opset *p = ops;
	while(p->min) {
		if (ip >= p->min)
			return p;
		p++;
	}
	return NULL;
}

static uint16_t next_word(void)
{
	return idata[iptr++];
}

static char *decode_addr(char *p, uint8_t bits)
{
	uint8_t v = bits & 0x0F;
	switch(bits & 0x30) {
	case 0x00:	/* R */
		p += sprintf(p, "R%d", v);
		break;
	case 0x10:
		p += sprintf(p, "*R%d", v);
		break;
	case 0x20:
		p += sprintf(p, "@%04X", next_word());
		if (v)
			p += sprintf(p, "(R%d)", v);
		break;
	case 0x30:
		p += sprintf(p, "*R%d+", v);
		break;
	}
	return p;
}

static char *decode_op(struct opset *op, uint16_t ip)
{
	static char buf[128];
	char *out = buf;
	uint16_t inst;
	const char *name = NULL;

	if (op) {
		inst = (ip - op->min) >> op->shift;
		if (op->name)
			name = op->name[inst];
	}

	if (name == NULL) {
		sprintf(buf, "ILLEGAL %04X", ip);
		return buf;
	}
	/* TODO; a first *name byte to say 'non inst must be zero' */
	while(*name) {
		if (*name != '%') {
			*out++ = *name++;
			continue;
		}
		switch(*++name) {
		case 'w':
			out += sprintf(out, "R%d", ip & 0x0F);
			break;
		case 'W':
			out += sprintf(out, "R%d", (ip >> 6) & 0x0F);
			break;
		case 's':
			out = decode_addr(out, ip);
			break;
		case 'd':
			out = decode_addr(out, ip >> 6);
			break;
		case 'a':
			out = decode_addr(out, ip >> 4);
			break;
		case 'x':
			out += sprintf(out, "%d", (ip >> 6) & 0x0F);
			break;
		case '8':
			out += sprintf(out, "%d", (int)(int8_t)ip);
			break;
		case 'S':
			out += sprintf(out, "%d", (ip >> 4) & 0x0F);
			break;
		case 'i':
			out += sprintf(out, "%04X", next_word());
			break;
		}
		name++;
	}
	*out = 0;
	return buf;
}

static uint16_t tms9995_debug_read(struct tms9995 *tms, uint16_t addr)
{
	if (is_onchip(tms, addr))
		return (tms->onchip_memory[addr & 0xFE] << 8) |
			tms->onchip_memory[(addr & 0xFE) | 1];
	return (tms9995_readb_debug(tms, addr & 0xFFFE) << 8) |
		tms9995_readb_debug(tms, addr | 1);
}


static void tms9995_disassemble(struct tms9995 *tms)
{
	struct opset *op;
	uint16_t ir;
	uint16_t addr = tms->PC_debug;
	unsigned int i;

	iptr = 0;
	idata[0] = tms9995_debug_read(tms, addr);
	idata[1] = tms9995_debug_read(tms, addr + 2);
	idata[2] = tms9995_debug_read(tms, addr + 4);

	ir = next_word();
	op = tms9995_get_op(ir);

	for (i = 0; i < 16; i++) {
		fprintf(stderr, "R%d %04x ", i, tms9995_read_workspace_register_debug(tms, i));
		if ((i & 3) == 3) {
			fprintf(stderr, "\n");
			if (i != 15)
				fprintf(stderr, "\t\t");
		} else
			fprintf(stderr, " | ");
	}
	fprintf(stderr, "%04X: %s\n\t\t", tms->PC_debug, decode_op(op, ir));
}
