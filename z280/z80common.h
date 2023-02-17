// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Aaron Giles
// copyright-holders:Olivier Galibert
// copyright-holders:Carl, Miodrag Milanovic, Vas Crabb
/*****************************************************************************
 *
 *   z80common.h
 *
 *****************************************************************************/

#pragma once

#ifndef __Z80COMMON_H__
#define __Z80COMMON_H__

extern int VERBOSE;
#include <stdio.h>
#define logerror printf
#define LOG(...) do { if (VERBOSE) logerror (__VA_ARGS__); } while (0)

#define ATTR_UNUSED /**/

#include <stdint.h>
#include <assert.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int8_t INT8;
typedef int32_t INT32;
typedef UINT32 offs_t;
typedef void device_t;
typedef UINT16 emu_timer;

#define LSB_FIRST
union PAIR
{
#ifdef LSB_FIRST
	struct { uint8_t l,h,h2,h3; } b;
	struct { uint16_t l,h; } w;
	struct { int8_t l,h,h2,h3; } sb;
	struct { int16_t l,h; } sw;
#else
	struct { uint8_t h3,h2,h,l; } b;
	struct { int8_t h3,h2,h,l; } sb;
	struct { uint16_t h,l; } w;
	struct { int16_t h,l; } sw;
#endif
	uint32_t d;
	int32_t sd;
};

#define INLINE /**/

#ifndef TRUE
#define TRUE                1
#endif

#ifndef FALSE
#define FALSE               0
#endif

#define BIT(x,n) (((x)>>(n))&1)

// standard state indexes
enum
{
	STATE_GENPC = -1,               // generic program counter (live)
	STATE_GENPCBASE = -2,           // generic program counter (base of current instruction)
	STATE_GENSP = -3,               // generic stack pointer
	STATE_GENFLAGS = -4             // generic flags
};

enum line_state
{
	CLEAR_LINE = 0,             // clear (a fired or held) line
	ASSERT_LINE,                // assert an interrupt immediately
	HOLD_LINE,                  // hold interrupt line until acknowledged
	PULSE_LINE                  // pulse interrupt line instantaneously (only for NMI, RESET)
};

// I/O line definitions
enum
{
	// input lines
	MAX_INPUT_LINES = 32+3,
	INPUT_LINE_IRQ0 = 0,
	INPUT_LINE_IRQ1 = 1,
	INPUT_LINE_IRQ2 = 2,
	INPUT_LINE_IRQ3 = 3,
	INPUT_LINE_IRQ4 = 4,
	INPUT_LINE_IRQ5 = 5,
	INPUT_LINE_IRQ6 = 6,
	INPUT_LINE_IRQ7 = 7,
	INPUT_LINE_IRQ8 = 8,
	INPUT_LINE_IRQ9 = 9,
	INPUT_LINE_NMI = MAX_INPUT_LINES - 3,

	// special input lines that are implemented in the core
	INPUT_LINE_RESET = MAX_INPUT_LINES - 2,
	INPUT_LINE_HALT = MAX_INPUT_LINES - 1
};

#undef PARITY_NONE
#undef PARITY_ODD
#undef PARITY_EVEN
#undef PARITY_MARK
#undef PARITY_SPACE

	enum parity_t
	{
		PARITY_NONE,     /* no parity. a parity bit will not be in the transmitted/received data */
		PARITY_ODD,      /* odd parity */
		PARITY_EVEN,     /* even parity */
		PARITY_MARK,     /* one parity */
		PARITY_SPACE     /* zero parity */
	};

	enum stop_bits_t
	{
		STOP_BITS_0,
		STOP_BITS_1 = 1,
		STOP_BITS_1_5 = 2,
		STOP_BITS_2 = 3
	};
 
// address spaces
enum address_spacenum
{
	AS_0,                           // first address space
	AS_1,                           // second address space
	AS_2,                           // third address space
	AS_3,                           // fourth address space
	ADDRESS_SPACES,                 // maximum number of address spaces

	// alternate address space names for common use
	AS_PROGRAM = AS_0,              // program address space
	AS_DATA = AS_1,                 // data address space
	AS_IO = AS_2                    // I/O address space
};

// Disassembler constants for the return value
#define DASMFLAG_SUPPORTED     0x80000000   // are disassembly flags supported?
#define DASMFLAG_STEP_OUT      0x40000000   // this instruction should be the end of a step out sequence
#define DASMFLAG_STEP_OVER     0x20000000   // this instruction should be stepped over by setting a breakpoint afterwards
#define DASMFLAG_OVERINSTMASK  0x18000000   // number of extra instructions to skip when stepping over
#define DASMFLAG_OVERINSTSHIFT 27           // bits to shift after masking to get the value
#define DASMFLAG_LENGTHMASK    0x0000ffff   // the low 16-bits contain the actual length

typedef int (*device_irq_acknowledge_callback)(device_t *device, int irqnum);
typedef void (*devcb_write_line)(device_t *device, int state);
typedef UINT8 (*init_byte_callback)(device_t *device);

// struct with function pointers for accessors; use is generally discouraged unless necessary
struct address_space
{
	// accessor methods for reading data
	UINT8       (*read_byte)(offs_t byteaddress);
	UINT16      (*read_word)(offs_t byteaddress);
	void        (*write_byte)(offs_t byteaddress, UINT8 data);
	void        (*write_word)(offs_t byteaddress, UINT16 data);

	// accessor methods for reading raw data (opcodes)
	UINT8 (*read_raw_byte)(offs_t byteaddress/*, offs_t directxor = 0*/);
	UINT16 (*read_raw_word)(offs_t byteaddress/*, offs_t directxor = 0*/);
};

#endif
