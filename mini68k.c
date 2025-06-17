/*
 *	John Coffman's
 *	RBC Mini68K + MF/PIC board with PPIDE
 *
 *	68008 CPU @8MHz 0 or 1 ws I/O 1 or 2 ws
 *	NS32202 interrupt controller
 *	512K to 2MB RAM
 *	128-512K flash ROM
 *	4MB expanded paged memory option
 *	Autvectored interrupts off the MF/PIC
 *
 *	Mapping
 *	000000-1FFFFF	SRAM
 *	200000-2FFFFF	Banked RAM window
 *	300000-37FFFF	Off board
 *	380000-3EFFFF	Flash/EPROM
 *	3F0000-3FFFFF	I/O on the ECB bus
 *
 *	I/O space on the ECB
 *	0x40	MF/PIC board base (PPIDE 0x44)
 *		0x40	32202
 *		0x42	cfg
 *		0x43	rtc
 *		0x44	PPI
 *		0x48	sio	16x50
 *	0x2x	DiskIO PPIDE
 *	0x3x	DiskIO Floppy
 *	0x00	4Mem
 *	0x08	DualSD
 *
 *	Low 1K, 4K or 64K can be protected (not emulated)
 *
 *	TODO
 *	- ns202 priority logic (esp rotating)
 *	- Finish the ns202 register model
 *	- DualSD second card
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <m68k.h>
#include "serialdevice.h"
#include "ttycon.h"
#include "16x50.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "sdcard.h"
#include "lib765/include/765.h"


/* IDE controller */
static struct ppide *ppide;	/* MFPIC */
static struct ppide *ppide2;	/* DiskIO */
/* SD */
static struct sdcard *sd[2];	/* Dual SD */
/* Serial */
static struct uart16x50 *uart;
/* RTC */
static struct rtc *rtc;
static unsigned rtc_loaded;
/* FDC on DiskIO */
static FDC_PTR fdc;
static FDRV_PTR drive_a, drive_b;
/* 4M Memory card */
static unsigned memsize = 512;
static unsigned mem_4mb;
static uint8_t m4_bank[64];
static uint8_t m4_bankp;

/* 2MB RAM */
static uint8_t ram[0x200000];
/* 128K ROM */
static uint8_t rom[0x20000];
/* Force ROM into low space for the first 8 reads */
static uint8_t u27;
/* Config register on the MFPIC */
static uint8_t mfpic_cfg;
/* Memory for 4M card */
static uint8_t mem4[4][0x100000];

static int trace = 0;

#define TRACE_MEM	1
#define TRACE_CPU	2
#define TRACE_UART	4
#define TRACE_PPIDE	8
#define TRACE_RTC	16
#define TRACE_FDC	32
#define TRACE_NS202	64
#define TRACE_SD	128

uint8_t fc;

/* Read/write macros */
#define READ_BYTE(BASE, ADDR) (BASE)[ADDR]
#define READ_WORD(BASE, ADDR) (((BASE)[ADDR]<<8) | \
			(BASE)[(ADDR)+1])
#define READ_LONG(BASE, ADDR) (((BASE)[ADDR]<<24) | \
			((BASE)[(ADDR)+1]<<16) | \
			((BASE)[(ADDR)+2]<<8) | \
			(BASE)[(ADDR)+3])

#define WRITE_BYTE(BASE, ADDR, VAL) (BASE)[ADDR] = (VAL)&0xff
#define WRITE_WORD(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>8) & 0xff; \
			(BASE)[(ADDR)+1] = (VAL)&0xff
#define WRITE_LONG(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>24) & 0xff; \
			(BASE)[(ADDR)+1] = ((VAL)>>16)&0xff; \
			(BASE)[(ADDR)+2] = ((VAL)>>8)&0xff; \
			(BASE)[(ADDR)+3] = (VAL)&0xff


/* Hardware emulation */

struct ns32202 {
	uint8_t reg[32];
	uint16_t ct_l, ct_h;
	unsigned live;
	unsigned irq;
#define NO_INT	0xFF
};

#define R_HVCT	0
#define R_SVCT	1
#define R_ELTG	2
#define R_TPR	4
#define R_IPND	6
#define R_ISRV	8
#define R_IMSK	10
#define R_CSRC	12
#define R_FPRT	14
#define R_MCTL	16
#define R_OCASN	17
#define R_CIPTR	18
#define R_PDAT	19
#define R_IPS	20
#define	R_PDIR	21
#define	R_CCTL	22
#define R_CICTL	23
#define R_CSV	24
#define R_CCV	28

struct ns32202 ns202;

/* Bitop helpers for reg pairs */
static int ns202_top16(unsigned r)
{
	unsigned v = ns202.reg[r] | (ns202.reg[r+1] << 8);
	int n = 15;
	while (n >= 0) {
		if (v & (1 << n))
			return n;
		n--;
	}
	return -1;
}

static unsigned ns202_test16(unsigned r, unsigned n)
{
	if (n >= 8)
		return !!(ns202.reg[r + 1] & (1 << (n - 8)));
	return !!(ns202.reg[r] & (1 << n));
}

static void ns202_set16(unsigned r, unsigned n)
{
	if (n >= 8)
		ns202.reg[r + 1] |= 1 << (n - 8);
	else
		ns202.reg[r] |= 1 << n;
}

static void ns202_clear16(unsigned r, unsigned n)
{
	if (n >= 8)
		ns202.reg[r + 1] &= ~(1 << (n - 8));
	else
		ns202.reg[r] &= ~(1 << n);
}

/* TODO - rotating priority */
/* FIXME: is prio low - hi or hi - low */
static int ns202_hipri(void)
{
	int n = 15;
	while(n >= 0) {
		if (ns202_test16(R_IPND, n) && !ns202_test16(R_IMSK, n))
			return n;
		n--;
	}
	return -1;
}

/* Find the highest interrupt priority and if it is higher than the
   current highest priority then set irq and remember it
   TODO: set HSRV ?? */
static void ns202_compute_int(void)
{
	/* Find the highest interrupt that isn't masked */
	int n = ns202_hipri();
	int t = ns202_top16(R_ISRV);
	if (n == -1)
		return;
	/* TODO: rotating mode */
	if (ns202.live == NO_INT || n > t) {
		/* We have a new winner for topmost interrupt */
		ns202.irq = 1;
		ns202.live = n;
		ns202.reg[R_HVCT] &= 0xF0;
		ns202.reg[R_HVCT] |= n;
//		if (trace & TRACE_NS202)
//			fprintf(stderr, "ns202: interrupt %d\n", n);
	}
}

/* We had int raised (hopefully) and the CPU acked it */
static unsigned ns202_int_ack(void)
{
	unsigned live = ns202.live;
	/* The IPND for the active interrupt is cleared, the corresponding
	   bit in the ISRV is set. We don't model cascaded ICU */
	ns202_clear16(R_IPND, live);
	ns202_set16(R_ISRV, live);
	if (trace & TRACE_NS202)
		fprintf(stderr, "ns202: intack %d\n", live);
	/* And the interrupt is dropped */
	ns202.irq = 0;
	/* Clear the host IRQ as well : HACK - abstract this */
	m68k_set_irq(0);
	/* Check if there isn't now a higher priority into to interrupt the
	   interrupt */
	ns202_compute_int();
	live |= ns202.reg[R_HVCT] & 0xF0;
	if (trace & TRACE_NS202)
		fprintf(stderr, "ns202: intack vector %02X\n", live);
	return live;
}

/* RETI or equivalent occurred. */
static void ns202_clear_int(void)
{
	unsigned live = ns202.live;
	if (live == NO_INT)
		return;
	/* Guesswork - seems the ACK clears the counter flag, but what
	   about error bit ? */
	if (live == (ns202.reg[R_CIPTR] & 0x0F))
		ns202.reg[R_CICTL] &= 0xFB;
	if (live == ns202.reg[R_CIPTR] >> 4)
		ns202.reg[R_CICTL] &= 0xBF;
	ns202.reg[R_HVCT] |= 0x0F;
	/* Clear the live interrupt in ISRV */
	ns202_clear16(R_ISRV, live);
	if (trace & TRACE_NS202)
		fprintf(stderr, "ns202: int clear %d\n", live);
	ns202.live = NO_INT;
	/* Check if there is anything pending to cause a next interrupt */
	ns202_compute_int();
}

/* TODO: emulate mis-setting 8 v 16bit mode */
static unsigned int do_ns202_read(unsigned int address)
{
	unsigned ns32_reg = (address >> 8) & 0x1F;
	unsigned ns32_sti = (address >> 8) & 0x20;

	switch(ns32_reg) {
	case R_HVCT:
		/* INTA or RETI cycle */
		if (ns32_sti)	/* RETI */
			ns202_clear_int();
		else		/* INTA */
			ns202_int_ack();
		if (ns32_sti)
			return ns202.reg[R_HVCT]|0x0F;
		return ns202.reg[R_HVCT];
	case R_SVCT:
//		ns32_hvct_recalc();
		return ns202.reg[R_HVCT];
	case R_FPRT:
		if (ns202.reg[R_FPRT] < 8)
			return 1 << ns202.reg[R_FPRT];
		return 0;
	case R_FPRT + 1:
		if (ns202.reg[R_FPRT] >= 8)
			return 1 << (ns202.reg[R_FPRT] - 8);
		return 0;
	case R_CCV:
	case R_CCV + 1:
	case R_CCV + 2:
	case R_CCV + 3:
		/* The CCV can only be read when counter readings are
		   frozen, but the documentation says nothing about what
		   happens otherwise, so ignore this TODO */
	case R_TPR:
	case R_TPR + 1:
	case R_ELTG:
	case R_ELTG + 1:
	case R_IPND:
	case R_IPND + 1:
	case R_CSRC:
	case R_CSRC + 1:
	case R_ISRV:
	case R_ISRV + 1:
	case R_IMSK:
	case R_IMSK + 1:
	case R_MCTL:
	case R_OCASN:
	case R_CIPTR:
	case R_PDAT:
		/* We assume no input GPIO */
	case R_IPS:
	case R_PDIR:
	case R_CCTL:
	case R_CICTL:
	case R_CSV:
	case R_CSV + 1:
	case R_CSV + 2:
	case R_CSV + 3:
	default:
		return ns202.reg[ns32_reg];
	}
}

unsigned int ns202_read(unsigned int address)
{
	unsigned r = do_ns202_read(address);
	if (trace & TRACE_NS202)
		fprintf(stderr, "ns202_read %06X[%-2d] = %02X\n", address,
				(address >> 8) & 0x1F, r);
	return r;
}

void ns202_write(unsigned int address, unsigned int value)
{
	unsigned ns32_reg = (address >> 8) & 0x1F;
//	unsigned ns32_sti = (address >> 8) & 0x20;

	if (trace & TRACE_NS202)
		fprintf(stderr, "ns202_write %06X[%-2d] = %02X\n", address, ns32_reg, value);
	switch(ns32_reg) {
	case R_HVCT:
		break;
	case R_SVCT:
		ns202.reg[R_HVCT] &= 0x0F;
		ns202.reg[R_HVCT] |= (value & 0xF0);
		break;
	/* TODO: IPND write is special forms */
	case R_IPND:
	case R_IPND + 1:
		break;
	case R_FPRT:
		ns202.reg[R_FPRT] = value & 0x0F;
		break;
	case R_FPRT + 1:
		/* Not writeable */
		break;
	case R_CCTL:
		/* Never see CDCRL or CDCRH 1 */
		ns202.reg[ns32_reg] &= 0xFC;
		ns202.reg[ns32_reg] = value;
		/* Need to process single cycle decrementer here TODO */
		break;
	case R_CICTL:
		if (value & 0x01) {
			ns202.reg[ns32_reg] &= 0xF0;
			ns202.reg[ns32_reg] |= value & 0x0F;
		}
		if (value & 0x10) {
			ns202.reg[ns32_reg] &= 0x0F;
			ns202.reg[ns32_reg] |= value & 0xF0;
		}
		break;
	case R_CCV:
	case R_CCV + 1:
	case R_CCV + 2:
	case R_CCV + 3:
	/* Just adjust the register */
	case R_TPR:
	case R_TPR + 1:
	case R_ELTG:
	case R_ELTG + 1:
	case R_ISRV:
	case R_ISRV + 1:
	case R_IMSK:
	case R_IMSK + 1:
	case R_CSRC:
	case R_CSRC + 1:
	case R_MCTL:
	case R_OCASN:
	case R_CIPTR:
	case R_PDAT:
		/* We assume no output GPIO activity */
	case R_IPS:
	case R_PDIR:
	case R_CSV:
	case R_CSV + 1:
	case R_CSV + 2:
	case R_CSV + 3:
	default:
		ns202.reg[ns32_reg] = value;
	}
	ns202_compute_int();
}

void ns202_raise(unsigned irq)
{
	if (ns202.reg[R_MCTL] & 0x08)	/* FRZ */
		return;
	ns202_set16(R_IPND, irq);
	ns202_compute_int();
}

void ns202_counter(void)
{
	uint8_t cctl = ns202.reg[R_CCTL];
	uint8_t cictl = ns202.reg[R_CICTL];
	/* Split clocks, low enabled */
	if ((cctl & 0x84) == 0x04) {
		/* Low counter decrement */
		ns202.ct_l--;
		if (ns202.ct_l == 0) {
			if (cictl & 0x04)
				cictl |= 0x08;
			else
				cictl |= 0x04;
			ns202.ct_l = ns202.reg[R_CSV];
			ns202.ct_l |= ns202.reg[R_CSV + 1] << 8;
//			if (trace & TRACE_NS202)
//				fprintf(stderr, "ns202: low counter 0 reload %d.\n", ns202.ct_l);
		}
	}
	if ((cctl & 0x88) == 0x08) {
		/* High counter decrement */
		ns202.ct_h--;
		if (ns202.ct_h == 0) {
			if (cictl & 0x40)
				cictl |= 0x80;
			else
				cictl |= 0x40;
			ns202.ct_h = ns202.reg[R_CSV + 2];
			ns202.ct_h |= ns202.reg[R_CSV + 3] << 8;
//			if (trace & TRACE_NS202)
//				fprintf(stderr, "ns202: high counter 0 reload %d.\n", ns202.ct_h);
		}
	}
	ns202.reg[R_CICTL] = cictl;
	if ((cctl & 0x88) == 0x88) {
		/* 32bit decrement */
		if (ns202.ct_l == 0) {
			ns202.ct_h--;
			if (ns202.ct_h == 0) {
				if (cictl & 0x40)
					cictl |= 0x80;
				else
					cictl |= 0x40;
				ns202.ct_l = ns202.reg[R_CSV];
				ns202.ct_l |= ns202.reg[R_CSV + 1] << 8;
				ns202.ct_h = ns202.reg[R_CSV + 2];
				ns202.ct_h |= ns202.reg[R_CSV + 3] << 8;
//				if (trace & TRACE_NS202)
//					fprintf(stderr, "ns202: dual counter 0.\n");
			}
		}
		ns202.ct_l--;
	}
	/* Raise interrupts as needed */
	if ((cictl & 0x60) == 0x60) {
		ns202_raise(ns202.reg[R_CIPTR] >> 4);
	}
	if ((cictl & 0x06) == 0x06) {
		ns202_raise(ns202.reg[R_CIPTR] & 0x0F);
	}
}

void ns202_tick(unsigned clocks)
{
	static unsigned dclock;
	unsigned scale = (ns202.reg[R_CCTL] & 0x40) ? 4 : 1;

	dclock += clocks;
	while (dclock >= scale) {
		dclock -= scale;
		ns202_counter();
	}
	/* Update LCCV/HCCV if we should do so */
	if (!(ns202.reg[R_MCTL] & 0x80)) {	/* CFRZ */
		ns202.reg[28] = ns202.ct_l;
		ns202.reg[29] = ns202.ct_l >> 8;
		ns202.reg[30] = ns202.ct_h;
		ns202.reg[31] = ns202.ct_h >> 8;
	}
}

void ns202_reset(void)
{
	ns202.reg[R_IMSK] = 0xFF;
	ns202.reg[R_IMSK + 1] = 0xFF;
	ns202.reg[R_CIPTR] = 0xFF;
	ns202.live = NO_INT;
}

static unsigned int irq_pending;

void recalc_interrupts(void)
{
	if (uart16x50_irq_pending(uart))
		ns202_raise(12);
	/* TODO: which IRQ */
	if (ns202.irq){
//		if (trace & TRACE_NS202)
//			fprintf(stderr,  "IRQ raised\n");
		m68k_set_irq(M68K_IRQ_2);
	} else
		m68k_set_irq(0);
}


static unsigned int cfg_read(void)
{
	return mfpic_cfg;
}

static void cfg_write(unsigned int value)
{
	/* 7-3 user */
	/* Bit 2 masks upper 8 interrupts */
	/* 1:0 shift value for interrupt vector */
	mfpic_cfg = value;
}

/* Remap the bits as the MF/PIC doesn't follow the usual RBC/RC2014 mapping */

static unsigned rtc_remap_w(unsigned v)
{
	unsigned r = 0;
	if (v & 1)		/* Data / Data */
		r |= 0x80;
	if (!(v & 2))		/* Write / /Write */
		r |= 0x20;
	if (v & 4)		/* Clock / Clock */
		r |= 0x40;
	if (!(v & 8))		/* Reset / /Reset */
		r |= 0x10;
	return r;
}

static unsigned rtc_remap_r(unsigned v)
{
	unsigned r = 0;
	if (v & 0x01)		/* Data in */
		r |= 0x01;
	return r;
}

/*
 *	Dual SD. Simple bitbang SD card. Nothing fancy
 *	not even a write/read driven clock
 *
 *	Interrupt not currently emulated. Currently we hardcode
 *	a card in slot 0 and none in slot 1
 */

static uint8_t dsd_op;
static uint8_t dsd_sel = 0x10;
static uint8_t dsd_sel_w;
static uint8_t dsd_rx, dsd_tx;
static uint8_t dsd_bitcnt;
static uint8_t dsd_bit;

/* Rising edge: Value sampling */
static void dualsd_clock_high(void)
{
	dsd_rx <<= 1;
	dsd_rx |= dsd_op & 1;
	dsd_bitcnt++;
	if (dsd_bitcnt == 8) {
		dsd_tx = sd_spi_in(sd[0], dsd_rx);
		if (trace & TRACE_SD)
			fprintf(stderr, "sd: sent %02X got %02x\n", dsd_rx, dsd_tx);
		dsd_bitcnt = 0;
	}
}

/* Falling edge: Values change */
static void dualsd_clock_low(void)
{
	dsd_bit = (dsd_tx & 0x80) ? 1: 0;
	dsd_tx <<= 1;
}

static void dualsd_write(unsigned addr, unsigned val)
{
	static unsigned delta;
	if (sd[0] == NULL)
		return;
	if (trace & TRACE_SD)
		fprintf(stderr, "dsd_write %d %02X\n", addr & 1, val);
	if (addr & 1) {
		dsd_sel_w = val;
		dsd_sel &= 0xFE;
		dsd_sel |= val & 0x01;
		/* IRQ logic not yet emulated TODO */
	} else {
		delta = dsd_op ^ val;
		dsd_op = val;
		/* Only doing card 0 for now */
		if ((dsd_sel & 1) == 0) {
			if (delta & 0x04) {
				if (val & 0x04) {
					sd_spi_lower_cs(sd[0]);
					dsd_bitcnt = 0;
				} else
					sd_spi_raise_cs(sd[0]);
			}
			if (delta & 0x02) {
				if (val & 0x02)
					dualsd_clock_high();
				else
					dualsd_clock_low();
			}
		}
	}
}

static unsigned do_dualsd_read(unsigned addr)
{
	if (sd[0] == NULL)
		return 0xFF;
	if (addr & 1)
		return dsd_sel;
	else {
		/* For now we just fake card 1 absent, 0 rw present */
		if (dsd_sel & 1)
			return (dsd_op & 0x06);
		else
			return (dsd_op & 0x06) | 0x20 | dsd_bit;
	}
}

static unsigned dualsd_read(unsigned addr)
{
	unsigned val = do_dualsd_read(addr);
	if (trace & TRACE_SD)
		fprintf(stderr, "dsd_read %d %02X\n", addr & 1, val);
	return val;
}


/* FDC: TC not connected ? */
static void fdc_log(int debuglevel, char *fmt, va_list ap)
{
	if ((trace & TRACE_FDC) || debuglevel == 0)
		vfprintf(stderr, "fdc: ", ap);
}

static void fdc_write(uint8_t addr, uint8_t val)
{
	if (addr & 0x08)
		fdc_write_dor(fdc, val);
	else if (addr & 1)
		fdc_write_data(fdc, val);
}

static uint8_t fdc_read(uint8_t addr)
{
	if (addr & 0x08)
		return fdc_read_dir(fdc);
	else if (addr & 1)
		return fdc_read_data(fdc);
	else
		return fdc_read_ctrl(fdc);
}

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

/* Read data from RAM, ROM, or a device */
unsigned int do_cpu_read_byte(unsigned int address, unsigned debug)
{
	address &= 0x3FFFFF;
	if (!(u27 & 0x80)) {
		if (debug == 0) {
			u27 <<= 1;
			u27 |= 1;
		}
		return rom[address & 0x1FFFF];
	}
	if (debug == 0) {
		u27 <<= 1;
		u27 |= 1;
	}
	if (address < 0x200000) {
		if (address < memsize)
			return ram[address];
		else
			return 0xFF;
	}
	if (address < 0x30000) {
		if (mem_4mb) {
			unsigned r = (address >> 14) & 0x3F;
			r = m4_bank[r];
			if (r == 0xFF)
				return 0xFF;
			return mem4[m4_bank[r]][address & 0x3FFF];
		}
		return 0xFF;
	}
	if (address < 0x380000)
		return 0xFF;
	if (address < 0x3F0000)
		return rom[address & 0x1FFFF];
	/* I/O space */
	/* Disassembler doesn't trigger I/O side effects */
	if (debug)
		return 0xFF;
	address &= 0xFFFF;
	if ((address & 0xF0) == 0x20)
		return ppide_read(ppide2, address & 0x03);
	if ((address & 0xF0) == 0x30)
		return fdc_read(address);
	switch(address & 0xFF) {
	case 0x08:
	case 0x09:
		return dualsd_read(address);
	case 0x40:
		return ns202_read(address);
	case 0x42:
		return cfg_read();
	case 0x43:
		return rtc_remap_r(rtc_read(rtc));
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
		return ppide_read(ppide, address & 0x03);
	case 0x48:
	case 0x49:
	case 0x4A:
	case 0x4B:
	case 0x4C:
	case 0x4D:
	case 0x4E:
	case 0x4F:
		return uart16x50_read(uart, address & 0x07);
	}
	return 0xFF;
}

unsigned int cpu_read_byte(unsigned int address)
{
	unsigned int v = do_cpu_read_byte(address, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RB %06X -> %02X\n", address, v);
	return v;
}

unsigned int do_cpu_read_word(unsigned int address, unsigned int debug)
{
	return (do_cpu_read_byte(address, debug) << 8) | do_cpu_read_byte(address + 1, debug);
}

unsigned int cpu_read_word(unsigned int address)
{
	unsigned int v = do_cpu_read_word(address, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RW %06X -> %04X\n", address, v);
	return v;
}

unsigned int cpu_read_word_dasm(unsigned int address)
{
	return do_cpu_read_word(address, 1);
}

unsigned int cpu_read_long(unsigned int address)
{
	return (cpu_read_word(address) << 16) | cpu_read_word(address + 2);
}

unsigned int cpu_read_long_dasm(unsigned int address)
{
	return (cpu_read_word_dasm(address) << 16) | cpu_read_word_dasm(address + 2);
}

void cpu_write_byte(unsigned int address, unsigned int value)
{
	address &= 0x3FFFFF;
	if (!(u27 & 0x80)) {
		u27 <<= 1;
		u27 |= 1;
		return;
	}
	u27 <<= 1;
	u27 |= 1;
	if (address < memsize) {
		ram[address] = value;
		return;
	}
	if (address >= 0x30000 && address <= 0x380000 && mem_4mb) {
		unsigned r = (address >> 14) & 0x3F;
		r = m4_bank[r];
		if (r == 0xFF)
			return;
		mem4[r][address & 0x3FFF] = value;
		return;
	}
	if (address < 0x3F0000) {
		if (trace & TRACE_MEM)
			fprintf(stderr,  "%06x: write to invalid space.\n", address);
		return;
	}
	if ((address & 0xF0) == 0x20) {
		ppide_write(ppide2, address & 0x03, value);
		return;
	}
	if ((address & 0xF0) == 0x30) {
		fdc_write(address, value);
		return;
	}
	switch(address & 0xFF) {
	/* 4MEM : 00 for now*/
	case 0x00:
		m4_bankp = value;
		return;
	case 0x01:
		m4_bank[m4_bankp] = value;
		return;
	/* DualSD */
	case 0x08:
	case 0x09:
		dualsd_write(address, value);
		return;
	/* MFPIC */
	case 0x40:
		ns202_write(address & 0xFFFF, value);
		return;
	case 0x42:
		cfg_write(value);
		return;
	case 0x43:
		rtc_write(rtc, rtc_remap_w(value));
		return;
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
		ppide_write(ppide, address & 0x03, value);
		return;
	case 0x48:
	case 0x49:
	case 0x4A:
	case 0x4B:
	case 0x4C:
	case 0x4D:
	case 0x4E:
	case 0x4F:
		uart16x50_write(uart, address & 0x07, value);
		return;
	}
}

void cpu_write_word(unsigned int address, unsigned int value)
{
	address &= 0xFFFFFF;

	if (trace & TRACE_MEM)
		fprintf(stderr, "WW %06X <- %04X\n", address, value);

	cpu_write_byte(address, value >> 8);
	cpu_write_byte(address + 1, value & 0xFF);
}

void cpu_write_long(unsigned int address, unsigned int value)
{
	address &= 0xFFFFFF;

	cpu_write_word(address, value >> 16);
	cpu_write_word(address + 2, value & 0xFFFF);
}

void cpu_write_pd(unsigned int address, unsigned int value)
{
	address &= 0xFFFFFF;

	cpu_write_word(address + 2, value & 0xFFFF);
	cpu_write_word(address, value >> 16);
}

void cpu_instr_callback(void)
{
	if (trace & TRACE_CPU) {
		char buf[128];
		unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
		m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
		fprintf(stderr, ">%06X %s\n", pc, buf);
	}
}

static void device_init(void)
{
	irq_pending = 0;
	ppide_reset(ppide);
	uart16x50_reset(uart);
	uart16x50_attach(uart, &console);
	u27 = 0;
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, 0, &saved_term);
	if (rtc_loaded)
		rtc_save(rtc, "mini68k.nvram");
	exit(1);
}

static void exit_cleanup(void)
{
	if (rtc_loaded)
		rtc_save(rtc, "mini68k.nvram");
	tcsetattr(0, 0, &saved_term);
}

static void take_a_nap(void)
{
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = 100000;
	if (nanosleep(&t, NULL))
		perror("nanosleep");
}

int cpu_irq_ack(int level)
{
	unsigned v = ns202_int_ack();
	/* Now apply the board glue */
	if (!(mfpic_cfg & 4)) {
		v &= 7;
		v |= mfpic_cfg & 0xF8;
	} else {
		v &= 0x0F;
		v |= mfpic_cfg & 0xF0;
	}
	v <<= (mfpic_cfg & 3);
	if (trace & TRACE_NS202)
		fprintf(stderr, "68K vector %02X\n", v);
	return v;
}

void cpu_pulse_reset(void)
{
	device_init();
}

void cpu_set_fc(int fc)
{
}

void usage(void)
{
	fprintf(stderr, "mini68k: [-0][-1][-2][-e][-m memsize][-r rompath][-i idepath][-I idepath] [-d debug].\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd;
	int cputype = M68K_CPU_TYPE_68000;
	int fast = 0;
	int opt;
	const char *romname = "mini-128.rom";
	const char *diskname = NULL;
	const char *diskname2 = NULL;
	const char *patha = NULL;
	const char *pathb = NULL;
	const char *sdname = NULL;

	while((opt = getopt(argc, argv, "012d:efi:m:r:s:A:B:I:")) != -1) {
		switch(opt) {
		case '0':
			cputype = M68K_CPU_TYPE_68000;
			break;
		case '1':
			cputype = M68K_CPU_TYPE_68010;
			break;
		case '2':
			cputype = M68K_CPU_TYPE_68020;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'e':
			cputype = M68K_CPU_TYPE_68EC020;
			break;
		case 'f':
			fast = 1;
			break;
		case 'i':
			diskname = optarg;
			break;
		case 'm':
			memsize = atoi(optarg);
			break;
		case 'r':
			romname = optarg;
			break;
		case 's':
			sdname = optarg;
			break;
		case 'A':
			patha = optarg;
			break;
		case 'B':
			pathb = optarg;
			break;
		case 'I':
			diskname2 = optarg;
			break;
		default:
			usage();
		}
	}

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, cleanup);
		signal(SIGTSTP, SIG_IGN);
		term.c_lflag &= ~ICANON;
		term.c_iflag &= ~(ICRNL | IGNCR);
		term.c_cc[VMIN] = 1;
		term.c_cc[VTIME] = 0;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VEOF] = 0;
		term.c_lflag &= ~(ECHO | ECHOE | ECHOK);
		tcsetattr(0, 0, &term);
	}

	if (optind < argc)
		usage();

	memsize <<= 10;	/* In KiB for friendlyness */
	if (memsize & 0x7FFFF) {
		fprintf(stderr, "%s: RAM must be a multiple of 512K blocks.\n",
			argv[0]);
		exit(1);
	}
	if (memsize > sizeof(ram)) {
		fprintf(stderr, "%s: RAM size must be no more than %ldKib\n",
			argv[0], (long)sizeof(ram) >> 10);
		exit(1);
	}
	memset(ram, 0xA7, sizeof(ram));

	fd = open(romname, O_RDONLY);
	if (fd == -1) {
		perror(romname);
		exit(1);
	}
	if (read(fd, rom, 0x20000) != 0x20000) {
		fprintf(stderr, "%s: too short.\n", romname);
		exit(1);
	}
	close(fd);

	ppide = ppide_create("hd0");
	ppide_reset(ppide);
	if (diskname) {
		fd = open(diskname, O_RDWR);
		if (fd == -1) {
			perror(diskname);
			exit(1);
		}
		if (ppide == NULL)
			exit(1);
		if (ppide_attach(ppide, 0, fd))
			exit(1);
	}
	ppide_trace(ppide, trace & TRACE_PPIDE);

	ppide2 = ppide_create("hd1");
	ppide_reset(ppide2);
	if (diskname2) {
		fd = open(diskname2, O_RDWR);
		if (fd == -1) {
			perror(diskname2);
			exit(1);
		}
		if (ppide2 == NULL)
			exit(1);
		if (ppide_attach(ppide2, 0, fd))
			exit(1);
	}
	ppide_trace(ppide2, trace & TRACE_PPIDE);

	if (sdname) {
		fd = open(sdname, O_RDWR);
		if (fd == -1) {
			perror(sdname);
			exit(1);
		}
		sd[0] = sd_create("sd0");
		sd[1] = sd_create("sd1");
		sd_reset(sd[0]);
		sd_reset(sd[1]);
		sd_attach(sd[0], fd);
		sd_trace(sd[0], trace & TRACE_SD);
		sd_trace(sd[1], trace & TRACE_SD);
	}

	uart = uart16x50_create();
	if (trace & TRACE_UART)
		uart16x50_trace(uart, 1);

	rtc = rtc_create();
	rtc_reset(rtc);
	rtc_trace(rtc, trace & TRACE_RTC);
	rtc_load(rtc, "mini68k.nvram");
	rtc_loaded = 1;

	fdc = fdc_new();

	lib765_register_error_function(fdc_log);

	if (patha) {
		drive_a = fd_newdsk();
		fd_settype(drive_a, FD_35);
		fd_setheads(drive_a, 2);
		fd_setcyls(drive_a, 80);
		fdd_setfilename(drive_a, patha);
	} else
		drive_a = fd_new();

	if (pathb) {
		drive_b = fd_newdsk();
		fd_settype(drive_a, FD_35);
		fd_setheads(drive_a, 2);
		fd_setcyls(drive_a, 80);
		fdd_setfilename(drive_a, pathb);
	} else
		drive_b = fd_new();

	fdc_reset(fdc);
	fdc_setisr(fdc, NULL);

	fdc_setdrive(fdc, 0, drive_a);
	fdc_setdrive(fdc, 1, drive_b);

	m68k_init();
	m68k_set_cpu_type(cputype);
	m68k_pulse_reset();

	/* Init devices */
	device_init();

	while (1) {
		/* Approximate a 68008 */
		m68k_execute(400);
		uart16x50_event(uart);
		recalc_interrupts();
		/* The CPU runs at 8MHz but the NS202 is run off the serial
		   clock */
		ns202_tick(184);
		if (!fast)
			take_a_nap();
	}
}
