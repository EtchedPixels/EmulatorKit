// license:BSD-3-Clause
// copyright-holders:Michael Zapf
/*
  tms9995.h

  See tms9995.cpp for documentation
  Also see tms9900.h for types of TMS99xx processors.
*/

#ifndef MAME_CPU_TMS9995_TMS9995_H
#define MAME_CPU_TMS9995_TMS9995_H

enum
{
	INT_9995_RESET = 0,
	INT_9995_INTREQ = 1,
	INT_9995_INT1 = 2,
	INT_9995_INT4 = 3
};

#define INPUT_LINE_NMI	255

struct tms9995;

// Method pointer
typedef void (*ophandler)(struct tms9995 *);

// Sequence of micro-operations
typedef const uint8_t* microprogram;

// Opcode list entry
typedef struct tms9995_instruction
{
	uint16_t              opcode;
	int                 id;
	int                 format;
	microprogram        prog;       // Microprogram
} tms9995_instruction;

struct tms9995 {
	bool    mp9537;

	// State / debug management
	uint16_t  state_any;

	// TMS9995 hardware registers
	uint16_t  WP;     // Workspace pointer
	uint16_t  PC;     // Program counter
	uint16_t  ST;     // Status register

	// The TMS9995 has a prefetch feature which causes a wrong display of the PC.
	// We use this additional member for the debugger only.
	uint16_t  PC_debug;

	// Indicates the instruction acquisition phase
	bool    iaq;

	// 256 bytes of onchip memory
	uint8_t   onchip_memory[256];

	// Processor states
	bool    idle_state;
	bool    nmi_state;
	bool    hold_state;
	bool    hold_requested;

	// READY handling. The READY line is operated before the clock
	// pulse falls. As the ready line is only set once in this emulation we
	// keep the level in a buffer (like a latch)
	bool    ready_bufd;   // buffered state
	bool    ready;        // sampled value

	// Auto-wait state generation
	bool    request_auto_wait_state;
	bool    auto_wait;

	// Cycle counter
	int     icount;

	// Phase of the memory access
	int     mem_phase;

	// Check the READY line?
	bool    check_ready;

	// Check the HOLD line
	bool    check_hold;

	// For multi-pass operations. For instance, memory word accesses are
	// executed as two consecutive byte accesses. CRU accesses are repeated
	// single-bit accesses.
	int     pass;

	// For Format 1 instruction; determines whether the next operand address
	// derivation is for the source or address operand
	bool    get_destination;

	// Used for situations when a command is byte-oriented, but the memory access
	// must be word-oriented. Example: MOVB *R1,R0; we must read the full word
	// from R1 to get the address.
	bool    word_access;

	// Interrupt handling
	bool    nmi_active;
	bool    int1_active;
	bool    int4_active;
	bool    int_overflow;

	bool    reset;
	bool    from_reset;
	bool    mid_flag;
	bool    mid_active;

	int     decrementer_clkdiv;
	bool    log_interrupt;

	// Flag field
	int     int_pending;

	// The TMS9995 is capable of raising an internal interrupt on
	// arithmetic overflow, depending on the status register Overflow Enable bit.
	// However, the specs also say that this feature is non-functional in the
	// currently available chip. Thus we have an option to turn it off so that
	// software will not change its behavior on overflows.
	bool    check_overflow;

	// Store the interrupt mask part of the ST. This is used when processing
	// an interrupt, passing the new mask from the service_interrupt part to
	// the program part.
	int     intmask;

	// Stored address
	uint16_t  address;

	// Stores the recently read word or the word to be written
	uint16_t  current_value;

	// Stores the value of the source operand in multi-operand instructions
	uint16_t  source_value;

	// During indexed addressing, this value is added to get the final address value.
	uint16_t  address_add;

	// During indirect/auto-increment addressing, this copy of the address must
	// be preserved while writing the new value to the register.
	uint16_t  address_saved;

	// Another copy of the address
	uint16_t  address_copy;

	// Copy of the value
	uint16_t  value_copy;

	// Stores the recent register number. Only used to pass the register
	// number during the operand address derivation.
	int     regnumber;

	// Stores the number of bits or shift operations
	int     count;

	// ============== Decrementer =======================
	// Start value
	uint16_t  starting_count_storage_register;

	// Current decrementer value.
	uint16_t  decrementer_value;

	// ============== CRU support ======================

	uint16_t  cru_address;
	uint16_t  cru_value;
	bool    cru_first_read;

	// CPU-internal CRU flags
	bool    flag[16];

	// ============== Prefetch support =====================

	// We implement the prefetch mechanism by two separate datasets for
	// the decoded commands. When the next instruction shall be started,
	// the contents from the pre* members are copied to the main members.

	uint16_t  IR;
	uint16_t  command;
	int     index;
	bool    byteop;

	uint16_t  pre_IR;
	uint16_t  pre_command;
	int     pre_index;
	bool    pre_byteop;

	// State of the currently executed instruction
	int     inst_state;

	// ================ Microprogram support ========================

#if 0
	// Lookup table entry
	struct lookup_entry
	{
		std::unique_ptr<lookup_entry[]> next_digit;
		int index; // pointing to the static instruction list
	};

	// Pointer to the lookup table; the entry point for searching the command
	std::unique_ptr<lookup_entry[]>   m_command_lookup_table;
#endif

	// Index of the interrupt program
	int     interrupt_mp_index;

	// Index of the operand address derivation subprogram
	int     operand_address_derivation_index;

	// Micro-operation program counter (as opposed to the program counter PC)
	int     MPC;

	// Calling microprogram (used when data derivation is called)
	int     caller_index;
	int     caller_MPC;

	// Table of microprograms
	const microprogram *mp_table;

	// Used to display the number of consumed cycles in the log.
	int     first_cycle;

	// Tracing
	bool	trace;
	bool	itrace;
};

/* The decode tables. Each node has 16 entries which can point to a subtable
   or give the decode entry for the instruction. A simple binary search might
   be better than all the shifting and masking this produces - FIXME */
struct tms_decode {
	struct tms_decode *next_digit[16];
	int index[16];
};

extern void tms9995_set_hold_state(struct tms9995 *tms, bool state);
extern void tms9995_ready_line(struct tms9995 *tms, bool state);
extern void tms9995_hold_line(struct tms9995 *tms, bool state);
extern void tms9995_reset_line(struct tms9995 *tms, bool state);

extern uint8_t tms9995_readb(struct tms9995 *tms, uint16_t addr);
extern uint8_t tms9995_readb_debug(struct tms9995 *tms, uint16_t addr);
extern void tms9995_writeb(struct tms9995 *tms, uint16_t addr, uint8_t val);

extern uint8_t tms9995_read_cru(struct tms9995 *tms, uint16_t addr);
extern void tms9995_write_cru(struct tms9995 *tms, uint16_t addr, uint8_t val);

extern void tms9995_device_start(struct tms9995 *tms);
extern struct tms9995 *tms9995_create(bool is_mp9537, bool bstep);
extern void tms9995_trace(struct tms9995 *tms, bool onoff);

extern void tms9995_execute_run(struct tms9995 *tms, unsigned int cycles);
extern void tms9995_execute_set_input(struct tms9995 *tms, int irqline, bool state);

#endif // MAME_CPU_TMS9995_TMS9995_H
