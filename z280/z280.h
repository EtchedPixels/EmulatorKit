#pragma once

#ifndef __Z280_H__
#define __Z280_H__

#include "z80common.h"
#include "z80daisy.h"
#include "z280uart.h"

enum
{
	Z280_PC = 0x100000,
	Z280_SP,
	Z280_USP,
	Z280_SSP,
	Z280_AF,
	Z280_BC,
	Z280_DE,
	Z280_HL,
	Z280_IX,
	Z280_IY,
	Z280_A,
	Z280_B,
	Z280_C,
	Z280_D,
	Z280_E,
	Z280_H,
	Z280_L,
	Z280_AF2,
	Z280_BC2,
	Z280_DE2,
	Z280_HL2,
	Z280_R,
	Z280_I,
	Z280_IM,
	Z280_IFF2,
	Z280_HALT,
	Z280_DC0,
	Z280_DC1,
	Z280_DC2,
	Z280_DC3,
	Z280_CR_MSR,

	Z280_GENPC = STATE_GENPC,
	Z280_GENSP = STATE_GENSP,
	Z280_GENPCBASE = STATE_GENPCBASE,
};

enum
{
	Z280_TABLE_op,
	Z280_TABLE_cb,
	Z280_TABLE_ed,
	Z280_TABLE_xy,
	Z280_TABLE_xycb,
	Z280_TABLE_dded,
	Z280_TABLE_fded,
	Z280_TABLE_ex    /* cycles counts for taken jr/jp/call and interrupt latency (rst opcodes) */
};

/*enum
{
	CPUINFO_PTR_Z280_CYCLE_TABLE = CPUINFO_PTR_CPU_SPECIFIC,
	CPUINFO_PTR_Z280_CYCLE_TABLE_LAST = CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_ex
};*/

/* traps */
#define Z280_TRAP_EPUM  0           /* EPUM trap */
#define Z280_TRAP_MEPU  1           /* MEPU trap */
#define Z280_TRAP_EPUF  2           /* EPUF trap */
#define Z280_TRAP_EPUI  3           /* EPUI trap */
#define Z280_TRAP_PRIV  4           /* Privileged instruction trap */
#define Z280_TRAP_SC    5           /* System call trap */
#define Z280_TRAP_ACCV  6           /* Access violation trap */
#define Z280_TRAP_SSO   7           /* System stack overflow trap */
#define Z280_TRAP_DIV   8           /* Division trap */
#define Z280_TRAP_SS    9           /* Single step trap */
#define Z280_TRAP_BP    10          /* Breakpoint trap */

#define Z280_TRAPSAVE_PREPC   1
#define Z280_TRAPSAVE_ARG16   2
#define Z280_TRAPSAVE_EPU     4
#define Z280_TRAPSAVE_EA      8

#define Z280_ABORT_ACCV   0
#define Z280_ABORT_FATAL  1

/* interrupt priorities */
#define Z280_INT_NMI    0           /* NMI */
#define Z280_INT_IRQ0   1           /* Group0: external IRQ0 */
#define Z280_INT_CTR0   2           /* Group1: internal CTR0 */
#define Z280_INT_DMA0   3           /* Group1: internal DMA0 */
#define Z280_INT_IRQ1   4           /* Group2: external IRQ1 */
#define Z280_INT_CTR1   5           /* Group3: internal CTR1 */
#define Z280_INT_UARTRX 6           /* Group3: internal UART RX */
#define Z280_INT_DMA1   7           /* Group3: internal DMA1 */
#define Z280_INT_IRQ2   8           /* Group4: external IRQ2 */
#define Z280_INT_UARTTX 9           /* Group5: internal UART TX */
#define Z280_INT_DMA2   10          /* Group5: internal DMA2 */
#define Z280_INT_CTR2   11          /* Group6: internal CTR2 */
#define Z280_INT_DMA3   12          /* Group6: internal DMA3 */
#define Z280_INT_MAX    Z280_INT_DMA3


/* Control registers p.5-77 */
#define Z280_CRSIZE  0x18
#define Z280_MSR   0     // Master Status reg (word) p.3-4
#define Z280_ISR   0x16  // Interrupt Status reg (word) p.3-4,3-5
#define Z280_IVTP  6     // Interrupt/Trap Vector Table Pointer (word) p.3-5,6-11
#define Z280_IOP   8     // I/O Page reg (byte) p.3-5
#define Z280_BTI   0xFF  // Bus Timing and Initialization reg. (byte) p.3-1,3-2
#define Z280_BTC   2     // Bus Timing and Control reg. (byte) p.3-2
#define Z280_SSLR  4     // System Stack Limit reg. (word) p.3-6 
#define Z280_TCR   0x10  // Trap Control reg. (byte) p.3-5,3-6
#define Z280_CCR   0x12  // Cache Control reg. (byte) p.3-3,3-4 
#define Z280_LAR   0x14  // Local Address reg. (byte) p.3-3

/* Internal IO ranges p.489*/
/**********************/
// UART
#define Z280_UARTIOP   0xFE	// UART IO page
#define Z280_UARTMASK  0xF0
#define Z280_UARTBASE  0x10	// base within IO page
#define Z280_UARTRSIZE 16	// IO range size
#define Z280_UARTCR	   0    // UART Configuration Reg. p.9-18
#define Z280_TCSR      2    // Transmitter Control/Status Reg. p.9-19
#define Z280_RCSR      4    // Receiver Control/Status Reg. p.9-20
#define Z280_RDR       6    // Receive Data Reg. p.9-18,9-21
#define Z280_TDR       8    // Transmit Data Reg. p.9-17,9-21

// Counter Timers
#define Z280_CTIOP     0xFE	// Counter/Timer IO page
#define Z280_CTMASK    0xE0
#define Z280_CTBASE    0xE0	// base within IO page
#define Z280_CTRSIZE   32	// IO range size
#define Z280_CTUSIZE   8	// IO range size of each unit
#define Z280_CTCR      0	// Counter/Timer Configuration Reg. p.9-5
#define Z280_CTCSR     1	// Counter/Timer Command/Status Reg. p.9-6
#define Z280_CTTCR     2	// Counter/Timer Time Constant Reg. p.9-6
#define Z280_CTCTR     3	// Counter/Timer Count-Time Reg. p.9-6

// DMA
#define Z280_DMAIOP    0xFF	// DMA IO page
#define Z280_DMAMASK   0xE0
#define Z280_DMABASE   0x00	// base within IO page
#define Z280_DMARSIZE  32	// IO range size
#define Z280_DMAUSIZE  8	// IO range size of each unit
#define Z280_DAL       0    // Destination Address Low p.9-14,9-15
#define Z280_DAH       1    // Destination Address High
#define Z280_SAL       2	// Source Address Low p.9-14,9-15
#define Z280_SAH       3	// Source Address High
#define Z280_DMACNT    4	// DMA Count reg. p.9-14
#define Z280_DMATDR    5	// Transaction Descriptor reg p.9-13,9-14
#define Z280_DMAMCR	   0x1F	// DMA Master Control Register p.9-13,9-14

// MMU
#define Z280_MMUIOP    0xFF // MMU IO page
#define Z280_MMUMASK   0xF0
#define Z280_MMUBASE   0xF0	// base within IO page
#define Z280_MMURSIZE  16	// IO range size
#define Z280_MMUMCR    0    // MMU Master Control Reg. p.7-5
#define Z280_PDRP      1    // Page Descriptor Reg. Pointer p.7-2,7-5
#define Z280_DSP       5    // Descriptor Select Port p.7-6
#define Z280_BMP       4    // Block Move Port p.7-6
#define Z280_IP        2    // Invalidation Port p.7-6

// Refresh
#define Z280_RRRIOP    0xFF // Refresh Rate IO page
#define Z280_RRRMASK   0xFF
#define Z280_RRR       0xE8	// Refresh Rate Register p.9-1

#define Z280_TYPE_Z280	0

#ifdef UNUSED_DEFINITION
/* MMU mapped memory lookup */
extern UINT8 z280_readmem(device_t *device, offs_t offset);
extern void z280_writemem(device_t *device, offs_t offset, UINT8 data);
#endif

struct z280_device {
	char *m_tag;
	UINT32 m_type;
	UINT32 m_clock;
	void *m_token;
	char *z280uart_tag;
	struct z280uart_device *z280uart;
	init_byte_callback bti_init_cb;
	int m_bus16; /* OPT pin */
	UINT32 m_ctin0, m_ctin1, m_ctin2;
	UINT16 ctin1_brg_const, ctin1_uart_timer;
};

//void cpu_get_info_z280(device_t *device, UINT32 state, cpuinfo *info);
//extern const device_type Z280;

/*-------------------------------------------------
    debugger_instruction_hook - CPU cores call
    this once per instruction from CPU cores
-------------------------------------------------*/

void z280_debug(device_t *device, offs_t curpc);
offs_t cpu_disassemble_z280(device_t *device, char *buffer, offs_t pc, const UINT8 *opram, int options);

struct z280_device *cpu_create_z280(char *tag, UINT32 type, UINT32 clock, 
    struct address_space *ram,
	struct address_space *iospace, device_irq_acknowledge_callback irqcallback, struct z80daisy_interface *daisy_init,
	init_byte_callback bti_init_cb, /* init BTI by AD0-AD7 on reset */
	int bus16, /* OPT pin 8 or 16bit bus */
	UINT32 ctin0, UINT32 ctin1, UINT32 ctin2, /* CTINx clocks (optional) */
	rx_callback_t z280uart_rx_cb,tx_callback_t z280uart_tx_cb);
void cpu_reset_z280(device_t *device);
void cpu_execute_z280(device_t *device, int icount);
int cpu_translate_z280(device_t *device, enum address_spacenum space, int intention, offs_t *address);

void z280_set_irq_line(device_t *device, int irqline, int state);
void z280_set_rdy_line(device_t *device, int rdyline, int state);
                                                 
offs_t cpu_get_state_z280(device_t *device,int device_state_entry);
void cpu_string_export_z280(device_t *device, int device_state_entry, char *string);

#endif /* __Z280_H__ */
