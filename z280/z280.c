/*****************************************************************************
 *
 *   z280.c
 *   Portable Z280 emulator
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License
 *     as published by the Free Software Foundation; either version 2
 *     of the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 *****************************************************************************/

/*   Copyright Juergen Buchmueller, all rights reserved. */
/*   Copyright Michal Tomek 2021 z280emu */

/*****************************************************************************

Z280 Info:

Known clock speeds (from ZiLOG): 10, 12.5 MHz

ZiLOG Z280 codes:

  Speed: 10 = 10MHz
         12 = 12.5MHz
Package: V = 68-Pin PLCC
   Temp: S = 0C to +70C
         E = -40C to +85C
Environmental Flow: C = Plastic Standard


Example:

   CPU is 8028012VSC = Z280, 12.5MHz, 68-Pin PLCC, 0C to +70C, Plastic Standard


 *****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>

//#include "emu.h"
//#include "debugger.h"
#include "z280.h"

/****************************************************************************/
/* The Z280 registers. HALT is set to 1 when the CPU is halted              */
/****************************************************************************/

struct z280_state
{
	union PAIR    PREPC,PC,SSP,USP,AF,BC,DE,HL,IX,IY;
	union PAIR    AF2,BC2,DE2,HL2;
	UINT8   AF2inuse, BC2inuse;
	UINT8   R;								/* R is not refresh counter but GPR p.488 */
	UINT8   IFF2,HALT,IM,I;					/* IFF2 name kept in accordance with Z80/Z180. Note that 
											   p.5-118 calls this IFF and p.6-3 Interrupt Shadow Reg.*/
	UINT8   cr[Z280_CRSIZE];                /* control registers */
	UINT8   rrr;                            /* refresh rate register */

	UINT8   mmur[4];                        /* MMU registers */
	UINT16  pdr[32];						/* Page descriptor registers; user, system */

	UINT8   ctcr[3];			            /* counter timer registers */
	UINT8   ctcsr[3];
	UINT16  ctctr[3];
	UINT16  cttcr[3];

	UINT32  sar[4];                         /* DMA registers */
	UINT32  dar[4];
	UINT16  dmatdr[4];
	UINT16  dmacnt[4];
	UINT8   dmamcr;
	UINT8   dma_pending[4];                 /* DMA service pending */
	int     dma_active;
	UINT8   rdy_state[4];                   /* RDY line states */

	UINT8   nmi_state;                      /* nmi line state */
	UINT8   nmi_pending;                    /* nmi pending */
	UINT8   irq_state[3];                   /* irq line states (INT0,INT1,INT2) */
	UINT8   int_pending[Z280_INT_MAX + 1];  /* interrupt pending */
	UINT8   after_EI;                       /* are we in the EI shadow? */
	UINT32  ea;                             /* effective address */
	int     eapdr;                          /* PDR used to calculate ea */
	UINT16  timer_cnt;
	struct z80_daisy_chain *daisy;	/* daisy chain */
	device_irq_acknowledge_callback irq_callback;
	struct z280_device *device;
	struct address_space *ram;
	struct address_space *iospace;
	int icount;
	int extra_cycles;           /* extra cpu cycles */
	UINT8 *cc[8];	/* cycle count tables */
	jmp_buf abort_handler;
	UINT8 abort_type;                       /* which abort will be taken upon ACCV */
};

INLINE struct z280_state *get_safe_token(device_t *device)
{
	assert(device != NULL);
	//assert(device->type() == Z280);
	return (struct z280_state *)((struct z280_device *)device)->m_token;
}

/* z80 FLAGS */
#define CF  0x01
#define NF  0x02
#define PF  0x04
#define VF  PF
#define XF  0x08
#define HF  0x10
#define YF  0x20
#define ZF  0x40
#define SF  0x80

/*
 * Prevent warnings on NetBSD.  All identifiers beginning with an underscore
 * followed by an uppercase letter are reserved by the C standard (ISO/IEC
 * 9899:1999, 7.1.3) to be used by the implementation.  It'd be best to rename
 * all such instances, but this is less intrusive and error-prone.
 */
#undef _B
#undef _C
#undef _L

#define _PPC    PREPC.d /* previous program counter */

#define _PCD    PC.d
#define _PC     PC.w.l

#define _USPD   USP.d
#define _USP    USP.w.l

#define _SSPD   SSP.d
#define _SSP    SSP.w.l

#define _SPD(cs)        (is_system(cs)?(cs)->_SSPD:(cs)->_USPD)
#define _SP(cs)         (is_system(cs)?(cs)->_SSP:(cs)->_USP)
#define SET_SPD(cs,val) if(is_system(cs)) (cs)->_SSPD = val; else (cs)->_USPD = val
#define SET_SP(cs,val)  if(is_system(cs)) (cs)->_SSP = val; else (cs)->_USP = val
#define INC2_SP(cs)     if(is_system(cs)) (cs)->_SSP += 2; else (cs)->_USP += 2
#define DEC2_SP(cs)     if(is_system(cs)) (cs)->_SSP -= 2; else (cs)->_USP -= 2
#define INC_SP(cs)      if(is_system(cs)) (cs)->_SSP ++; else (cs)->_USP ++
#define DEC_SP(cs)      if(is_system(cs)) (cs)->_SSP --; else (cs)->_USP --

#define _AFD    AF.d
#define _AF     AF.w.l
#define _A      AF.b.h
#define _F      AF.b.l

#define _BCD    BC.d
#define _BC     BC.w.l
#define _B      BC.b.h
#define _C      BC.b.l

#define _DED    DE.d
#define _DE     DE.w.l
#define _D      DE.b.h
#define _E      DE.b.l

#define _HLD    HL.d
#define _HL     HL.w.l
#define _H      HL.b.h
#define _L      HL.b.l

#define _IXD    IX.d
#define _IX     IX.w.l
#define _HX     IX.b.h
#define _LX     IX.b.l

#define _IYD    IY.d
#define _IY     IY.w.l
#define _HY     IY.b.h
#define _LY     IY.b.l

/* Control register bitmasks */
// MSR
#define Z280_MSR_US   0x4000   // User/System bit
#define Z280_MSR_BH   0x1000   // Breakpoint-on-halt bit
#define Z280_MSR_SSP  0x200	   // Single-Step Pending bit
#define Z280_MSR_SS   0x100	   // Single-Step bit
#define Z280_MSR_IREMASK  0x7f // Interrupt Request Enable bits (Group0-6)
#define Z280_MSR_US_USER 1
#define Z280_MSR_US_SYSTEM 0
#define is_system(cs) ((MSR(cs)&Z280_MSR_US) != Z280_MSR_US)
#define is_user(cs) ((MSR(cs)&Z280_MSR_US) == Z280_MSR_US)

// ISR
#define Z280_ISR_IVE  0xf000   // Interrupt Vector Enable bits
#define Z280_ISR_IM   0x300	   // Interrupt Mode
#define Z280_ISR_IRPMASK  0x7f // Interrupt Request Pending bits (Group0-6)

UINT8 interrupt_group[Z280_INT_MAX+1] = {-1, 0, 1, 1, 2, 3, 3, 3, 4, 5, 5, 6, 6};

// TCR
#define Z280_TCR_I    0x4      // Inhibit User I/O bit
#define Z280_TCR_E    0x2      // EPU Enable bit
#define Z280_TCR_S    0x1      // System Stack Overflow Warning bit

// Control register access
#define MSR(cs)      (*(UINT16*)&(cs)->cr[Z280_MSR])
#define ISR(cs)      (*(UINT16*)&(cs)->cr[Z280_ISR])
#define IVTP(cs)     (*(UINT16*)&(cs)->cr[Z280_IVTP])
#define BTI(cs)      ((cs)->cr[3]) // store BTI at offset 3
#define SSLR(cs)     (*(UINT16*)&(cs)->cr[Z280_SSLR])

#define CALC_IVADDR(cs,n)    (((IVTP(cs)&0xfff0)<<8)+n)   // calc interrupt vector address

/* Internal IO register bitmasks */
// Counter Timers
#define Z280_CTCR_CS    0x80   // Continuous/Single Cycle
#define Z280_CTCR_RE    0x40   // Retrigger Enable
#define Z280_CTCR_IE    0x20   // Interrupt Enable
#define Z280_CTCR_CTC   0x10   // Counter/Timer Cascade
#define Z280_CTCR_IPA   0x0f   // Input Pin Assignments
#define Z280_CTCR_EO    0x8    // Enable output
#define Z280_CTCR_CT    0x4    // Counter/Timer
#define Z280_CTCR_G     0x2    // Gate
#define Z280_CTCR_T     0x1    // Trigger

#define Z280_CTCSR_EN    0x80   // Enable
#define Z280_CTCSR_GT    0x40   // Retrigger Enable
#define Z280_CTCSR_TR    0x20   // Interrupt Enable
#define Z280_CTCSR_CIP   0x4    // Count in Progress
#define Z280_CTCSR_CC    0x2    // Counter/Timer Cascade
#define Z280_CTCSR_COR   0x1    // Count Overrun

// MMU
#define Z280_MMUMCR_UTE   0x8000  // User Mode Translate Enable
#define Z280_MMUMCR_UPD   0x4000  // User Mode Program/Data Separation Enable
#define Z280_MMUMCR_STE   0x800   // System Mode Translate Enable
#define Z280_MMUMCR_SPD   0x400   // System Mode Program/Data Separation Enable
#define Z280_MMUMCR_PFIMASK  0x1f   // Page Fault Identifier

#define Z280_PDR_PFA      0xfff0  // Page Frame Address
#define Z280_PDR_V        0x8	  // Valid
#define Z280_PDR_WP       0x4	  // Write-Protect
#define Z280_PDR_C        0x2	  // Cacheable
#define Z280_PDR_M        0x1	  // Modified

//#define MMU_DEBUG // log translated EAs
#define MMUMCR(cs)      (*(UINT16*)&(cs)->mmur[0])
#define PDRP(cs)        ((cs)->mmur[2])  // store PDRP at offset 2
#define MMU_REMAP_ADDR_FAILED     0x80000000

// DMA
#define Z280_DMAMCR_SR1  0x40     // SW Ready for DMA1
#define Z280_DMAMCR_SR0  0x20	  // SW Ready for DMA0
#define Z280_DMAMCR_EOP  0x10     // End of Process
#define Z280_DMAMCR_D3L  0x8	  // DMA3 Link
#define Z280_DMAMCR_D2L  0x4      // DMA2 Link
#define Z280_DMAMCR_D1T  0x2      // DMA1 to Transmitter Link
#define Z280_DMAMCR_D0R  0x1      // DMA0 to Receiver Link

#define Z280_DMATDR_EN   0x8000   // DMA Enable
#define Z280_DMATDR_SAD  0x7000	  // Source Address Descriptor
#define Z280_DMATDR_IE   0x800    // Interrupt Enable
#define Z280_DMATDR_ST   0x600	  // Size of Transfer
#define Z280_DMATDR_BRP  0x180    // Bus Request Protocol
#define Z280_DMATDR_TYPE 0x60     // Transaction Type
#define Z280_DMATDR_TC   0x10     // Transfer Complete
#define Z280_DMATDR_DAD  0xE      // Destination Address Descriptor
#define Z280_DMATDR_EPS  0x1      // End-of-Process Signalled

#define Z280_DMATDR_TYPE_FLOWTHR (0<<5)  // Flowthrough
#define Z280_DMATDR_TYPE_FLYBYW  (2<<5)	 // Flyby write (IO to mem)
#define Z280_DMATDR_TYPE_FLYBYR  (3<<5)	 // Flyby read (mem to IO)

#define Z280_DMATDR_DAD_INCM  (0<<1)  // Increment memory
#define Z280_DMATDR_DAD_DECM  (1<<1)  // Decrement memory
#define Z280_DMATDR_DAD_M     (2<<1)  // Memory
#define Z280_DMATDR_DAD_INCIO (4<<1)  // Increment IO
#define Z280_DMATDR_DAD_DECIO (5<<1)  // Decrement IO
#define Z280_DMATDR_DAD_IO    (6<<1)  // IO

#define Z280_DMATDR_SAD_INCM  (0<<12)
#define Z280_DMATDR_SAD_DECM  (1<<12)
#define Z280_DMATDR_SAD_M     (2<<12)
#define Z280_DMATDR_SAD_INCIO (4<<12)
#define Z280_DMATDR_SAD_DECIO (5<<12)
#define Z280_DMATDR_SAD_IO    (6<<12)

#define Z280_DMATDR_BRP_SINGLE (0<<7)
#define Z280_DMATDR_BRP_BURST  (1<<7)
#define Z280_DMATDR_BRP_CONT   (2<<7)  // Continuous

#define Z280_DMATDR_ST_BYTE   (0<<9)
#define Z280_DMATDR_ST_WORD   (1<<9)
#define Z280_DMATDR_ST_LONG   (2<<9)

/***************************************************************************
    CPU PREFIXES

    order is important here - see z280tbl.h
***************************************************************************/

#define Z280_PREFIX_op          0
#define Z280_PREFIX_cb          1
#define Z280_PREFIX_dd          2
#define Z280_PREFIX_ed          3
#define Z280_PREFIX_fd          4
#define Z280_PREFIX_xycb        5
#define Z280_PREFIX_dded        6
#define Z280_PREFIX_fded        7

#define Z280_PREFIX_COUNT       (Z280_PREFIX_fded + 1)



UINT8 SZ[256];       /* zero and sign flags */
UINT8 SZ_BIT[256];   /* zero, sign and parity/overflow (=zero) flags for BIT opcode */
UINT8 SZP[256];      /* zero, sign and parity flags */
UINT8 SZHV_inc[256]; /* zero, sign, half carry and overflow flags INC r8 */
UINT8 SZHV_dec[256]; /* zero, sign, half carry and overflow flags DEC r8 */

UINT8 *SZHVC_add;
UINT8 *SZHVC_sub;

UINT16 z280_readcontrol(struct z280_state *cpustate, offs_t port);
void z280_writecontrol(struct z280_state *cpustate, offs_t port, UINT16 data);
UINT8 z280_readio_byte(struct z280_state *cpustate, offs_t port);
void z280_writeio_byte(struct z280_state *cpustate, offs_t port, UINT8 data);
UINT16 z280_readio_word(struct z280_state *cpustate, offs_t port);
void z280_writeio_word(struct z280_state *cpustate, offs_t port, UINT16 data);
int z280_dma(struct z280_state *cpustate, int channel);
void cpu_burn_z280(device_t *device, int cycles);
//static void cpu_set_info_z280(device_t *device, UINT32 state, cpuinfo *info);
void z280_reload_timer(struct z280_state *cpustate, int unit);
void check_dma_interrupt(struct z280_state *cpustate, int channel);
int z280_take_dma(struct z280_state *cpustate);
int check_interrupts(struct z280_state *cpustate);
void set_irq_internal(device_t *device, int irq, int state);
int take_trap(struct z280_state *cpustate, int trap);

#include "z280ops.h"
#include "z280tbl.h"

#include "z280cb.c"
#include "z280xy.c"
#include "z280dd.c"
#include "z280fd.c"
#include "z280ed.c"
#include "z280dded.c"
#include "z280fded.c"
#include "z280op.c"

UINT16 z280_readcontrol(struct z280_state *cpustate, offs_t reg)
{
	UINT16 data = 0;

	switch (reg) {
	    /* 16 bit control registers */
		case Z280_MSR:
			data = *(UINT16*)&cpustate->cr[reg];
			LOG("Z280 '%s' MSR rd $%04x\n", cpustate->device->m_tag, data);
			break;
		case Z280_ISR:
			data = (*(UINT16*)&cpustate->cr[reg])|((UINT16)cpustate->IM<<8);
			/* check for pending interrupts */
			int i;
			for (i = Z280_INT_IRQ0; i <= Z280_INT_MAX; i++)
				data |= (1<<interrupt_group[i]);
			LOG("Z280 '%s' ISR rd $%04x\n", cpustate->device->m_tag, data);
			break;
		case Z280_IVTP:
			data = *(UINT16*)&cpustate->cr[reg];
			LOG("Z280 '%s' IVTP rd $%04x\n", cpustate->device->m_tag, data);
			break;
		case Z280_SSLR:
			data = *(UINT16*)&cpustate->cr[reg];
			LOG("Z280 '%s' SLR rd $%04x\n", cpustate->device->m_tag, data);
			break;

		/* 8 bit control registers */
		case Z280_IOP:
			data = cpustate->cr[reg];
			LOG("Z280 '%s' IOP rd $%02x\n", cpustate->device->m_tag, data);
			break;
		case Z280_BTI:
			data = BTI(cpustate);
			LOG("Z280 '%s' BTI rd $%02x\n", cpustate->device->m_tag, data);
			break;
		case Z280_BTC:
			data = cpustate->cr[reg];
			LOG("Z280 '%s' BTC rd $%02x\n", cpustate->device->m_tag, data);
			break;
		case Z280_TCR:
			data = cpustate->cr[reg];
			LOG("Z280 '%s' TCR rd $%02x\n", cpustate->device->m_tag, data);
			break;
		case Z280_CCR:
			data = cpustate->cr[reg];
			LOG("Z280 '%s' CCR rd $%02x\n", cpustate->device->m_tag, data);
			break;
		case Z280_LAR:
			data = cpustate->cr[reg];
			LOG("Z280 '%s' LAR rd $%02x\n", cpustate->device->m_tag, data);
			break;

		default:
			LOG("Z280 '%s' bogus read control reg %02X\n", cpustate->device->m_tag, reg & 0xff);
			break;
	}

	return data;
}

void z280_writecontrol(struct z280_state *cpustate, offs_t reg, UINT16 data)
{
	switch (reg) {
	    /* 16 bit control registers */
		case Z280_MSR:
			LOG("Z280 '%s' MSR wr $%04x\n", cpustate->device->m_tag, data);
			*(UINT16*)&cpustate->cr[reg] = data;
			break;
		case Z280_ISR:
			LOG("Z280 '%s' ISR wr $%04x\n", cpustate->device->m_tag, data);
			*(UINT16*)&cpustate->cr[reg] = data&Z280_ISR_IVE;
			break;
		case Z280_IVTP:
			LOG("Z280 '%s' IVTP wr $%04x\n", cpustate->device->m_tag, data);
			*(UINT16*)&cpustate->cr[reg] = data;
			break;
		case Z280_SSLR:
			LOG("Z280 '%s' SLR wr $%04x\n", cpustate->device->m_tag, data);
			*(UINT16*)&cpustate->cr[reg] = data&0xfff0;
			break;

		/* 8 bit control registers */
		case Z280_IOP:
			LOG("Z280 '%s' IOP wr $%02x\n", cpustate->device->m_tag, data&0xff);
			cpustate->cr[reg] = data;
			break;
		case Z280_BTI:
			LOG("Z280 '%s' BTI wr $%02x\n", cpustate->device->m_tag, data&0xff);
			BTI(cpustate) = data;
			break;
		case Z280_BTC:
			LOG("Z280 '%s' BTC wr $%02x\n", cpustate->device->m_tag, data&0xff);
			cpustate->cr[reg] = data;
			break;
		case Z280_TCR:
			LOG("Z280 '%s' TCR wr $%02x\n", cpustate->device->m_tag, data&0xff);
			cpustate->cr[reg] = data;
			break;
		case Z280_CCR:
			LOG("Z280 '%s' CCR wr $%02x\n", cpustate->device->m_tag, data&0xff);
			cpustate->cr[reg] = data;
			break;
		case Z280_LAR:
			LOG("Z280 '%s' LAR wr $%02x\n", cpustate->device->m_tag, data&0xff);
			cpustate->cr[reg] = data;
			break;

		default:                                                                      
			LOG("Z280 '%s' bogus write control reg %02X:$%04X\n", cpustate->device->m_tag, reg &0xff, data);
			break;
	}
}

INLINE int CT_UNIT(offs_t port) {
	int unit;
	switch (port & ((Z280_CTRSIZE-1) & ~(Z280_CTUSIZE-1))) {
		case 0<<3:
			unit = 0;
			break;
		case 1<<3:
			unit = 1;
			break;
		case 3<<3:
			unit = 2;
			break;
	}
	return unit;
}

/* 
   note that ZBUS is big endian (p.13-6,13-11):
   all bytes with an even address are transferred on AD8-15, and with an odd address on AD0-7;
   byte IO to an even address accesses the high byte of the peripheral register;
   byte IO to an odd address accesses the low byte of the peripheral register.

   TODO: all internal peripherals always use ZBUS mode regardless of the OPT pin?
*/

UINT8 z280_readio_byte(struct z280_state *cpustate, offs_t port)
{
	UINT8 data = 0;

	if(cpustate->cr[Z280_IOP] == Z280_UARTIOP && (port & Z280_UARTMASK) == Z280_UARTBASE) {
	    offs_t uartport = port & (Z280_UARTRSIZE-1);
		switch (uartport) {
			case Z280_UARTCR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport);
				LOG("Z280 '%s' UARTCR rd $%02x\n", cpustate->device->m_tag, data);
				break;
			case Z280_TCSR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport);
				LOG("Z280 '%s' TCS rd $%02x\n", cpustate->device->m_tag, data);
				break;
			case Z280_RCSR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport);
				LOG("Z280 '%s' RCS rd $%02x\n", cpustate->device->m_tag, data);
				break;
			case Z280_RDR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport);
				LOG("Z280 '%s' RDR rd $%02x\n", cpustate->device->m_tag, data);
				break;

			default:
				LOG("Z280 '%s' bogus read io reg b,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_CTIOP && (port & Z280_CTMASK) == Z280_CTBASE) {
	    int unit = CT_UNIT(port);
		offs_t ctport = port & (Z280_CTUSIZE-1);
		switch (ctport) {
			case Z280_CTCR:
				data = cpustate->ctcr[unit];
				LOG("Z280 '%s' CTCR%d rd $%02x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTCSR:
				data = cpustate->ctcsr[unit] |0x18;
				LOG("Z280 '%s' CTCSR%d rd $%02x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTTCR:
				data = cpustate->cttcr[unit]>>8;
				LOG("Z280 '%s' b,CTTCR%d (even) rd $%02x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTCTR:
				data = cpustate->ctctr[unit];
				LOG("Z280 '%s' b,CTCTR%d (odd) rd $%02x\n", cpustate->device->m_tag, unit, data);
				break;
			default:
				LOG("Z280 '%s' bogus read io reg b,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_MMUIOP && (port & Z280_MMUMASK) == Z280_MMUBASE) {
		offs_t mmuport = port & (Z280_MMURSIZE-1);
		switch (mmuport) {
			case Z280_MMUMCR:
				data = (MMUMCR(cpustate) | 0x33e0)>> 8;
				LOG("Z280 '%s' b,MMUMCR (even) rd $%02x\n", cpustate->device->m_tag, data);
				break;
			case Z280_PDRP:
				data = PDRP(cpustate);
				LOG("Z280 '%s' PDRP rd $%02x\n", cpustate->device->m_tag, data);
				break;
			case Z280_DSP:
				data = cpustate->pdr[PDRP(cpustate)];
				LOG("Z280 '%s' b,DSP (odd) rd $%02x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				break;
			case Z280_BMP:
				data = cpustate->pdr[PDRP(cpustate)]>>8;
				LOG("Z280 '%s' b,BMP (even) rd $%02x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				PDRP(cpustate)++;
				break;
			case Z280_IP:
				data = 0xff; /* unpredictable */
				LOG("Z280 '%s' IP rd $%02x\n", cpustate->device->m_tag, data);
				break;
			default:
				LOG("Z280 '%s' bogus read io reg b,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if (cpustate->cr[Z280_IOP] == Z280_RRRIOP && (port & Z280_RRRMASK) == Z280_RRR)
	{
		data = cpustate->rrr;
		LOG("Z280 '%s' RRR rd $%02x\n", cpustate->device->m_tag, data);
	}
	else
		LOG("Z280 unimplemented read io reg: b,%06X\n",port );

	return data;
}

void z280_writeio_byte(struct z280_state *cpustate, offs_t port, UINT8 data)
{
    if(cpustate->cr[Z280_IOP] == Z280_UARTIOP && (port & Z280_UARTMASK) == Z280_UARTBASE) {
	    offs_t uartport = port & (Z280_UARTRSIZE-1);
		switch (uartport) {
			case Z280_UARTCR:
			    LOG("Z280 '%s' UARTCR wr $%02x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data);
				break;
			case Z280_TCSR:
				LOG("Z280 '%s' TCS wr $%02x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data);
				break;
			case Z280_RCSR:
				LOG("Z280 '%s' RCS wr $%02x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data);
				break;
			case Z280_TDR:
				LOG("Z280 '%s' TDR wr $%02x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data);
				break;

			default:
				LOG("Z280 '%s' bogus write io reg b,%06X:$%02X\n", cpustate->device->m_tag, port, data);
				break;
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_CTIOP && (port & Z280_CTMASK) == Z280_CTBASE) {
	    int unit = CT_UNIT(port);
		offs_t ctport = port & (Z280_CTUSIZE-1);
		switch (ctport) {
			case Z280_CTCR:
				LOG("Z280 '%s' CTCR%d wr $%02x\n", cpustate->device->m_tag, unit, data);
				if (unit!=0) data &= ~Z280_CTCR_CTC;
				cpustate->ctcr[unit] = data;
				break;
			case Z280_CTCSR:
				LOG("Z280 '%s' CTCSR%d wr $%02x\n", cpustate->device->m_tag, unit, data);
				if (!(cpustate->ctcsr[unit] & Z280_CTCSR_TR) && 
				   (data & (Z280_CTCSR_EN | Z280_CTCSR_TR)) == (Z280_CTCSR_EN | Z280_CTCSR_TR)) // triggered
				{
					z280_reload_timer(cpustate, unit);
				}
				cpustate->ctcsr[unit] = data;
				int irq;
				if (!(cpustate->ctcsr[unit] & Z280_CTCSR_CC))
				{
					switch (unit) {
						case 0:
							irq = Z280_INT_CTR0;
							break;
						case 1:
							irq = Z280_INT_CTR1;
							break;
						case 2:
							irq = Z280_INT_CTR2;
							break;
					}
					LOG("%s CT%d clear interrupt\n", cpustate->device->m_tag, unit); 
					cpustate->int_pending[irq] = CLEAR_LINE;
				}
				break;
			case Z280_CTTCR:
				cpustate->cttcr[unit] = ((UINT16)data<<8) | (cpustate->cttcr[unit]&0xff);
				LOG("Z280 '%s' b,CTTCR%d (even) wr $%02x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTCTR:
				cpustate->ctctr[unit] = (UINT16)data | (cpustate->ctctr[unit]&0xff00);
				LOG("Z280 '%s' b,CTCTR%d (odd) wr $%02x\n", cpustate->device->m_tag, unit, data);
				break;
			default:
				LOG("Z280 '%s' bogus write io reg w,%06X:$%04X\n", cpustate->device->m_tag, port, data);
				break;
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_MMUIOP && (port & Z280_MMUMASK) == Z280_MMUBASE) {
		offs_t mmuport = port & (Z280_MMURSIZE-1);
		int i;
		switch (mmuport) {
			case Z280_MMUMCR:
				MMUMCR(cpustate) = ((UINT16)data<<8) | (MMUMCR(cpustate)&0xff);
				LOG("Z280 '%s' b,MMUMCR (even) wr $%02x\n", cpustate->device->m_tag, data);
				break;
			case Z280_PDRP:
				PDRP(cpustate) = data;
				LOG("Z280 '%s' PDRP wr $%02x\n", cpustate->device->m_tag, data);
				break;
			case Z280_DSP:
				cpustate->pdr[PDRP(cpustate)] = (UINT16)data | (cpustate->pdr[PDRP(cpustate)]&0xff00);
				LOG("Z280 '%s' b,DSP (odd) wr $%02x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				break;
			case Z280_BMP:
				cpustate->pdr[PDRP(cpustate)] = ((UINT16)data<<8) | (cpustate->pdr[PDRP(cpustate)]&0xff);
				LOG("Z280 '%s' b,BMP (even) wr $%02x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				PDRP(cpustate)++;
				break;
			case Z280_IP:
				if (data&0x1) // invalidate system 0-7
				{
					for (i=16; i<24; i++)
					{
						cpustate->pdr[i] &= ~Z280_PDR_V;
					}
				}
				if (data&0x2) // invalidate system 8-15
				{
					for (i=24; i<32; i++)
					{
						cpustate->pdr[i] &= ~Z280_PDR_V;
					}
				}
				if (data&0x4) // invalidate user 0-7
				{
					for (i=0; i<8; i++)
					{
						cpustate->pdr[i] &= ~Z280_PDR_V;
					}
				}
				if (data&0x8) // invalidate user 8-15
				{
					for (i=8; i<16; i++)
					{
						cpustate->pdr[i] &= ~Z280_PDR_V;
					}
				}
				LOG("Z280 '%s' IP wr $%02x\n", cpustate->device->m_tag, data);
				break;
			default:
				LOG("Z280 '%s' bogus write io reg b,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if (cpustate->cr[Z280_IOP] == Z280_RRRIOP && (port & Z280_RRRMASK) == Z280_RRR)
	{
		LOG("Z280 '%s' RRR wr $%02x\n", cpustate->device->m_tag, data);
		cpustate->rrr = data & 0xb0;
	}
	else
		LOG("Z280 unimplemented write io reg: b,%06X:%02X \n",port,data );

}

UINT16 z280_readio_word(struct z280_state *cpustate, offs_t port)
{
	UINT16 data = 0;

	if(cpustate->cr[Z280_IOP] == Z280_UARTIOP && (port & Z280_UARTMASK) == Z280_UARTBASE) {
	    offs_t uartport = port & (Z280_UARTRSIZE-1);
		switch (uartport) {
			case Z280_UARTCR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport) <<8;
				LOG("Z280 '%s' w,UARTCR (even) rd $%04x\n", cpustate->device->m_tag, data);
				break;
			case Z280_TCSR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport) <<8;
				LOG("Z280 '%s' w,TCS (even) rd $%04x\n", cpustate->device->m_tag, data);
				break;
			case Z280_RCSR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport) <<8;
				LOG("Z280 '%s' w,RCS (even) rd $%04x\n", cpustate->device->m_tag, data);
				break;
			case Z280_RDR:
				data = z280uart_device_register_read(cpustate->device->z280uart, uartport) <<8;
				LOG("Z280 '%s' w,RDR (even) rd $%04x\n", cpustate->device->m_tag, data);
				break;

			default:
				LOG("Z280 '%s' bogus read io reg b,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_CTIOP && (port & Z280_CTMASK) == Z280_CTBASE) {
	    int unit = CT_UNIT(port);
		offs_t ctport = port & (Z280_CTUSIZE-1);
		switch (ctport) {
			case Z280_CTCR:
				data = cpustate->ctcr[unit]<<8;
				LOG("Z280 '%s' w,CTCR%d (even) rd $%04x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTCSR:
				data = cpustate->ctcsr[unit] |0x18;
				LOG("Z280 '%s' w,CTCSR%d (odd) rd $%04x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTTCR:
				data = cpustate->cttcr[unit];
				LOG("Z280 '%s' CTTCR%d rd $%04x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTCTR:
				data = cpustate->ctctr[unit];
				LOG("Z280 '%s' CTCTR%d rd $%04x\n", cpustate->device->m_tag, unit, data);
				break;
			default:
				LOG("Z280 '%s' bogus read io reg w,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if (cpustate->cr[Z280_IOP] == Z280_DMAIOP && (port & Z280_DMAMASK) == Z280_DMABASE)
	{
	    if ((port & (Z280_DMARSIZE-1)) == Z280_DMAMCR)
		{
			data = (UINT16)cpustate->dmamcr|0xf080;
			LOG("Z280 '%s' DMAMCR rd $%04x\n", cpustate->device->m_tag, data);
		}
		else
		{
			int unit = (port & (Z280_DMARSIZE-1))>>3;
			offs_t dmaport = port & (Z280_DMAUSIZE-1);
			switch (dmaport) {
				case Z280_DAL:
					data = (cpustate->dar[unit]&0xfff) |0xf000;
					LOG("Z280 '%s' DAL%d rd $%04x\n", cpustate->device->m_tag, unit, data);
					break;
				case Z280_DAH:
					data = ((cpustate->dar[unit]>>8) & 0xfff0) |0xf;
					LOG("Z280 '%s' DAH%d rd $%04x\n", cpustate->device->m_tag, unit, data);
					break;
				case Z280_SAL:
					data = (cpustate->sar[unit]&0xfff) |0xf000;
					LOG("Z280 '%s' SAL%d rd $%04x\n", cpustate->device->m_tag, unit, data);
					break;
				case Z280_SAH:
					data = ((cpustate->sar[unit]>>8) & 0xfff0) |0xf;
					LOG("Z280 '%s' SAH%d rd $%04x\n", cpustate->device->m_tag, unit, data);
					break;
				case Z280_DMACNT:
					data = cpustate->dmacnt[unit];
					LOG("Z280 '%s' DMACNT%d rd $%04x\n", cpustate->device->m_tag, unit, data);
					break;
				case Z280_DMATDR:
					data = cpustate->dmatdr[unit];
					LOG("Z280 '%s' DMATDR%d rd $%04x\n", cpustate->device->m_tag, unit, data);
					break;
				default:
					LOG("Z280 '%s' bogus read io reg w,%06X\n", cpustate->device->m_tag, port);
					break;
			}
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_MMUIOP && (port & Z280_MMUMASK) == Z280_MMUBASE) {
		offs_t mmuport = port & (Z280_MMURSIZE-1);
		switch (mmuport) {
			case Z280_MMUMCR:
				data = MMUMCR(cpustate) | 0x33e0;
				LOG("Z280 '%s' MMUMCR rd $%04x\n", cpustate->device->m_tag, data);
				break;
			case Z280_PDRP:
				data = PDRP(cpustate);
				LOG("Z280 '%s' w,PDRP (odd) rd $%04x\n", cpustate->device->m_tag, data);
				break;
			case Z280_DSP:
				data = cpustate->pdr[PDRP(cpustate)];
				LOG("Z280 '%s' DSP rd $%04x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				break;
			case Z280_BMP:
				data = cpustate->pdr[PDRP(cpustate)];
				LOG("Z280 '%s' BMP rd $%04x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				PDRP(cpustate)++;
				break;
			case Z280_IP:
				data = 0xff <<8; /* unpredictable */
				LOG("Z280 '%s' w,IP (even) rd $%04x\n", cpustate->device->m_tag, data);
				break;
			default:
				LOG("Z280 '%s' bogus read io reg w,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if (cpustate->cr[Z280_IOP] == Z280_RRRIOP && (port & Z280_RRRMASK) == Z280_RRR)
	{
		data = cpustate->rrr <<8;
		LOG("Z280 '%s' w,RRR (even) rd $%04x\n", cpustate->device->m_tag, data);
	}
	else
		LOG("Z280 unimplemented read io reg: w,%06X\n",port );

	return data;
}

void z280_writeio_word(struct z280_state *cpustate, offs_t port, UINT16 data)
{
    if(cpustate->cr[Z280_IOP] == Z280_UARTIOP && (port & Z280_UARTMASK) == Z280_UARTBASE) {
	    offs_t uartport = port & (Z280_UARTRSIZE-1);
		switch (uartport) {
			case Z280_UARTCR:
			    LOG("Z280 '%s' w,UARTCR (even) wr $%04x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data>>8);
				break;
			case Z280_TCSR:
				LOG("Z280 '%s' w,TCS (even) wr $%04x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data>>8);
				break;
			case Z280_RCSR:
				LOG("Z280 '%s' w,RCS (even) wr $%04x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data>>8);
				break;
			case Z280_TDR:
				LOG("Z280 '%s' w,TDR (even) wr $%04x\n", cpustate->device->m_tag, data);
				z280uart_device_register_write(cpustate->device->z280uart, uartport, data>>8);
				break;

			default:
				LOG("Z280 '%s' bogus write io reg b,%06X:$%04X\n", cpustate->device->m_tag, port, data);
				break;
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_CTIOP && (port & Z280_CTMASK) == Z280_CTBASE) {
	    int unit = CT_UNIT(port);
		offs_t ctport = port & (Z280_CTUSIZE-1);
		switch (ctport) {
			case Z280_CTCR:
				LOG("Z280 '%s' w,CTCR%d (even) wr $%04x\n", cpustate->device->m_tag, unit, data);
				data>>=8;
				if (unit!=0) data &= ~Z280_CTCR_CTC;
				cpustate->ctcr[unit] = data;
				break;
			case Z280_CTCSR:
				LOG("Z280 '%s' w,CTCSR%d (odd) wr $%04x\n", cpustate->device->m_tag, unit, data);
				if (!(cpustate->ctcsr[unit] & Z280_CTCSR_TR) && 
				   (data & (Z280_CTCSR_EN | Z280_CTCSR_TR)) == (Z280_CTCSR_EN | Z280_CTCSR_TR)) // triggered
				{
					z280_reload_timer(cpustate, unit);
				}
				cpustate->ctcsr[unit] = data;
				int irq;
				if (!(cpustate->ctcsr[unit] & Z280_CTCSR_CC))
				{
					switch (unit) {
						case 0:
							irq = Z280_INT_CTR0;
							break;
						case 1:
							irq = Z280_INT_CTR1;
							break;
						case 2:
							irq = Z280_INT_CTR2;
							break;
					}
					LOG("%s CT%d clear interrupt\n", cpustate->device->m_tag, unit); 
					cpustate->int_pending[irq] = CLEAR_LINE;
				}
				break;
			case Z280_CTTCR:
				cpustate->cttcr[unit] = data;
				LOG("Z280 '%s' CTTCR%d wr $%04x\n", cpustate->device->m_tag, unit, data);
				break;
			case Z280_CTCTR:
				cpustate->ctctr[unit] = data;
				LOG("Z280 '%s' CTCTR%d wr $%04x\n", cpustate->device->m_tag, unit, data);
				break;
			default:
				LOG("Z280 '%s' bogus write io reg w,%06X:$%04X\n", cpustate->device->m_tag, port, data);
				break;
		}
	}
	else if (cpustate->cr[Z280_IOP] == Z280_DMAIOP && (port & Z280_DMAMASK) == Z280_DMABASE)
	{
	    if ((port & (Z280_DMARSIZE-1)) == Z280_DMAMCR)
		{
			LOG("Z280 '%s' DMAMCR wr $%04x\n", cpustate->device->m_tag, data);
			cpustate->dmamcr = data&0x7f;
		}
		else
		{
			int unit = (port & (Z280_DMARSIZE-1))>>3;
			offs_t dmaport = port & (Z280_DMAUSIZE-1);
			switch (dmaport) {
				case Z280_DAL:
					LOG("Z280 '%s' DAL%d wr $%04x\n", cpustate->device->m_tag, unit, data);
					cpustate->dar[unit] = (cpustate->dar[unit]&0xfff000) | (data&0xfff);
					break;
				case Z280_DAH:
					LOG("Z280 '%s' DAH%d wr $%04x\n", cpustate->device->m_tag, unit, data);
					cpustate->dar[unit] = (cpustate->dar[unit]&0xfff) | ((data&0xfff0) << 8);
					break;
				case Z280_SAL:
					LOG("Z280 '%s' SAL%d wr $%04x\n", cpustate->device->m_tag, unit, data);
					cpustate->sar[unit] = (cpustate->sar[unit]&0xfff000) | (data&0xfff);
					break;
				case Z280_SAH:
					LOG("Z280 '%s' SAH%d wr $%04x\n", cpustate->device->m_tag, unit, data);
					cpustate->sar[unit] = (cpustate->sar[unit]&0xfff) | ((data&0xfff0) << 8);
					break;
				case Z280_DMACNT:
					LOG("Z280 '%s' DMACNT%d wr $%04x\n", cpustate->device->m_tag, unit, data);
					cpustate->dmacnt[unit] = data;
					break;
				case Z280_DMATDR:
					LOG("Z280 '%s' DMATDR%d wr $%04x\n", cpustate->device->m_tag, unit, data);
					cpustate->dmatdr[unit] = data;
					break;
				default:
					LOG("Z280 '%s' bogus write io reg w,%06X:$%04X\n", cpustate->device->m_tag, port, data);
					break;
			}
		}
	}
	else if(cpustate->cr[Z280_IOP] == Z280_MMUIOP && (port & Z280_MMUMASK) == Z280_MMUBASE) {
		offs_t mmuport = port & (Z280_MMURSIZE-1);
		switch (mmuport) {
			case Z280_MMUMCR:
				MMUMCR(cpustate) = data;
				LOG("Z280 '%s' MMUMCR wr $%04x\n", cpustate->device->m_tag, data);
				break;
			case Z280_PDRP:
				PDRP(cpustate) = data;
				LOG("Z280 '%s' w,PDRP (odd) wr $%04x\n", cpustate->device->m_tag, data);
				break;
			case Z280_DSP:
				cpustate->pdr[PDRP(cpustate)] = data;
				LOG("Z280 '%s' DSP wr $%04x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				break;
			case Z280_BMP:
				cpustate->pdr[PDRP(cpustate)] = data;
				LOG("Z280 '%s' BMP wr $%04x pdr=%d\n", cpustate->device->m_tag, data, PDRP(cpustate));
				PDRP(cpustate)++;
				break;
			case Z280_IP:
				LOG("Z280 '%s' w,IP (even) wr $%04x\n", cpustate->device->m_tag, data);
				z280_writeio_byte(cpustate, port, data>>8);
				break;
			default:
				LOG("Z280 '%s' bogus write io reg w,%06X\n", cpustate->device->m_tag, port);
				break;
		}
	}
	else if (cpustate->cr[Z280_IOP] == Z280_RRRIOP && (port & Z280_RRRMASK) == Z280_RRR)
	{
		LOG("Z280 '%s' w,RRR (even) wr $%02x\n", cpustate->device->m_tag, data);
		data >>= 8;
		cpustate->rrr = data & 0xb0;
	}
	else
		LOG("Z280 unimplemented write io reg: w,%06X:%04X \n",port,data );

}

int z280_check_dma(struct z280_state *cpustate)
{
	int cycles = 0;
	// if a channel is active, do DMA
	if (cpustate->dma_active != -1)
	{
		cycles = z280_take_dma(cpustate);
	}
	else
	{
		// DMA is idle. check for requests
		if (((cpustate->dmamcr & Z280_DMAMCR_SR0)||cpustate->rdy_state[0]) && (cpustate->dmatdr[0] & Z280_DMATDR_EN))
		{
			LOG("Z280 '%s' DMA0 service request\n", cpustate->device->m_tag);
			cpustate->dma_pending[0] = 1;
		}
		else if (((cpustate->dmamcr & Z280_DMAMCR_SR1)||cpustate->rdy_state[1]) && (cpustate->dmatdr[1] & Z280_DMATDR_EN))
		{
			LOG("Z280 '%s' DMA1 service request\n", cpustate->device->m_tag);
			cpustate->dma_pending[1] = 1;
		}
		else if (cpustate->rdy_state[2] && (cpustate->dmatdr[2] & Z280_DMATDR_EN))
		{
			LOG("Z280 '%s' DMA2 service request\n", cpustate->device->m_tag);
			cpustate->dma_pending[2] = 1;
		}
		else if (cpustate->rdy_state[3] && (cpustate->dmatdr[3] & Z280_DMATDR_EN))
		{
			LOG("Z280 '%s' DMA3 service request\n", cpustate->device->m_tag);
			cpustate->dma_pending[3] = 1;
		}

		int i;
		/* activate request with the highest priority */
		for (i = 0; i < 4; i++)
			if (cpustate->dma_pending[i])
			{
				cpustate->dma_active = i;
				cpustate->dma_pending[i] = 0;
				cycles = z280_take_dma(cpustate) + 10;
				break;
			}
	}
	return cycles;
}

#define INCR_DAR_SAR(sz) { \
    if ((cpustate->dmatdr[channel] & (Z280_DMATDR_DAD &~0x8)) == Z280_DMATDR_DAD_INCM) \
	   cpustate->dar[channel] += sz; \
    else if ((cpustate->dmatdr[channel] & (Z280_DMATDR_DAD &~0x8)) == Z280_DMATDR_DAD_DECM) \
	   cpustate->dar[channel] -= sz;  \
    if ((cpustate->dmatdr[channel] & (Z280_DMATDR_SAD &~0x4000)) == Z280_DMATDR_SAD_INCM) \
	   cpustate->sar[channel] += sz; \
    else if ((cpustate->dmatdr[channel] & (Z280_DMATDR_SAD &~0x4000)) == Z280_DMATDR_SAD_DECM) \
	   cpustate->sar[channel] -= sz;  \
	cpustate->dmacnt[channel]--; \
}

void check_dma_interrupt(struct z280_state *cpustate, int channel) {
	int irq;
	switch (channel)
	{
		case 0:
			irq = Z280_INT_DMA0;
			break;
		case 1:
			irq = Z280_INT_DMA1;
			break;
		case 2:
			irq = Z280_INT_DMA2;
			break;
		case 3:
			irq = Z280_INT_DMA3;
			break;
	}
	if ((cpustate->dmatdr[channel] & Z280_DMATDR_IE) && (cpustate->dmatdr[channel] & (Z280_DMATDR_EPS | Z280_DMATDR_TC)))
	{
		set_irq_internal(cpustate->device, irq, ASSERT_LINE);
		LOG("Z280 '%s' DMA%d assert interrupt\n", cpustate->device->m_tag, channel);
	}
	else
	{
		set_irq_internal(cpustate->device, irq, CLEAR_LINE);
		LOG("Z280 '%s' DMA%d clear interrupt\n", cpustate->device->m_tag, channel);
	}
}

int z280_take_dma(struct z280_state *cpustate)
{
	int cycles = 0;
	int channel = cpustate->dma_active;
	UINT16 data;
	LOG("Z280 '%s' DMA%d busrq dar=%06X sar=%06X cnt=%04X\n", cpustate->device->m_tag, channel, cpustate->dar[channel], cpustate->sar[channel], cpustate->dmacnt[channel]);
	while (cpustate->dmacnt[channel]) {
		// do one move
		if ((cpustate->dmatdr[channel] & Z280_DMATDR_DAD) <= Z280_DMATDR_DAD_M && (cpustate->dmatdr[channel] & Z280_DMATDR_SAD) <= Z280_DMATDR_SAD_M)
		{
			// memory to memory
			if ((cpustate->dmatdr[channel] & Z280_DMATDR_TYPE) == Z280_DMATDR_TYPE_FLOWTHR)
			{
				switch (cpustate->dmatdr[channel] & Z280_DMATDR_ST)
				{
					case Z280_DMATDR_ST_WORD:
						data = cpustate->ram->read_word(cpustate->sar[channel]&0xfffffe);
						LOG("Z280 '%s' DMA%d move w M<-M dar=%06X sar=%06X $%04X\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data);
						cpustate->ram->write_word(cpustate->dar[channel]&0xfffffe, data);
						INCR_DAR_SAR(2);
						break;
					case Z280_DMATDR_ST_BYTE:
						data = cpustate->ram->read_byte(cpustate->sar[channel]);
						LOG("Z280 '%s' DMA%d move b M<-M dar=%06X sar=%06X $%02X '%c'\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data, isprint(data) ? data : ' ');
						cpustate->ram->write_byte(cpustate->dar[channel], data);
						INCR_DAR_SAR(1);
						break;
					default:
						LOG("Z280 '%s' DMA%d invalid size $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_ST)>>9);
						break;
				}
			}
			else
			{
				LOG("Z280 '%s' DMA%d invalid transaction type $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_TYPE)>>5);
			}
		}
		else if ((cpustate->dmatdr[channel] & Z280_DMATDR_DAD) <= Z280_DMATDR_DAD_M && (cpustate->dmatdr[channel] & Z280_DMATDR_SAD) >= Z280_DMATDR_SAD_INCIO && (cpustate->dmatdr[channel] & Z280_DMATDR_SAD) <= Z280_DMATDR_SAD_IO)
		{
			// IO to memory
			if ((cpustate->dmatdr[channel] & Z280_DMATDR_TYPE) == Z280_DMATDR_TYPE_FLOWTHR || (channel < 2 && (cpustate->dmatdr[channel] & Z280_DMATDR_TYPE) == Z280_DMATDR_TYPE_FLYBYW))
			{																																			
				switch (cpustate->dmatdr[channel] & Z280_DMATDR_ST)
				{
					case Z280_DMATDR_ST_LONG:
						LOG("Z280 '%s' DMA%d unimplemented move l M<-I dar=%06X sar=%06X $%04X\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data);
						break;
					case Z280_DMATDR_ST_WORD:
						data = IN16(cpustate, cpustate->sar[channel]);
						LOG("Z280 '%s' DMA%d move w M<-I dar=%06X sar=%06X $%04X\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data);
						cpustate->ram->write_word(cpustate->dar[channel]&0xfffffe, data);
						INCR_DAR_SAR(2);
						break;
					case Z280_DMATDR_ST_BYTE:
						data = IN(cpustate, cpustate->sar[channel]);
						LOG("Z280 '%s' DMA%d move b M<-I dar=%06X sar=%06X $%02X '%c'\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data, isprint(data) ? data : ' ');
						cpustate->ram->write_byte(cpustate->dar[channel], data);
						INCR_DAR_SAR(1);
						break;
					default:
						LOG("Z280 '%s' DMA%d invalid size $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_ST)>>9);
						break;
				}
			}
			else
			{
				LOG("Z280 '%s' DMA%d invalid transaction type $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_TYPE)>>5);
			}
		}
		else if ((cpustate->dmatdr[channel] & Z280_DMATDR_DAD) >= Z280_DMATDR_DAD_INCIO && (cpustate->dmatdr[channel] & Z280_DMATDR_DAD) <= Z280_DMATDR_DAD_IO && (cpustate->dmatdr[channel] & Z280_DMATDR_SAD) <= Z280_DMATDR_SAD_M)
		{
			// memory to IO
			if ((cpustate->dmatdr[channel] & Z280_DMATDR_TYPE) == Z280_DMATDR_TYPE_FLOWTHR || (channel < 2 && (cpustate->dmatdr[channel] & Z280_DMATDR_TYPE) == Z280_DMATDR_TYPE_FLYBYR))
			{																																			
				switch (cpustate->dmatdr[channel] & Z280_DMATDR_ST)
				{
					case Z280_DMATDR_ST_LONG:
						LOG("Z280 '%s' DMA%d unimplemented move l I<-M dar=%06X sar=%06X $%04X\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data);
						break;
					case Z280_DMATDR_ST_WORD:
						data = cpustate->ram->read_word(cpustate->sar[channel]&0xfffffe);
						LOG("Z280 '%s' DMA%d move w I<-M dar=%06X sar=%06X $%04X\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data);
						OUT16(cpustate, cpustate->dar[channel], data);
						INCR_DAR_SAR(2);
						break;
					case Z280_DMATDR_ST_BYTE:
						data = cpustate->ram->read_byte(cpustate->sar[channel]);
						LOG("Z280 '%s' DMA%d move b I<-M dar=%06X sar=%06X $%02X '%c'\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data, isprint(data) ? data : ' ');
						OUT(cpustate, cpustate->dar[channel], data);
						INCR_DAR_SAR(1);
						break;
					default:
						LOG("Z280 '%s' DMA%d invalid size $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_ST)>>9);
						break;
				}
			}
			else
			{
				LOG("Z280 '%s' DMA%d invalid transaction type $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_TYPE)>>5);
			}
		}
		else if ((cpustate->dmatdr[channel] & Z280_DMATDR_DAD) >= Z280_DMATDR_DAD_INCIO && (cpustate->dmatdr[channel] & Z280_DMATDR_DAD) <= Z280_DMATDR_DAD_IO 
			&& (cpustate->dmatdr[channel] & Z280_DMATDR_SAD) >= Z280_DMATDR_SAD_INCIO && (cpustate->dmatdr[channel] & Z280_DMATDR_SAD) <= Z280_DMATDR_SAD_IO)
		{
			// IO to IO
			if ((cpustate->dmatdr[channel] & Z280_DMATDR_TYPE) == Z280_DMATDR_TYPE_FLOWTHR)
			{																																			
				switch (cpustate->dmatdr[channel] & Z280_DMATDR_ST)
				{
					case Z280_DMATDR_ST_WORD:
						data = IN16(cpustate, cpustate->sar[channel]);
						LOG("Z280 '%s' DMA%d move w I<-I dar=%06X sar=%06X $%04X\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data);
						OUT16(cpustate, cpustate->dar[channel], data);
						INCR_DAR_SAR(2);
						break;
					case Z280_DMATDR_ST_BYTE:
						data = IN(cpustate, cpustate->sar[channel]);
						LOG("Z280 '%s' DMA%d move b I<-I dar=%06X sar=%06X $%02X '%c'\n", cpustate->device->m_tag, channel,  cpustate->dar[channel], cpustate->sar[channel], data, isprint(data) ? data : ' ');
						OUT(cpustate, cpustate->dar[channel], data);
						INCR_DAR_SAR(1);
						break;
					default:
						LOG("Z280 '%s' DMA%d invalid size $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_ST)>>9);
						break;
				}
			}
			else
			{
				LOG("Z280 '%s' DMA%d invalid transaction type $%02X\n", cpustate->device->m_tag, channel, (cpustate->dmatdr[channel] & Z280_DMATDR_TYPE)>>5);
			}
		}

		// check termination conditions
		// TODO EOP
		if ((cpustate->dmatdr[channel] & Z280_DMATDR_BRP) == Z280_DMATDR_BRP_BURST)
		{
			if (cpustate->rdy_state[channel] == CLEAR_LINE)	// interrupted by pulling /RDY high
			{
				cpustate->dma_active = -1;
				return cycles;
			}
		}
		else if ((cpustate->dmatdr[channel] & Z280_DMATDR_BRP) != Z280_DMATDR_BRP_CONT)
		{
			// single mode
			cpustate->dma_active = -1;
			return cycles;
		}
	}
	LOG("Z280 '%s' DMA%d finished dar=%06X sar=%06X cnt=%04X\n", cpustate->device->m_tag, channel, cpustate->dar[channel], cpustate->sar[channel], cpustate->dmacnt[channel]);

	// set TC and INT; reset EN
	cpustate->dmatdr[channel] |= Z280_DMATDR_TC;
	cpustate->dmatdr[channel] &= ~Z280_DMATDR_EN;
	check_dma_interrupt(cpustate, channel);
	cpustate->dma_active = -1;

	return cycles;
}

struct z280_device *cpu_create_z280(char *tag, UINT32 type, UINT32 clock, 
    struct address_space *ram,
	struct address_space *iospace, device_irq_acknowledge_callback irqcallback, struct z80daisy_interface *daisy_init,
	init_byte_callback bti_init_cb, /* init BTI by AD0-AD7 on reset */
	int bus16, /* OPT pin 8 or 16bit bus */
	UINT32 ctin0, UINT32 ctin1, UINT32 ctin2, /* CTINx clocks (optional) */
	rx_callback_t z280uart_rx_cb,tx_callback_t z280uart_tx_cb)
{
	struct z280_device *d = malloc(sizeof(struct z280_device));
	memset(d,0,sizeof(struct z280_device));
	d->m_type = type;
	d->m_clock = clock;
	d->m_tag = tag;

	struct z280_state *cpustate = malloc(sizeof(struct z280_state));
	d->m_token = cpustate;
	memset(cpustate,0,sizeof(struct z280_state));
	cpustate->device = d;

	cpustate->ram = ram;
	cpustate->iospace = iospace;

	//struct z280_state *cpustate = get_safe_token(device);
	//if (device->static_config() != NULL)
	//	cpustate->daisy.init(device, (const z80_daisy_config *)device->static_config());
	if (daisy_init != NULL)
		cpustate->daisy = z80_daisy_chain_create(d,daisy_init); // allocate head and build chain pointers
	cpustate->irq_callback = irqcallback;

	d->bti_init_cb = bti_init_cb;
	d->m_bus16 = bus16;
	d->m_ctin0 = ctin0;
	d->m_ctin1 = ctin1;
	if (ctin1) {
		d->ctin1_brg_const = clock / ctin1;
		d->ctin1_uart_timer = 0;
	}
	d->m_ctin2 = ctin2;

	d->z280uart_tag = malloc(20);
	strcpy(d->z280uart_tag,tag);
	strcat(d->z280uart_tag,"UART");
	d->z280uart = z280uart_device_create(d,d->z280uart_tag,/*clock,*/
			z280uart_rx_cb, z280uart_tx_cb);

	SZHVC_add = malloc(2*256*256);
	SZHVC_sub = malloc(2*256*256);

	int i, p;
	int oldval, newval, val;
	UINT8 *padd, *padc, *psub, *psbc;
	/* allocate big flag arrays once */
	padd = &SZHVC_add[  0*256];
	padc = &SZHVC_add[256*256];
	psub = &SZHVC_sub[  0*256];
	psbc = &SZHVC_sub[256*256];
	for (oldval = 0; oldval < 256; oldval++)
	{
		for (newval = 0; newval < 256; newval++)
		{
			/* add or adc w/o carry set */
			val = newval - oldval;
			*padd = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
			*padd |= (newval & (YF | XF));  /* undocumented flag bits 5+3 */

			if( (newval & 0x0f) < (oldval & 0x0f) ) *padd |= HF;
			if( newval < oldval ) *padd |= CF;
			if( (val^oldval^0x80) & (val^newval) & 0x80 ) *padd |= VF;
			padd++;

			/* adc with carry set */
			val = newval - oldval - 1;
			*padc = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
			*padc |= (newval & (YF | XF));  /* undocumented flag bits 5+3 */
			if( (newval & 0x0f) <= (oldval & 0x0f) ) *padc |= HF;
			if( newval <= oldval ) *padc |= CF;
			if( (val^oldval^0x80) & (val^newval) & 0x80 ) *padc |= VF;
			padc++;

			/* cp, sub or sbc w/o carry set */
			val = oldval - newval;
			*psub = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
			*psub |= (newval & (YF | XF));  /* undocumented flag bits 5+3 */
			if( (newval & 0x0f) > (oldval & 0x0f) ) *psub |= HF;
			if( newval > oldval ) *psub |= CF;
			if( (val^oldval) & (oldval^newval) & 0x80 ) *psub |= VF;
			psub++;

			/* sbc with carry set */
			val = oldval - newval - 1;
			*psbc = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
			*psbc |= (newval & (YF | XF));  /* undocumented flag bits 5+3 */
			if( (newval & 0x0f) >= (oldval & 0x0f) ) *psbc |= HF;
			if( newval >= oldval ) *psbc |= CF;
			if( (val^oldval) & (oldval^newval) & 0x80 ) *psbc |= VF;
			psbc++;
		}
	}
	for (i = 0; i < 256; i++)
	{
		p = 0;
		if( i&0x01 ) ++p;
		if( i&0x02 ) ++p;
		if( i&0x04 ) ++p;
		if( i&0x08 ) ++p;
		if( i&0x10 ) ++p;
		if( i&0x20 ) ++p;
		if( i&0x40 ) ++p;
		if( i&0x80 ) ++p;
		SZ[i] = i ? i & SF : ZF;
		SZ[i] |= (i & (YF | XF));       /* undocumented flag bits 5+3 */
		SZ_BIT[i] = i ? i & SF : ZF | PF;
		SZ_BIT[i] |= (i & (YF | XF));   /* undocumented flag bits 5+3 */
		SZP[i] = SZ[i] | ((p & 1) ? 0 : PF);
		SZHV_inc[i] = SZ[i];
		if( i == 0x80 ) SZHV_inc[i] |= VF;
		if( (i & 0x0f) == 0x00 ) SZHV_inc[i] |= HF;
		SZHV_dec[i] = SZ[i] | NF;
		if( i == 0x7f ) SZHV_dec[i] |= VF;
		if( (i & 0x0f) == 0x0f ) SZHV_dec[i] |= HF;
	}

	/* set up the state table */
	/*{
		device_state_interface *state;
		device->interface(state);
		state->state_add(Z280_PC,         "PC",        cpustate->PC.w.l);
		state->state_add(STATE_GENPC,     "GENPC",     cpustate->_PCD).noshow();
		state->state_add(STATE_GENPCBASE, "GENPCBASE", cpustate->PREPC.w.l).noshow();
		state->state_add(Z280_USP,        "USP",       cpustate->USP.w.l);
		state->state_add(Z280_SSP,        "SSP",       cpustate->SSP.w.l);
		state->state_add(STATE_GENSP,     "GENSP",     _SP(cpustate)).noshow();
		state->state_add(STATE_GENFLAGS,  "GENFLAGS",  cpustate->AF.b.l).noshow().formatstr("%8s");
		state->state_add(Z280_A,          "A",         cpustate->_A).noshow();
		state->state_add(Z280_B,          "B",         cpustate->_B).noshow();
		state->state_add(Z280_C,          "C",         cpustate->_C).noshow();
		state->state_add(Z280_D,          "D",         cpustate->_D).noshow();
		state->state_add(Z280_E,          "E",         cpustate->_E).noshow();
		state->state_add(Z280_H,          "H",         cpustate->_H).noshow();
		state->state_add(Z280_L,          "L",         cpustate->_L).noshow();
		state->state_add(Z280_AF,         "AF",        cpustate->AF.w.l);
		state->state_add(Z280_BC,         "BC",        cpustate->BC.w.l);
		state->state_add(Z280_DE,         "DE",        cpustate->DE.w.l);
		state->state_add(Z280_HL,         "HL",        cpustate->HL.w.l);
		state->state_add(Z280_IX,         "IX",        cpustate->IX.w.l);
		state->state_add(Z280_IY,         "IY",        cpustate->IY.w.l);
		state->state_add(Z280_AF2,        "AF2",       cpustate->AF2.w.l);
		state->state_add(Z280_BC2,        "BC2",       cpustate->BC2.w.l);
		state->state_add(Z280_DE2,        "DE2",       cpustate->DE2.w.l);
		state->state_add(Z280_HL2,        "HL2",       cpustate->HL2.w.l);
		state->state_add(Z280_R,          "R",         cpustate->rtemp).callimport().callexport();
		state->state_add(Z280_I,          "I",         cpustate->I);
		state->state_add(Z280_IM,         "IM",        cpustate->IM).mask(0x3);
		state->state_add(Z280_IFF2,       "IFF2",      cpustate->IFF2);
		state->state_add(Z280_HALT,       "HALT",      cpustate->HALT).mask(0x1);

	}

	device->save_item(NAME(cpustate->AF.w.l));
	device->save_item(NAME(cpustate->BC.w.l));
	device->save_item(NAME(cpustate->DE.w.l));
	device->save_item(NAME(cpustate->HL.w.l));
	device->save_item(NAME(cpustate->IX.w.l));
	device->save_item(NAME(cpustate->IY.w.l));
	device->save_item(NAME(cpustate->PC.w.l));
	device->save_item(NAME(_SP(cpustate)));
	device->save_item(NAME(cpustate->AF2.w.l));
	device->save_item(NAME(cpustate->BC2.w.l));
	device->save_item(NAME(cpustate->DE2.w.l));
	device->save_item(NAME(cpustate->HL2.w.l));
	device->save_item(NAME(cpustate->R));
	device->save_item(NAME(cpustate->IFF2));
	device->save_item(NAME(cpustate->HALT));
	device->save_item(NAME(cpustate->IM));
	device->save_item(NAME(cpustate->I));
	device->save_item(NAME(cpustate->nmi_state));
	device->save_item(NAME(cpustate->nmi_pending));
	device->save_item(NAME(cpustate->irq_state));
	device->save_item(NAME(cpustate->int_pending));
	device->save_item(NAME(cpustate->after_EI));

	device->save_item(NAME(cpustate->mmu));
	*/
	return d;
}

/****************************************************************************
 * Reset registers to their initial values
 ****************************************************************************/

void cpu_reset_z280(device_t *device)
{
	LOG("cpu_reset_z280\n");
	struct z280_state *cpustate = get_safe_token(device);

	cpustate->_PPC = 0;
	cpustate->_PCD = 0;
	cpustate->_USP = 0;
	cpustate->_SSP = 0;
	cpustate->_AFD = 0;
	cpustate->_BCD = 0;
	cpustate->_DED = 0;
	cpustate->_HLD = 0;
	cpustate->_IXD = 0;
	cpustate->_IYD = 0;
	cpustate->AF2.d = 0;
	cpustate->BC2.d = 0;
	cpustate->DE2.d = 0;
	cpustate->HL2.d = 0;
	cpustate->AF2inuse = 0;
	cpustate->BC2inuse = 0;
	cpustate->R = 0;
	cpustate->IFF2 = 0;
	cpustate->HALT = 0;
	cpustate->IM = 0;
	cpustate->I = 0;
	cpustate->nmi_state = CLEAR_LINE;
	cpustate->nmi_pending = 0;
	memset(cpustate->int_pending, 0, sizeof(cpustate->int_pending));
	cpustate->irq_state[0] = CLEAR_LINE;
	cpustate->irq_state[1] = CLEAR_LINE;
	cpustate->irq_state[2] = CLEAR_LINE;
	cpustate->after_EI = 0;
	cpustate->ea = 0;
	cpustate->abort_type = Z280_ABORT_ACCV;	// default is ACCV

	memcpy(cpustate->cc, (UINT8 *)cc_default, sizeof(cpustate->cc));
	//cpustate->_IX = cpustate->_IY = 0;
	//cpustate->_F = ZF;          /* Zero flag is set */

	/* reset cr registers */
	memset(cpustate->cr, 0, sizeof(cpustate->cr));
	
	/* reset io registers */
	cpustate->rrr = 0;
	memset(cpustate->mmur, 0, sizeof(cpustate->mmur));
	memset(cpustate->pdr, 0, sizeof(cpustate->pdr));
	int i;
	for (i=0; i<3; i++)
	{
	   cpustate->ctcr[i] = 0;
	   cpustate->ctcsr[i] = 0;
	}
	cpustate->timer_cnt = 0;

    cpustate->dar[0] = 0;
    cpustate->dmatdr[0] = 0x100;
    cpustate->dmacnt[0] = 0x100;
	for (i=1; i<4; i++)
	{
	   cpustate->dmatdr[i] &= ~(Z280_DMATDR_EN | Z280_DMATDR_IE | Z280_DMATDR_TC | Z280_DMATDR_EPS);
	}
	cpustate->dmamcr = 0;
	cpustate->dma_active = -1;
	memset(cpustate->dma_pending, 0, sizeof(cpustate->dma_pending));
	memset(cpustate->rdy_state, 0, sizeof(cpustate->rdy_state));

	if (cpustate->daisy != NULL)
		z80_daisy_chain_post_reset(cpustate->daisy);
	/* cache mmu tables */
	z280_mmu(cpustate);

	cpustate->cr[Z280_BTC] = 0x30;
	BTI(cpustate) = cpustate->device->bti_init_cb(cpustate->device);
	cpustate->cr[Z280_CCR] = 0x80;
}

#define timer_linking (cpustate->ctcr[0] & Z280_CTCR_CTC)

/* Reload CT timer */
void z280_reload_timer(struct z280_state *cpustate, int unit)
{
	if (unit != 0 || !timer_linking) // CT1,2 always reload, CT0 only if not linked
	{
		cpustate->ctctr[unit] = cpustate->cttcr[unit];
		LOG("%s CT%d reloaded $%04X\n", cpustate->device->m_tag, unit, cpustate->cttcr[unit]);
	}
	if (unit == 1 && timer_linking)
	{
		cpustate->ctctr[0] = cpustate->cttcr[0]; // reload low word in CT0 also
		LOG("%s CT1:CT0 reloaded $%04X:%04X\n", cpustate->device->m_tag, cpustate->cttcr[1],cpustate->cttcr[0]);
	}
}

/* Terminal count
   execute actions when a timer counts to 0
*/
void terminal_count(struct z280_state *cpustate, int unit)
{
	LOG("%s CT%d counted to 0\n", cpustate->device->m_tag, unit);
	if (cpustate->ctcsr[unit] & Z280_CTCSR_CC)
	{
		cpustate->ctcsr[unit] |= Z280_CTCSR_COR;
	}
	else
	{
		cpustate->ctcsr[unit] |= Z280_CTCSR_CC;
	}

	if (unit == 0 && timer_linking)
	{
		cpustate->ctctr[1]--; // decrement high word in CT1
	}

	// clock UART from CT1
	if (unit == 1 && (cpustate->device->z280uart->m_uartcr & 0x8) /*UARTCR_CS*/)
	{
		z280uart_device_timer(cpustate->device->z280uart);
	}

	if(cpustate->ctcr[unit] & Z280_CTCR_IE)
	{
		int irq;
		switch (unit) {
			case 0:
				irq = Z280_INT_CTR0;
				break;
			case 1:
				irq = Z280_INT_CTR1;
				break;
			case 2:
				irq = Z280_INT_CTR2;
				break;
		}
		LOG("%s CT%d assert interrupt\n", cpustate->device->m_tag, unit); 
		cpustate->int_pending[irq] = ASSERT_LINE;
	}
}


/* Clock CT timers 
   decrement timers according to cycles elapsed in the last instruction execution
*/
void clock_timers(struct z280_state *cpustate, int cycles)
{
	/* This is not the best place, but: if the UART is clocked from CTIN1 (the default),
	   we are bypassing CT1 but need to call z280uart_device_timer in constant intervals
	   according to the main clock / CTIN1 ratio. 
	   This is precalculated and saved as ctin1_brg_const. */
	if (!(cpustate->device->z280uart->m_uartcr & 0x8 /*UARTCR_CS*/))
	{
		cpustate->device->ctin1_uart_timer += cycles;
		if (cpustate->device->ctin1_uart_timer >= cpustate->device->ctin1_brg_const)
		{
			z280uart_device_timer(cpustate->device->z280uart);
			cpustate->device->ctin1_uart_timer -= cpustate->device->ctin1_brg_const;
		}
	}

	// now the real CT stuff
	cpustate->timer_cnt += cycles;

	if (cpustate->timer_cnt >= 4) // p.9-2, fairly tough divisor. (timers decrement almost every instruction)
	{
		UINT16 decr = cpustate->timer_cnt >>2;
		cpustate->timer_cnt &= 3;

		int i;
		for (i=0; i<3; i++)
		{
			if(!(cpustate->ctcr[i] & Z280_CTCR_CT) // timer mode; note: counter mode is not implemented
			   && (cpustate->ctcsr[i] & (Z280_CTCSR_EN | Z280_CTCSR_GT)) == (Z280_CTCSR_EN | Z280_CTCSR_GT)) // timer and gate enabled
			{
				UINT16 old = cpustate->ctctr[i];
				if (i != 1 || !timer_linking) // CT0,2 always decrement. CT1 only if not linked
				{
					cpustate->ctctr[i] -= decr;
				}

				if(!cpustate->ctctr[i] || (old && cpustate->ctctr[i] > old)) // decremented to 0 or passed through 0
				{
					terminal_count(cpustate, i);
				}
				if ((cpustate->ctcr[i] & Z280_CTCR_CS) && (!old || cpustate->ctctr[i] > old)) // continuous mode - decremented from 0 or passed through 0
				{
					z280_reload_timer(cpustate, i);
				}
			}
		}
	}
}

// helper function to calculate UART baud rate
UINT32 get_brg_const_z280(struct z280_device *d)
{
	struct z280_state *cpustate = get_safe_token(d);
	if ( timer_linking )
		return ((UINT32)cpustate->cttcr[0] | ((UINT32)cpustate->cttcr[1] << 16)) + 1;
	else
		return (UINT32)cpustate->cttcr[1] + 1;
}

int check_interrupts(struct z280_state *cpustate)
{
	int i;
	int cycles = 0;

	/* scan external interrupts */
	cpustate->int_pending[Z280_INT_IRQ0] = cpustate->irq_state[INPUT_LINE_IRQ0];
	cpustate->int_pending[Z280_INT_IRQ1] = cpustate->irq_state[INPUT_LINE_IRQ1];
	cpustate->int_pending[Z280_INT_IRQ2] = cpustate->irq_state[INPUT_LINE_IRQ2];

	/* check for NMI */
	if (cpustate->int_pending[Z280_INT_NMI])
	{
		cycles += take_interrupt(cpustate, Z280_INT_NMI);
		cpustate->int_pending[Z280_INT_NMI] = 0;
	}

    /* check for interrupts */
	else if ((cpustate->cr[Z280_MSR]&Z280_MSR_IREMASK) && !cpustate->after_EI)
	{
		/* check for pending interrupts */
		for (i = Z280_INT_IRQ0; i <= Z280_INT_MAX; i++)
			if (cpustate->int_pending[i] && (MSR(cpustate) & (1<<interrupt_group[i])))
			{
				cycles += take_interrupt(cpustate, i);
				//cpustate->int_pending[i] = 0;
				break;
			}
	}

	return cycles;
}

/****************************************************************************
 * Execute 'cycles' T-states. Return number of T-states really executed
 ****************************************************************************/
void cpu_execute_z280(device_t *device, int icount)
{
	struct z280_state *cpustate = get_safe_token(device);
	int curcycles;
	cpustate->icount = icount;

	while (cpustate->icount > 0)
	{
		// DMA
		curcycles = z280_check_dma(cpustate);
		//cpustate->icount -= curcycles;
		//clock_timers(cpustate, curcycles);

		// interrupts
		curcycles += check_interrupts(cpustate);
		//cpustate->icount -= curcycles;
		//clock_timers(cpustate, curcycles);
		cpustate->after_EI = 0;

		// debugger hook
		cpustate->_PPC = cpustate->_PCD;
		z280_debug(device, cpustate->_PCD);

		// instructon fetch
		if (!cpustate->HALT)
		{
			//cpustate->R++;
			if (MSR(cpustate)&Z280_MSR_SSP)
			{
				MSR(cpustate) &= ~Z280_MSR_SSP;
				curcycles += take_trap(cpustate, Z280_TRAP_SS);
			}
			else
			{
				MSR(cpustate) = (MSR(cpustate)&Z280_MSR_SS)? (MSR(cpustate)|Z280_MSR_SSP) : (MSR(cpustate)&~Z280_MSR_SSP);
				if (setjmp(cpustate->abort_handler) == 0)
				{
					// try to execute the instruction
					cpustate->extra_cycles = 0;
					curcycles += exec_op(cpustate,ROP(cpustate));
					curcycles += cpustate->extra_cycles;
				}
				else if (cpustate->abort_type == Z280_ABORT_ACCV)
				{
					curcycles += take_trap(cpustate, Z280_TRAP_ACCV);
				}
				else
				{
					curcycles += take_fatal(cpustate);
				}
			}
		}
		else
			curcycles += 3;

		cpustate->icount -= curcycles;
		clock_timers(cpustate, curcycles);
	}

	//cpustate->old_icount -= cpustate->icount;
}

/****************************************************************************
 * Burn 'cycles' T-states. Adjust R register for the lost time
 ****************************************************************************/
/*void cpu_burn_z280(device_t *device, int cycles)
{
	/ FIXME: This is not appropriate for dma /
	struct z280_state *cpustate = get_safe_token(device);
	while ( (cycles > 0) )
	{
		clock_timers(cpustate, 3);
		/ NOP takes 3 cycles per instruction /
		cpustate->R += 1;
		cpustate->icount -= 3;
		cycles -= 3;
	}
}*/

/****************************************************************************
 * Set IRQ line state
 ****************************************************************************/
void set_irq_line(struct z280_state *cpustate, int irqline, int state)
{
	if (irqline == INPUT_LINE_NMI)
	{
		/* mark an NMI pending on the rising edge */
		if (cpustate->nmi_state == CLEAR_LINE && state != CLEAR_LINE)
			cpustate->nmi_pending = 1;
		cpustate->nmi_state = state;
	}
	else
	{
		LOG("Z280 '%s' set_irq_line %d = %d\n",cpustate->device->m_tag , irqline,state);

		/* update the IRQ state */
		cpustate->irq_state[irqline] = state;
		if (cpustate->daisy != NULL)
			cpustate->irq_state[0] = z80_daisy_chain_update_irq_state(cpustate->daisy);

		/* the main execute loop will take the interrupt */
	}
}

// external setter
// call this from the board
void z280_set_irq_line(device_t *device, int irqline, int state) {
	struct z280_state *cpustate = get_safe_token(device);
	set_irq_line(cpustate,irqline,state);
}

// internal peripherals irq lines setter
// DO NOT call this from the board
void set_irq_internal(device_t *device, int irq, int state) {
	struct z280_state *cpustate = get_safe_token(device);
	cpustate->int_pending[irq] = state;
}

/****************************************************************************
 * Set RDY line state
 ****************************************************************************/
void set_rdy_line(struct z280_state *cpustate, int rdyline, int state)
{
	cpustate->rdy_state[rdyline] = state;
}

// external setter
// call this from the board
void z280_set_rdy_line(device_t *device, int rdyline, int state) {
	struct z280_state *cpustate = get_safe_token(device);
	set_rdy_line(cpustate,rdyline,state);
}



/* logical to physical address translation (for debugger purposes) */
int cpu_translate_z280(device_t *device, enum address_spacenum space, int intention, offs_t *address)
{
	struct z280_state *cpustate = get_safe_token(device);
	*address = MMU_REMAP_ADDR_DBG(cpustate, *address, space==AS_PROGRAM?1:0);
	return TRUE;
}


/**************************************************************************
 * STATE IMPORT/EXPORT
 **************************************************************************/

/*
void cpu_state_import_z280(device_t *device, int device_state_entry)
{
	struct z280_state *cpustate = get_safe_token(device);

	switch (device_state_entry)
	{
		case Z280_R:
			cpustate->R = cpustate->rtemp;
			break;

		default:
			logerror("CPU_IMPORT_STATE(z280) called for unexpected value\n");
			break;
	}
}


void cpu_state_export_z280(device_t *device, int device_state_entry)
{
	struct z280_state *cpustate = get_safe_token(device);

	switch (device_state_entry)
	{
		case Z280_R:
			cpustate->rtemp = cpustate->R;
			break;

		default:
			logerror("CPU_EXPORT_STATE(z280) called for unexpected value\n");
			break;
	}
}*/

void cpu_string_export_z280(device_t *device, int device_state_entry, char *string)
{
	struct z280_state *cpustate = get_safe_token(device);

	switch (device_state_entry)
	{
		case STATE_GENFLAGS:
			sprintf(string,"%c%c%c%c%c%c",
				cpustate->AF.b.l & 0x80 ? 'S':'.',
				cpustate->AF.b.l & 0x40 ? 'Z':'.',
				//cpustate->AF.b.l & 0x20 ? '5':'.',
				cpustate->AF.b.l & 0x10 ? 'H':'.',
				//cpustate->AF.b.l & 0x08 ? '3':'.',
				cpustate->AF.b.l & 0x04 ? 'P':'.',
				cpustate->AF.b.l & 0x02 ? 'N':'.',
				cpustate->AF.b.l & 0x01 ? 'C':'.');
			break;
	}
}

offs_t cpu_get_state_z280(device_t *device, int device_state_entry) {

	struct z280_state *cpustate = get_safe_token(device);

	switch (device_state_entry)
	{
		case Z280_PC: return cpustate->PC.w.l;
		case STATE_GENPC: return cpustate->_PCD;
		case STATE_GENPCBASE: return cpustate->PREPC.w.l;
		case Z280_SP: return _SPD(cpustate);
		case Z280_USP: return cpustate->USP.w.l;
		case Z280_SSP: return cpustate->SSP.w.l;
		case STATE_GENSP: return _SP(cpustate);
		case STATE_GENFLAGS: return cpustate->_F;
		case Z280_A: return cpustate->_A;
		case Z280_B: return cpustate->_B;
		case Z280_C: return cpustate->_C;
		case Z280_D: return cpustate->_D;
		case Z280_E: return cpustate->_E;
		case Z280_H: return cpustate->_H;
		case Z280_L: return cpustate->_L;
		case Z280_AF: return cpustate->AF.w.l;
		case Z280_BC: return cpustate->BC.w.l;
		case Z280_DE: return cpustate->DE.w.l;
		case Z280_HL: return cpustate->HL.w.l;
		case Z280_IX: return cpustate->IX.w.l;
		case Z280_IY: return cpustate->IY.w.l;
		case Z280_AF2: return cpustate->AF2.w.l;
		case Z280_BC2: return cpustate->BC2.w.l;
		case Z280_DE2: return cpustate->DE2.w.l;
		case Z280_HL2: return cpustate->HL2.w.l;
		case Z280_R: return cpustate->R;
		case Z280_I: return cpustate->I;
		case Z280_IM: return cpustate->IM &0x3;
		case Z280_IFF2: return cpustate->IFF2;
		case Z280_HALT: return cpustate->HALT &0x1;
		case Z280_CR_MSR: return MSR(cpustate);

		default:
			return 0;
	}
}

/**************************************************************************
 * Generic set_info
 **************************************************************************/

/*static CPU_SET_INFO( z280 )
{
	struct z280_state *cpustate = get_safe_token(device);
	switch (state)
	{
		/ --- the following bits of info are set as 64-bit signed integers --- /
		case CPUINFO_INT_INPUT_STATE + INPUT_LINE_NMI:  set_irq_line(cpustate, INPUT_LINE_NMI, info->i);    break;
		case CPUINFO_INT_INPUT_STATE + Z280_IRQ0:       set_irq_line(cpustate, Z280_IRQ0, info->i);         break;
		case CPUINFO_INT_INPUT_STATE + Z280_IRQ1:       set_irq_line(cpustate, Z280_IRQ1, info->i);         break;
		case CPUINFO_INT_INPUT_STATE + Z280_IRQ2:       set_irq_line(cpustate, Z280_IRQ2, info->i);         break;

		/ --- the following bits of info are set as pointers to data or functions --- /
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_op:      cpustate->cc[Z280_TABLE_op] = (UINT8 *)info->p;     break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_cb:      cpustate->cc[Z280_TABLE_cb] = (UINT8 *)info->p;     break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_ed:      cpustate->cc[Z280_TABLE_ed] = (UINT8 *)info->p;     break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_xy:      cpustate->cc[Z280_TABLE_xy] = (UINT8 *)info->p;     break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_xycb:    cpustate->cc[Z280_TABLE_xycb] = (UINT8 *)info->p;   break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_ex:      cpustate->cc[Z280_TABLE_ex] = (UINT8 *)info->p;     break;
	}
}*/


/**************************************************************************
 * Generic get_info
 **************************************************************************/

/*CPU_GET_INFO( z280 )
{
	struct z280_state *cpustate = (device != NULL && device->token() != NULL) ? get_safe_token(device) : NULL;
	switch (state)
	{
		/ --- the following bits of info are returned as 64-bit signed integers --- /
		case CPUINFO_INT_CONTEXT_SIZE:                  info->i = sizeof(z280_state);           break;
		case CPUINFO_INT_INPUT_LINES:                   info->i = 3;                            break;
		case CPUINFO_INT_DEFAULT_IRQ_VECTOR:            info->i = 0xff;                         break;
		case CPUINFO_INT_ENDIANNESS:                    info->i = ENDIANNESS_LITTLE;            break;
		case CPUINFO_INT_CLOCK_MULTIPLIER:              info->i = 1;                            break;
		case CPUINFO_INT_CLOCK_DIVIDER:                 info->i = 1;                            break;
		case CPUINFO_INT_MIN_INSTRUCTION_BYTES:         info->i = 1;                            break;
		case CPUINFO_INT_MAX_INSTRUCTION_BYTES:         info->i = 4;                            break;
		case CPUINFO_INT_MIN_CYCLES:                    info->i = 1;                            break;
		case CPUINFO_INT_MAX_CYCLES:                    info->i = 16;                           break;

		case CPUINFO_INT_DATABUS_WIDTH + AS_PROGRAM:            info->i = 8;                            break;
		case CPUINFO_INT_ADDRBUS_WIDTH + AS_PROGRAM:        info->i = 20;                           break;
		case CPUINFO_INT_ADDRBUS_SHIFT + AS_PROGRAM:        info->i = 0;                            break;
		case CPUINFO_INT_DATABUS_WIDTH + AS_IO:             info->i = 8;                            break;
		case CPUINFO_INT_ADDRBUS_WIDTH + AS_IO:             info->i = 16;                           break;
		case CPUINFO_INT_ADDRBUS_SHIFT + AS_IO:             info->i = 0;                            break;

		case CPUINFO_INT_INPUT_STATE + INPUT_LINE_NMI:  info->i = cpustate->nmi_state;          break;
		case CPUINFO_INT_INPUT_STATE + Z280_IRQ0:       info->i = cpustate->irq_state[0];       break;
		case CPUINFO_INT_INPUT_STATE + Z280_IRQ1:       info->i = cpustate->irq_state[1];       break;
		case CPUINFO_INT_INPUT_STATE + Z280_IRQ2:       info->i = cpustate->irq_state[2];       break;

		/ --- the following bits of info are returned as pointers --- /
		case CPUINFO_FCT_SET_INFO:      info->setinfo = CPU_SET_INFO_NAME(z280);                break;
		case CPUINFO_FCT_INIT:          info->init = CPU_INIT_NAME(z280);                       break;
		case CPUINFO_FCT_RESET:         info->reset = CPU_RESET_NAME(z280);                     break;
		case CPUINFO_FCT_EXECUTE:       info->execute = CPU_EXECUTE_NAME(z280);                 break;
		case CPUINFO_FCT_BURN:          info->burn = CPU_BURN_NAME(z280);                       break;
		case CPUINFO_FCT_DISASSEMBLE:   info->disassemble = CPU_DISASSEMBLE_NAME(z280);         break;
		case CPUINFO_FCT_TRANSLATE:     info->translate = CPU_TRANSLATE_NAME(z280);             break;
		case CPUINFO_FCT_IMPORT_STATE:  info->import_state = CPU_IMPORT_STATE_NAME(z280);       break;
		case CPUINFO_FCT_EXPORT_STATE:  info->export_state = CPU_EXPORT_STATE_NAME(z280);       break;
		case CPUINFO_FCT_EXPORT_STRING: info->export_string = CPU_EXPORT_STRING_NAME(z280);     break;

		/ --- the following bits of info are returned as pointers to functions --- /
		case CPUINFO_PTR_INSTRUCTION_COUNTER:           info->icount = &cpustate->icount;       break;

		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_op:      info->p = (void *)cpustate->cc[Z280_TABLE_op];  break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_cb:      info->p = (void *)cpustate->cc[Z280_TABLE_cb];  break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_ed:      info->p = (void *)cpustate->cc[Z280_TABLE_ed];  break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_xy:      info->p = (void *)cpustate->cc[Z280_TABLE_xy];  break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_xycb:    info->p = (void *)cpustate->cc[Z280_TABLE_xycb];    break;
		case CPUINFO_PTR_Z280_CYCLE_TABLE + Z280_TABLE_ex:      info->p = (void *)cpustate->cc[Z280_TABLE_ex];  break;

		/ --- the following bits of info are returned as NULL-terminated strings --- /
		case CPUINFO_STR_NAME:                          strcpy(info->s, "Z280");                break;
		case CPUINFO_STR_SHORTNAME:                     strcpy(info->s, "z280");                break;
		case CPUINFO_STR_FAMILY:                    strcpy(info->s, "Zilog Z8x280");        break;
		case CPUINFO_STR_VERSION:                   strcpy(info->s, "0.1");                 break;
		case CPUINFO_STR_SOURCE_FILE:                       strcpy(info->s, __FILE__);              break;
	}
}*/

//DEFINE_LEGACY_CPU_DEVICE(Z280, z280);
