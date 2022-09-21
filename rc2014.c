/*
 *	Platform features
 *
 *	Z80 at 7.372MHz
 *	Zilog SIO/2 at 0x80-0x83
 *	Motorola 6850 repeats all over 0x40-0x7F (not recommended)
 *	IDE at 0x10-0x17 no high or control access
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at 0xC0
 *	16550A at 0xA0
 *
 *	Known bugs
 *	Not convinced we have all the INT clear cases right for SIO error
 *
 *	Add support for using real CF card
 *
 *	The SC121 just an initial sketch for playing with IM2 and the
 *	relevant peripherals. I'll align it properly with the real thing as more
 *	info appears.
 *
 *	TODO: implement quiet for the mem_read methods
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>

#include "system.h"
#include "libz80/z80.h"
#include "lib765/include/765.h"

#include "acia.h"
#include "amd9511.h"
#include "ide.h"
#include "ppide.h"
#include "ps2.h"
#include "rtc_bitbang.h"
#include "sdcard.h"
#include "tms9918a.h"
#include "tms9918a_render.h"
#include "w5100.h"
#include "z80copro.h"
#include "z80dma.h"
#include "zxkey.h"
#include "z80dis.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t switchrom = 1;
static uint32_t romsize = 65536;

#define CPUBOARD_Z80		0
#define CPUBOARD_SC108		1
#define CPUBOARD_SC114		2
#define CPUBOARD_Z80SBC64	3
#define CPUBOARD_EASYZ80	4
#define CPUBOARD_SC121		5
#define CPUBOARD_MICRO80	6
#define CPUBOARD_ZRCC		7
#define CPUBOARD_TINYZ80	8
#define CPUBOARD_PDOG128	9
#define CPUBOARD_PDOG512	10

static uint8_t cpuboard = CPUBOARD_Z80;

static uint8_t have_ctc;
static uint8_t have_pio;
static uint8_t have_ps2;
static uint8_t have_kio;
static uint8_t have_wiznet;
static uint8_t have_cpld_serial;
static uint8_t have_im2;
static uint8_t have_16x50;
static uint8_t have_copro;
static uint8_t have_tms;

static uint8_t port30 = 0;
static uint8_t port38 = 0;
static uint8_t fast = 0;
static uint8_t int_recalc = 0;
static uint8_t is_z512;
static uint8_t z512_control = 0;

static struct ppide *ppide;
static struct sdcard *sdcard;
static struct z80copro *copro;
static FDC_PTR fdc;
static FDRV_PTR drive_a, drive_b;
static struct tms9918a *vdp;
static struct tms9918a_renderer *vdprend;
static struct amd9511 *amd9511;
struct ps2 *ps2;

struct zxkey *zxkey;

static uint16_t tstate_steps = 365;	/* RC2014 speed */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

#define IRQ_SIOA	1
#define IRQ_SIOB	2
#define IRQ_CTC		3	/* 3 4 5 6 */

static Z80Context cpu_z80;

static nic_w5100_t *wiz;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_ROM	0x000004
#define TRACE_UNK	0x000008
#define TRACE_CPU	0x000010
#define TRACE_512	0x000020
#define TRACE_RTC	0x000040
#define TRACE_SIO	0x000080
#define TRACE_CTC	0x000100
#define TRACE_CPLD	0x000200
#define TRACE_IRQ	0x000400
#define TRACE_UART	0x000800
#define TRACE_Z84C15	0x001000
#define TRACE_IDE	0x002000
#define TRACE_SPI	0x004000
#define TRACE_SD	0x008000
#define TRACE_PPIDE	0x010000
#define TRACE_COPRO	0x020000
#define TRACE_COPRO_IO	0x040000
#define TRACE_TMS9918A  0x080000
#define TRACE_FDC	0x100000
#define TRACE_PS2	0x200000
#define TRACE_ACIA	0x400000

static int trace = 0;
static FILE *tracef = 0; /* stderr may not be statically defined, so
                            initialize to stderr in main() */

static void reti_event(void);

static uint8_t mem_read0(uint16_t addr)
{
	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(tracef, "R %04x[%02X] = %02X\n", addr, (unsigned int) bankreg[bank], (unsigned int) ramrom[(bankreg[bank] << 14) + (addr & 0x3FFF)]);
		addr &= 0x3FFF;
		return ramrom[(bankreg[bank] << 14) + addr];
	}
	if (bank512 && !bankenable)
		addr &= 0x3FFF;
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %04X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

static void mem_write0(uint16_t addr, uint8_t val)
{
	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(tracef, "W %04x[%02X] = %02X\n", (unsigned int) addr, (unsigned int) bankreg[bank], (unsigned int) val);
		if (bankreg[bank] >= 32) {
			addr &= 0x3FFF;
			ramrom[(bankreg[bank] << 14) + addr] = val;
		}
		/* ROM writes go nowhere */
		else if (trace & TRACE_MEM)
			fprintf(tracef, "[Discarded: ROM]\n");
	} else {
		if (trace & TRACE_MEM)
			fprintf(tracef, "W: %04X = %02X\n", addr, val);
		if (addr >= 8192 && !bank512)
			ramrom[addr] = val;
		else if (trace & TRACE_MEM)
			fprintf(tracef, "[Discarded: ROM]\n");
	}
}

static uint8_t mem_read108(uint16_t addr)
{
	uint32_t aphys;
	if (addr < 0x8000 && !(port38 & 0x01))
		aphys = addr;
	else if (port38 & 0x80)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %05X = %02X\n", aphys, ramrom[aphys]);
	return ramrom[aphys];
}

static void mem_write108(uint16_t addr, uint8_t val)
{
	uint32_t aphys;
	if (trace & TRACE_MEM)
		fprintf(tracef, "W: %04X = %02X\n", addr, val);
	if (addr < 0x8000 && !(port38 & 0x01)) {
		if (trace & TRACE_MEM)
			fprintf(tracef, "[Discarded: ROM]\n");
		return;
	} else if (port38 & 0x80)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	if (trace & TRACE_MEM)
		fprintf(tracef, "W: aphys %05X\n", aphys);
	ramrom[aphys] = val;
}

static uint8_t mem_read114(uint16_t addr)
{
	uint32_t aphys;
	if (addr < 0x8000 && !(port38 & 0x01))
		aphys = addr;
	else if (port30 & 0x01)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %04X = %02X\n", addr, ramrom[aphys]);
	return ramrom[aphys];
}

static void mem_write114(uint16_t addr, uint8_t val)
{
	uint32_t aphys;
	if (trace & TRACE_MEM)
		fprintf(tracef, "W: %04X = %02X\n", addr, val);
	if (addr < 0x8000 && !(port38 & 0x01)) {
		if (trace & TRACE_MEM)
			fprintf(tracef, "[Discarded: ROM]\n");
		return;
	} else if (port30 & 0x01)
		aphys = addr + 131072;
	else
		aphys = addr + 65536;
	ramrom[aphys] = val;
}

/* I think this right
   0: sets the lower bank to the lowest 32K of the 128K
   1: sets the lower bank to the 32K-64K range
   2: sets the lower bank to the 64K-96K range
   3: sets the lower bank to the 96K-128K range
   where the upper memory is always bank 1.

   Power on is 3, which is why the bootstrap lives in 3. */

static uint8_t mem_read64(uint16_t addr)
{
	uint8_t r;
	if (addr >= 0x8000)
		r = ramrom[addr];
	else
		r = ramrom[bankreg[0] * 0x8000 + addr];
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %04x = %02X\n", addr, r);
	return r;
}

static void mem_write64(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(tracef, "W %04x = %02X\n", addr, val);
	if (addr >= 0x8000)	/* Top 32K is common */
		ramrom[addr + 65536] = val;
	else
		ramrom[bankreg[0] * 0x8000 + addr] = val;
}

/* ZRCC is a close relative of SBC64, but instead of a magic loader has
   a 64byte built in boot rom */

static uint8_t zrcc_irom[64] = {
	0x3C,
	0x3C,
	0x3C,
	0x3C,

	0x21,
	0x00,
	0xB0,

	0xDB,
	0xF8,

	0xE6,
	0x01,

	0x20,
	0x24,

	0xDB,
	0x17,

	0xE6,
	0x80,

	0x20,
	0xF4,

	0x47,

	0xD3,
	0x15,

	0xD3,
	0x14,

	0x3C,

	0xD3,
	0x13,

	0x0E,
	0x10,

	0xD3,
	0x12,

	0x3E,
	0x20,

	0xD3,
	0x17,

	0xDB,
	0x17,

	0xE6,
	0x08,

	0x28,
	0xFA,

	0xED,
	0xB2,

	0xC3,
	0x00,
	0xB0,

	0x3C,
	0x3C,
	0x3C,

	0xDB,
	0xF9,

	0xDB,
	0xF8,

	0xE6,
	0x01,

	0x28,
	0xFA,

	0xDB,
	0xF9,

	0x77,

	0x2C,

	0x20,
	0xF4,
	0xE9
};

static uint8_t mem_readzrcc(uint16_t addr)
{
	uint8_t r;
	if (addr < 0x40 && bankreg[1] == 0)
		r = zrcc_irom[addr];
	else if (addr >= 0x8000)
		r = ramrom[addr + 65536];	/* Top 32K is common */
	else
		r = ramrom[bankreg[0] * 0x8000 + addr];
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %04x = %02X\n", addr, r);
	return r;
}

static void mem_writezrcc(uint16_t addr, uint8_t val)
{
	if (addr <= 0x40 && bankreg[1]) {
		if (trace & TRACE_MEM)
			fprintf(tracef, "W %04X = %02X [ROM]\n", addr, val);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(tracef, "W %04X = %02X\n", addr, val);
	if (addr >= 0x8000)
		ramrom[addr + 65536] = val;
	else
		ramrom[bankreg[0] * 0x8000 + addr] = val;
}



struct z84c15 {
	uint8_t scrp;
	uint8_t wcr;
	uint8_t mwbr;
	uint8_t csbr;
	uint8_t mcr;
	uint8_t intpr;
};

struct z84c15 z84c15;

static void z84c15_init(void)
{
	z84c15.scrp = 0;
	z84c15.wcr = 0;		/* Really it's 0xFF for 15 instructions then 0 */
	z84c15.mwbr = 0xF0;
	z84c15.csbr = 0x0F;
	z84c15.mcr = 0x01;
	z84c15.intpr = 0;
}

/*
 *	The Z84C15 CS lines as wired for the Micro80
 */

static uint8_t *mmu_micro80_z84c15(uint16_t addr, int write)
{
	uint8_t cs0 = 0, cs1 = 0;
	uint8_t page = addr >> 12;
	if (page <= (z84c15.csbr & 0x0F))
		cs0 = 1;
	else if (page <= (z84c15.csbr >> 4))
		cs1 = 1;
	if (!(z84c15.mcr & 0x01))
		cs0 = 0;
	if (!(z84c15.mcr & 0x02))
		cs1 = 0;
	/* Depending upon final flash wiring. PIO might control
	   this and it might be 32K */
	/* CS0 low selects ROM always */
	if (trace & TRACE_MEM) {
		if (cs0)
			fprintf(tracef, "R");
		if (cs1)
			fprintf(tracef, "L");
		else
			fprintf(tracef, "H");
	}
	if (cs0) {
		if (write)
			return NULL;
		else
			return &ramrom[(addr & 0x3FFF)];
	}
	/* CS1 low forces A16 low */
	if (cs1)
		return &ramrom[0x20000 + addr];
	return &ramrom[0x30000 + addr];
}

static uint8_t mem_read_micro80(uint16_t addr)
{
	uint8_t val = *mmu_micro80_z84c15(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %04x = %02X\n", addr, val);
	return val;
}

static void mem_write_micro80(uint16_t addr, uint8_t val)
{
	uint8_t *p = mmu_micro80_z84c15(addr, 1);
	if (trace & TRACE_MEM)
		fprintf(tracef, "W %04x = %02X\n", addr, val);
	if (p == NULL)
		fprintf(tracef, "%04x: write to ROM of %02X attempted.\n", addr, val);
	else
		*p = val;
}

/*
 *	Pickled Dog 128K and 512K RAM boards
 *
 *	These use a configuration close to that of the Retrobrew SBC but
 *	fixed to 32K/32K divide and with the same register controlling both
 *	ROM and RAM.
 *
 *	On the 512K/512K set up the low 4 bits of the port are the bank for
 *	low memory and the top bit is set to indicate RAM bank. On the 128K
 *	the set up is quite different with separate banking for the lower and
 *	upper 128K
 *
 */

static uint8_t pick_bank;

static uint8_t *mmu_pickled128(uint16_t addr, uint8_t wr)
{
	uint8_t b = pick_bank;
	if (addr & 0x8000) {
		b >>= 4;
		addr &= 0x7FFF;
	}
	if (b & 0x08)
		return &ramrom[addr + 131072 + ((pick_bank & 7) << 15)];
	if (wr)
		return NULL;
	return &ramrom[addr + (pick_bank << 15)];
}

static uint8_t mem_read_pickled128(uint16_t addr)
{
	uint8_t *p = mmu_pickled128(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %04X = %02X\n", addr, *p);
	return *p;
}

static void mem_write_pickled128(uint16_t addr, uint8_t val)
{
	uint8_t *p = mmu_pickled128(addr, 1);
	if (p == NULL) {
		fprintf(tracef, "%04X: write to ROM of %02X attempted.\n", addr, val);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(tracef, "%04X = %02X\n", addr, val);
	*p = val;
}

static uint8_t *mmu_pickled512(uint16_t addr, uint8_t wr)
{
	/* Top 32K of RAM bank */
	if (addr & 0x8000)
		return &ramrom[addr + 0x100000 - 0x8000];
	if (pick_bank & 0x80)
		return &ramrom[addr + 524288 + (pick_bank << 15)];
	if (wr)
		return NULL;
	return &ramrom[addr + (pick_bank << 15)];
}

static uint8_t mem_read_pickled512(uint16_t addr)
{
	uint8_t *p = mmu_pickled512(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(tracef, "R %04X = %02X\n", addr, *p);
	return *p;
}

static void mem_write_pickled512(uint16_t addr, uint8_t val)
{
	uint8_t *p = mmu_pickled512(addr, 1);
	if (p == NULL) {
		fprintf(tracef, "%04X: write to ROM of %02X attempted.\n", addr, val);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(tracef, "%04X = %02X\n", addr, val);
	*p = val;
}

uint8_t do_mem_read(uint16_t addr, int quiet)
{
	uint8_t r;

	switch (cpuboard) {
	case CPUBOARD_Z80:
		r = mem_read0(addr);
		break;
	case CPUBOARD_SC108:
		r = mem_read108(addr);
		break;
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		r = mem_read114(addr);
		break;
	case CPUBOARD_Z80SBC64:
		r = mem_read64(addr);
		break;
	case CPUBOARD_EASYZ80:
		r = mem_read0(addr);
		break;
	case CPUBOARD_MICRO80:
		r = mem_read_micro80(addr);
		break;
	case CPUBOARD_ZRCC:
		r = mem_readzrcc(addr);
		break;
	case CPUBOARD_TINYZ80:
		r = mem_read0(addr);
		break;
	case CPUBOARD_PDOG128:
		r = mem_read_pickled128(addr);
		break;
	case CPUBOARD_PDOG512:
		r = mem_read_pickled512(addr);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
	return r;
}

uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r = do_mem_read(addr, 0);

	if (cpu_z80.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r == 0xCB) {
			rstate = 2;
			return r;
		}
		/* Look for ED with M1, followed directly by 4D and if so trigger
		   the interrupt chain */
		if (r == 0xED && rstate == 0) {
			rstate = 1;
			return r;
		}
	}
	if (r == 0x4D && rstate == 1)
		reti_event();
	rstate = 0;
	return r;
}

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z80:
		mem_write0(addr, val);
		break;
	case CPUBOARD_SC108:
		mem_write108(addr, val);
		break;
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		mem_write114(addr, val);
		break;
	case CPUBOARD_Z80SBC64:
		mem_write64(addr, val);
		break;
	case CPUBOARD_EASYZ80:
		mem_write0(addr, val);
		break;
	case CPUBOARD_MICRO80:
		mem_write_micro80(addr, val);
		break;
	case CPUBOARD_ZRCC:
		mem_writezrcc(addr, val);
		break;
	case CPUBOARD_TINYZ80:
		mem_write0(addr, val);
		break;
	case CPUBOARD_PDOG128:
		mem_write_pickled128(addr, val);
		break;
	case CPUBOARD_PDOG512:
		mem_write_pickled512(addr, val);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = do_mem_read(addr, 1);
	fprintf(tracef, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return do_mem_read(addr, 1);
}

static void z80_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(tracef, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(tracef, "   ");
	fprintf(tracef, "%-16s ", buf);
	fprintf(tracef, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z80.R1.br.A, cpu_z80.R1.br.F,
		cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL,
		cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}



unsigned int check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;

	FD_ZERO(&i);
	FD_SET(0, &i);
	FD_ZERO(&o);
	FD_SET(1, &o);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(2, &i, NULL, NULL, &tv) == -1) {
		if (errno == EINTR)
			return 0;
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

unsigned int next_char(void)
{
	char c;
	if (read(0, &c, 1) != 1) {
		printf("(tty read without ready byte)\n");
		return 0xFF;
	}
	if (c == 0x0A)
		c = '\r';
	return c;
}

void recalc_interrupts(void)
{
	int_recalc = 1;
}

struct acia *acia;
static uint8_t acia_narrow;


static void acia_check_irq(struct acia *acia)
{
	if (acia_irq_pending(acia))
		Z80INT(&cpu_z80, 0xFF);	/* FIXME probably last data or bus noise */
}


/* UART: very mimimal for the moment */

struct uart16x50 {
    uint8_t ier;
    uint8_t iir;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scratch;
    uint8_t ls;
    uint8_t ms;
    uint8_t dlab;
    uint8_t irq;
#define RXDA	1
#define TEMT	2
#define MODEM	8
    uint8_t irqline;
    uint8_t input;
};

static struct uart16x50 uart[1];

static void uart_init(struct uart16x50 *uptr, int in)
{
    uptr->dlab = 0;
    uptr->input = in;
}

static void uart_check_irq(struct uart16x50 *uptr)
{
    if (uptr->irqline)
	    Z80INT(&cpu_z80, 0xFF);	/* actually undefined */
}

/* Compute the interrupt indicator register from what is pending */
static void uart_recalc_iir(struct uart16x50 *uptr)
{
    if (uptr->irq & RXDA)
        uptr->iir = 0x04;
    else if (uptr->irq & TEMT)
        uptr->iir = 0x02;
    else if (uptr->irq & MODEM)
        uptr->iir = 0x00;
    else {
        uptr->iir = 0x01;	/* No interrupt */
        uptr->irqline = 0;
        return;
    }
    /* Ok so we have an event, do we need to waggle the line */
    if (uptr->irqline)
        return;
    uptr->irqline = uptr->irq;

}

/* Raise an interrupt source. Only has an effect if enabled in the ier */
static void uart_interrupt(struct uart16x50 *uptr, uint8_t n)
{
    if (uptr->irq & n)
        return;
    if (!(uptr->ier & n))
        return;
    uptr->irq |= n;
    uart_recalc_iir(uptr);
}

static void uart_clear_interrupt(struct uart16x50 *uptr, uint8_t n)
{
    if (!(uptr->irq & n))
        return;
    uptr->irq &= ~n;
    uart_recalc_iir(uptr);
}

static void uart_event(struct uart16x50 *uptr)
{
    uint8_t r = check_chario();
    uint8_t old = uptr->lsr;
    uint8_t dhigh;
    if (uptr->input && (r & 1))
        uptr->lsr |= 0x01;	/* RX not empty */
    if (r & 2)
        uptr->lsr |= 0x60;	/* TX empty */
    dhigh = (old ^ uptr->lsr);
    dhigh &= uptr->lsr;		/* Changed high bits */
    if (dhigh & 1)
        uart_interrupt(uptr, RXDA);
    if (dhigh & 0x2)
        uart_interrupt(uptr, TEMT);
}

static void show_settings(struct uart16x50 *uptr)
{
    uint32_t baud;

    if (!(trace & TRACE_UART))
        return;

    baud = uptr->ls + (uptr->ms << 8);
    if (baud == 0)
        baud = 1843200;
    baud = 1843200 / baud;
    baud /= 16;
    fprintf(tracef, "[%d:%d",
            baud, (uptr->lcr &3) + 5);
    switch(uptr->lcr & 0x38) {
        case 0x00:
        case 0x10:
        case 0x20:
        case 0x30:
            fprintf(tracef, "N");
            break;
        case 0x08:
            fprintf(tracef, "O");
            break;
        case 0x18:
            fprintf(tracef, "E");
            break;
        case 0x28:
            fprintf(tracef, "M");
            break;
        case 0x38:
            fprintf(tracef, "S");
            break;
    }
    fprintf(tracef, "%d ",
            (uptr->lcr & 4) ? 2 : 1);

    if (uptr->lcr & 0x40)
        fprintf(tracef, "break ");
    if (uptr->lcr & 0x80)
        fprintf(tracef, "dlab ");
    if (uptr->mcr & 1)
        fprintf(tracef, "DTR ");
    if (uptr->mcr & 2)
        fprintf(tracef, "RTS ");
    if (uptr->mcr & 4)
        fprintf(tracef, "OUT1 ");
    if (uptr->mcr & 8)
        fprintf(tracef, "OUT2 ");
    if (uptr->mcr & 16)
        fprintf(tracef, "LOOP ");
    fprintf(tracef, "ier %02x]\n", uptr->ier);
}

static void uart_write(struct uart16x50 *uptr, uint8_t addr, uint8_t val)
{
    switch(addr) {
    case 0:	/* If dlab = 0, then write else LS*/
        if (uptr->dlab == 0) {
            if (uptr == &uart[0]) {
                putchar(val);
                fflush(stdout);
            }
            uart_clear_interrupt(uptr, TEMT);
            uart_interrupt(uptr, TEMT);
        } else {
            uptr->ls = val;
            show_settings(uptr);
        }
        break;
    case 1:	/* If dlab = 0, then IER */
        if (uptr->dlab) {
            uptr->ms= val;
            show_settings(uptr);
        }
        else
            uptr->ier = val;
        break;
    case 2:	/* FCR */
        uptr->fcr = val & 0x9F;
        break;
    case 3:	/* LCR */
        uptr->lcr = val;
        uptr->dlab = (uptr->lcr & 0x80);
        show_settings(uptr);
        break;
    case 4:	/* MCR */
        uptr->mcr = val & 0x3F;
        break;
    case 5:	/* LSR (r/o) */
        break;
    case 6:	/* MSR (r/o) */
        break;
    case 7:	/* Scratch */
        uptr->scratch = val;
        break;
    }
}

static uint8_t uart_read(struct uart16x50 *uptr, uint8_t addr)
{
    uint8_t r;

    switch(addr) {
    case 0:
        /* receive buffer */
        if (uptr == &uart[0] && uptr->dlab == 0) {
            uart_clear_interrupt(uptr, RXDA);
            if (check_chario() & 1)
                return next_char();
            return 0x00;
        } else
            return uptr->ls;
        break;
    case 1:
        /* IER */
        if (uptr->dlab == 0)
            return uptr->ier;
        return uptr->ms;
    case 2:
        /* IIR */
        return uptr->iir;
    case 3:
        /* LCR */
        return uptr->lcr;
    case 4:
        /* mcr */
        return uptr->mcr;
    case 5:
        /* lsr */
        r = check_chario();
        uptr->lsr &=0x90;
        if (r & 1)
             uptr->lsr |= 0x01;	/* Data ready */
        if (r & 2)
             uptr->lsr |= 0x60;	/* TX empty | holding empty */
        /* Reading the LSR causes these bits to clear */
        r = uptr->lsr;
        uptr->lsr &= 0xF0;
        return r;
    case 6:
        /* msr */
        uptr->msr &= 0x7F;
        r = uptr->msr;
        /* Reading clears the delta bits */
        uptr->msr &= 0xF0;
        uart_clear_interrupt(uptr, MODEM);
        return r;
    case 7:
        return uptr->scratch;
    }
    return 0xFF;
}


struct z80_sio_chan {
	uint8_t wr[8];
	uint8_t rr[3];
	uint8_t data[3];
	uint8_t dptr;
	uint8_t irq;
	uint8_t rxint;
	uint8_t txint;
	uint8_t intbits;
#define INT_TX	1
#define INT_RX	2
#define INT_ERR	4
	uint8_t pending;	/* Interrupt bits pending as an IRQ cause */
	uint8_t vector;		/* Vector pending to deliver */
};

static int sio2;
static int sio2_input;
static struct z80_sio_chan sio[2];

/*
 *	Interrupts. We don't handle IM2 yet.
 */

static void sio2_clear_int(struct z80_sio_chan *chan, uint8_t m)
{
	if (trace & TRACE_IRQ) {
		fprintf(tracef, "Clear intbits %d %x\n",
			(int)(chan - sio), m);
	}
	chan->intbits &= ~m;
	chan->pending &= ~m;
	/* Check me - does it auto clear down or do you have to reti it ? */
	if (!(sio->intbits | sio[1].intbits)) {
		sio->rr[1] &= ~0x02;
		chan->irq = 0;
	}
	recalc_interrupts();
}

static void sio2_raise_int(struct z80_sio_chan *chan, uint8_t m)
{
	uint8_t new = (chan->intbits ^ m) & m;
	chan->intbits |= m;
	if ((trace & TRACE_SIO) && new)
		fprintf(tracef, "SIO raise int %x new = %x\n", m, new);
	if (new) {
		if (!sio->irq) {
			chan->irq = 1;
			sio->rr[1] |= 0x02;
			recalc_interrupts();
		}
	}
}

static void sio2_reti(struct z80_sio_chan *chan)
{
	/* Recalculate the pending state and vectors */
	/* FIXME: what really goes here */
	sio->irq = 0;
	recalc_interrupts();
}

static int sio2_check_im2(struct z80_sio_chan *chan)
{
	uint8_t vector = sio[1].wr[2];
	/* See if we have an IRQ pending and if so deliver it and return 1 */
	if (chan->irq) {
		/* Do the vector calculation in the right place */
		/* FIXME: move this to other platforms */
		if (sio[1].wr[1] & 0x04) {
			/* This is a subset of the real options. FIXME: add
			   external status change */
			if (sio[1].wr[1] & 0x04) {
				vector &= 0xF1;
				if (chan == sio)
					vector |= 1 << 3;
				if (chan->intbits & INT_RX)
					vector |= 4;
				else if (chan->intbits & INT_ERR)
					vector |= 2;
			}
			if (trace & TRACE_SIO)
				fprintf(tracef, "SIO2 interrupt %02X\n", vector);
			chan->vector = vector;
		} else {
			chan->vector = vector;
		}
		if (trace & (TRACE_IRQ|TRACE_SIO))
			fprintf(tracef, "New live interrupt pending is SIO (%d:%02X).\n",
				(int)(chan - sio), chan->vector);
		if (chan == sio)
			live_irq = IRQ_SIOA;
		else
			live_irq = IRQ_SIOB;
		Z80INT(&cpu_z80, chan->vector);
		return 1;
	}
	return 0;
}

/*
 *	The SIO replaces the last character in the FIFO on an
 *	overrun.
 */
static void sio2_queue(struct z80_sio_chan *chan, uint8_t c)
{
	if (trace & TRACE_SIO)
		fprintf(tracef, "SIO %d queue %d: ", (int) (chan - sio), c);
	/* Receive disabled */
	if (!(chan->wr[3] & 1)) {
		fprintf(tracef, "RX disabled.\n");
		return;
	}
	/* Overrun */
	if (chan->dptr == 2) {
		if (trace & TRACE_SIO)
			fprintf(tracef, "Overrun.\n");
		chan->data[2] = c;
		chan->rr[1] |= 0x20;	/* Overrun flagged */
		/* What are the rules for overrun delivery FIXME */
		sio2_raise_int(chan, INT_ERR);
	} else {
		/* FIFO add */
		if (trace & TRACE_SIO)
			fprintf(tracef, "Queued %d (mode %d)\n", chan->dptr, chan->wr[1] & 0x18);
		chan->data[chan->dptr++] = c;
		chan->rr[0] |= 1;
		switch (chan->wr[1] & 0x18) {
		case 0x00:
			break;
		case 0x08:
			if (chan->dptr == 1)
				sio2_raise_int(chan, INT_RX);
			break;
		case 0x10:
		case 0x18:
			sio2_raise_int(chan, INT_RX);
			break;
		}
	}
	/* Need to deal with interrupt results */
}

static void sio2_channel_timer(struct z80_sio_chan *chan, uint8_t ab)
{
	if (ab == 0) {
		int c = check_chario();

		if (sio2_input) {
			if (c & 1)
				sio2_queue(chan, next_char());
		}
		if (c & 2) {
			if (!(chan->rr[0] & 0x04)) {
				chan->rr[0] |= 0x04;
				if (chan->wr[1] & 0x02)
					sio2_raise_int(chan, INT_TX);
			}
		}
	} else {
		if (!(chan->rr[0] & 0x04)) {
			chan->rr[0] |= 0x04;
			if (chan->wr[1] & 0x02)
				sio2_raise_int(chan, INT_TX);
		}
	}
}

static void sio2_timer(void)
{
	sio2_channel_timer(sio, 0);
	sio2_channel_timer(sio + 1, 1);
}

static void sio2_channel_reset(struct z80_sio_chan *chan)
{
	chan->rr[0] = 0x2C;
	chan->rr[1] = 0x01;
	chan->rr[2] = 0;
	sio2_clear_int(chan, INT_RX | INT_TX | INT_ERR);
}

static void sio_reset(void)
{
	sio2_channel_reset(sio);
	sio2_channel_reset(sio + 1);
}

static uint8_t sio2_read(uint16_t addr)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio + 1 : sio;
	if (!(addr & 1)) {
		/* Control */
		uint8_t r = chan->wr[0] & 007;
		chan->wr[0] &= ~007;

		chan->rr[0] &= ~2;
		if (chan == sio && (sio[0].intbits | sio[1].intbits))
			chan->rr[0] |= 2;
		if (trace & TRACE_SIO)
			fprintf(tracef, "sio%c read reg %d = ", (addr & 2) ? 'b' : 'a', r);
		switch (r) {
		case 0:
		case 1:
			if (trace & TRACE_SIO)
				fprintf(tracef, "%02X\n", chan->rr[r]);
			return chan->rr[r];
		case 2:
			if (chan != sio) {
				if (trace & TRACE_SIO)
					fprintf(tracef, "%02X\n", chan->rr[2]);
				return chan->rr[2];
			}
		case 3:
			/* What does the hw report ?? */
			fprintf(tracef, "INVALID(0xFF)\n");
			return 0xFF;
		}
	} else {
		/* FIXME: irq handling */
		uint8_t c = chan->data[0];
		chan->data[0] = chan->data[1];
		chan->data[1] = chan->data[2];
		if (chan->dptr)
			chan->dptr--;
		if (chan->dptr == 0)
			chan->rr[0] &= 0xFE;	/* Clear RX pending */
		sio2_clear_int(chan, INT_RX);
		chan->rr[0] &= 0x3F;
		chan->rr[1] &= 0x3F;
		if (trace & TRACE_SIO)
			fprintf(tracef, "sio%c read data %d\n", (addr & 2) ? 'b' : 'a', c);
		if (chan->dptr && (chan->wr[1] & 0x10))
			sio2_raise_int(chan, INT_RX);
		return c;
	}
	return 0xFF;
}

static void sio2_write(uint16_t addr, uint8_t val)
{
	struct z80_sio_chan *chan = (addr & 2) ? sio + 1 : sio;
	uint8_t r;
	if (!(addr & 1)) {
		if (trace & TRACE_SIO)
			fprintf(tracef, "sio%c write reg %d with %02X\n", (addr & 2) ? 'b' : 'a', chan->wr[0] & 7, val);
		switch (chan->wr[0] & 007) {
		case 0:
			chan->wr[0] = val;
			/* FIXME: CRC reset bits ? */
			switch (val & 070) {
			case 000:	/* NULL */
				break;
			case 010:	/* Send Abort SDLC */
				/* SDLC specific no-op for async */
				break;
			case 020:	/* Reset external/status interrupts */
				sio2_clear_int(chan, INT_ERR);
				chan->rr[1] &= 0xCF;	/* Clear status bits on rr0 */
				break;
			case 030:	/* Channel reset */
				if (trace & TRACE_SIO)
					fprintf(tracef, "[channel reset]\n");
				sio2_channel_reset(chan);
				break;
			case 040:	/* Enable interrupt on next rx */
				chan->rxint = 1;
				break;
			case 050:	/* Reset transmitter interrupt pending */
				chan->txint = 0;
				sio2_clear_int(chan, INT_TX);
				break;
			case 060:	/* Reset the error latches */
				chan->rr[1] &= 0x8F;
				break;
			case 070:	/* Return from interrupt (channel A) */
				if (chan == sio) {
					sio->irq = 0;
					sio->rr[1] &= ~0x02;
					sio2_clear_int(sio, INT_RX | INT_TX | INT_ERR);
					sio2_clear_int(sio + 1, INT_RX | INT_TX | INT_ERR);
				}
				break;
			}
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			r = chan->wr[0] & 7;
			if (trace & TRACE_SIO)
				fprintf(tracef, "sio%c: wrote r%d to %02X\n",
					(addr & 2) ? 'b' : 'a', r, val);
			chan->wr[r] = val;
			if (chan != sio && r == 2)
				chan->rr[2] = val;
			chan->wr[0] &= ~007;
			break;
		}
		/* Control */
	} else {
		/* Strictly we should emulate this as two bytes, one going out and
		   the visible queue - FIXME */
		/* FIXME: irq handling */
		chan->rr[0] &= ~(1 << 2);	/* Transmit buffer no longer empty */
		chan->txint = 1;
		/* Should check chan->wr[5] & 8 */
		sio2_clear_int(chan, INT_TX);
		if (trace & TRACE_SIO)
			fprintf(tracef, "sio%c write data %d\n", (addr & 2) ? 'b' : 'a', val);
		if (chan == sio)
			write(1, &val, 1);
		else {
//			write(1, "\033[1m;", 5);
			write(1, &val,1);
//			write(1, "\033[0m;", 5);
		}
	}
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	uint8_t r =  ide_read8(ide0, addr);
	if (trace & TRACE_IDE)
		fprintf(tracef, "ide read %d = %02X\n", addr, r);
	return r;
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IDE)
		fprintf(tracef, "ide write %d = %02X\n", addr, val);
	ide_write8(ide0, addr, val);
}

struct rtc *rtc;

/*
 *	Z80 CTC
 */

struct z80_ctc {
	uint16_t count;
	uint16_t reload;
	uint8_t vector;
	uint8_t ctrl;
#define CTC_IRQ		0x80
#define CTC_COUNTER	0x40
#define CTC_PRESCALER	0x20
#define CTC_RISING	0x10
#define CTC_PULSE	0x08
#define CTC_TCONST	0x04
#define CTC_RESET	0x02
#define CTC_CONTROL	0x01
	uint8_t irq;		/* Only valid for channel 0, so we know
				   if we must wait for a RETI before doing
				   a further interrupt */
};

#define CTC_STOPPED(c)	(((c)->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET))

struct z80_ctc ctc[4];
uint8_t ctc_irqmask;

static void ctc_reset(struct z80_ctc *c)
{
	c->vector = 0;
	c->ctrl = CTC_RESET;
}

static void ctc_init(void)
{
	ctc_reset(ctc);
	ctc_reset(ctc + 1);
	ctc_reset(ctc + 2);
	ctc_reset(ctc + 3);
}

static void ctc_interrupt(struct z80_ctc *c)
{
	int i = c - ctc;
	if (c->ctrl & CTC_IRQ) {
		if (!(ctc_irqmask & (1 << i))) {
			ctc_irqmask |= 1 << i;
			recalc_interrupts();
			if (trace & TRACE_CTC)
				fprintf(tracef, "CTC %d wants to interrupt.\n", i);
		}
	}
}

static void ctc_reti(int ctcnum)
{
	if (ctc_irqmask & (1 << ctcnum)) {
		ctc_irqmask &= ~(1 << ctcnum);
		if (trace & TRACE_IRQ)
			fprintf(tracef, "Acked interrupt from CTC %d.\n", ctcnum);
	}
}

/* After a RETI or when idle compute the status of the interrupt line and
   if we are head of the chain this time then raise our interrupt */

static int ctc_check_im2(void)
{
	if (ctc_irqmask) {
		int i;
		for (i = 0; i < 4; i++) {	/* FIXME: correct order ? */
			if (ctc_irqmask & (1 << i)) {
				uint8_t vector = ctc[0].vector & 0xF8;
				vector += 2 * i;
				if (trace & TRACE_IRQ)
					fprintf(tracef, "New live interrupt is from CTC %d vector %x.\n", i, vector);
				live_irq = IRQ_CTC + i;
				Z80INT(&cpu_z80, vector);
				return 1;
			}
		}
	}
	return 0;
}

/* Model the chains between the CTC devices */
static void ctc_receive_pulse(int i);

static void ctc_pulse(int i)
{
	if (cpuboard != CPUBOARD_SC121) {
		/* Model CTC 2 chained into CTC 3 */
		if (i == 2)
			ctc_receive_pulse(3);
	}
	/* The SC121 has 0-2 for SIO baud and only 3 for a timer */
}

/* We don't worry about edge directions just a logical pulse model */
static void ctc_receive_pulse(int i)
{
	struct z80_ctc *c = ctc + i;
	if (c->ctrl & CTC_COUNTER) {
		if (CTC_STOPPED(c))
			return;
		if (c->count >= 0x0100)
			c->count -= 0x100;	/* No scaling on pulses */
		if ((c->count & 0xFF00) == 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			c->count = c->reload << 8;
		}
	} else {
		if (c->ctrl & CTC_PULSE)
			c->ctrl &= ~CTC_PULSE;
	}
}

/* Model counters */
static void ctc_tick(unsigned int clocks)
{
	struct z80_ctc *c = ctc;
	int i;
	int n;
	int decby;

	for (i = 0; i < 4; i++, c++) {
		/* Waiting a value */
		if (CTC_STOPPED(c))
			continue;
		/* Pulse trigger mode */
		if (c->ctrl & CTC_COUNTER)
			continue;
		/* 256x downscaled */
		decby = clocks;
		/* 16x not 256x downscale - so increase by 16x */
		if (!(c->ctrl & CTC_PRESCALER))
			decby <<= 4;
		/* Now iterate over the events. We need to deal with wraps
		   because we might have something counters chained */
		n = c->count - decby;
		while (n < 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			if (c->reload == 0)
				n += 256 << 8;
			else
				n += c->reload << 8;
		}
		c->count = n;
	}
}

static void ctc_write(uint8_t channel, uint8_t val)
{
	struct z80_ctc *c = ctc + channel;
	if (c->ctrl & CTC_TCONST) {
		if (trace & TRACE_CTC)
			fprintf(tracef, "CTC %d constant loaded with %02X\n", channel, val);
		c->reload = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET)) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(tracef, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		c->ctrl &= ~CTC_TCONST|CTC_RESET;
	} else if (val & CTC_CONTROL) {
		/* We don't yet model the weirdness around edge wanted
		   toggling and clock starts */
		if (trace & TRACE_CTC)
			fprintf(tracef, "CTC %d control loaded with %02X\n", channel, val);
		c->ctrl = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == CTC_RESET) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(tracef, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		/* Undocumented */
		if (!(c->ctrl & CTC_IRQ) && (ctc_irqmask & (1 << channel))) {
			ctc_irqmask &= ~(1 << channel);
			if (ctc_irqmask == 0) {
				if (trace & TRACE_IRQ)
					fprintf(tracef, "CTC %d irq reset.\n", channel);
				if (live_irq == IRQ_CTC + channel)
					live_irq = 0;
			}
		}
	} else {
		if (trace & TRACE_CTC)
			fprintf(tracef, "CTC %d vector loaded with %02X\n", channel, val);
		/* Only works on channel 0 */
		if (channel == 0)
			c->vector = val;
	}
}

static uint8_t ctc_read(uint8_t channel)
{
	uint8_t val = ctc[channel].count >> 8;
	if (trace & TRACE_CTC)
		fprintf(tracef, "CTC %d reads %02x\n", channel, val);
	return val;
}

static uint8_t sd_clock = 0x10;	/* Bit masks */
static uint8_t sd_mosi = 0x01;
static uint8_t sd_miso = 0x80;

static uint8_t sd_port = 1;	/* Channel for data in */

struct z80_pio {
	uint8_t data[2];
	uint8_t mask[2];
	uint8_t mode[2];
	uint8_t intmask[2];
	uint8_t icw[2];
	uint8_t mpend[2];
	uint8_t irq[2];
	uint8_t vector[2];
	uint8_t in[2];
};

static struct z80_pio pio[1];

static uint8_t pio_cs;

/* Software SPI test: one device for now */

static uint8_t spi_byte_sent(uint8_t val)
{
	uint8_t r = sd_spi_in(sdcard, val);
	if (trace & TRACE_SPI)
		fprintf(tracef,	"[SPI %02X:%02X]\n", val, r);
	return r;
}

/* Bit 2: CLK, 1: MOSI, 0: MISO */
static void bitbang_spi(uint8_t val)
{
	static uint8_t old = 0xFF;
	static uint8_t oldcs = 1;
	static uint8_t bits;
	static uint8_t bitct;
	static uint8_t rxbits = 0xFF;
	uint8_t delta = old ^ val;

	old = val;

	if (!sdcard)
		return;

	if ((pio_cs & 0x03) == 0x01) {		/* CS high - deselected */
		if (!oldcs) {
			if (trace & TRACE_SPI)
				fprintf(tracef,	"[Raised \\CS]\n");
			bits = 0;
			oldcs = 1;
			sd_spi_raise_cs(sdcard);
		}
	} else if (oldcs) {
		if (trace & TRACE_SPI)
			fprintf(tracef, "[Lowered \\CS]\n");
		oldcs = 0;
		sd_spi_lower_cs(sdcard);
	}
	/* Capture clock edge */
	if (delta & sd_clock) {		/* Clock edge */
		if (val & sd_clock) {	/* Rising - capture in SPI0 */
			bits <<= 1;
			bits |= (val & sd_mosi) ? 1 : 0;
			bitct++;
			if (bitct == 8) {
				rxbits = spi_byte_sent(bits);
				bitct = 0;
			}
		} else {
			/* Falling edge */
			pio->in[sd_port] &= ~sd_miso;
			pio->in[sd_port] |= (rxbits & 0x80) ? sd_miso : 0x00;
			rxbits <<= 1;
			rxbits |= 0x01;
		}
	}
}

/* Bus emulation helpers */

void pio_data_write(struct z80_pio *pio, uint8_t port, uint8_t val)
{
	if (cpuboard == CPUBOARD_MICRO80) {
		if (port == 0)
			bitbang_spi(val);
		else if (port == 1)
			pio_cs = val & 7;
	} else {
		if (port == 1) {
			pio_cs = (val & 0x08) >> 3;
			bitbang_spi(val);
		}
	}
}

void pio_strobe(struct z80_pio *pio, uint8_t port)
{
}

uint8_t pio_data_read(struct z80_pio *pio, uint8_t port)
{
	return pio->in[port];
}

static void pio_recalc(void)
{
	/* For now we don't model interrupts at all */
}

/* Simple Z80 PIO model. We don't yet deal with the fancy bidirectional mode
   or the strobes in mode 0-2. We don't do interrupt mask triggers on mode 3 */

/* TODO: interrupts, strobes */

static void pio_write(uint8_t addr, uint8_t val)
{
	uint8_t pio_port = (addr & 2) >> 1;
	uint8_t pio_ctrl = addr & 1;

	if (pio_ctrl) {
		if (pio->icw[pio_port] & 1) {
			pio->intmask[pio_port] = val;
			pio->icw[pio_port] &= ~1;
			pio_recalc();
			return;
		}
		if (pio->mpend[pio_port]) {
			pio->mask[pio_port] = val;
			pio_recalc();
			pio->mpend[pio_port] = 0;
			return;
		}
		if (!(val & 1)) {
			pio->vector[pio_port] = val;
			return;
		}
		if ((val & 0x0F) == 0x0F) {
			pio->mode[pio_port] = val >> 6;
			if (pio->mode[pio_port] == 3)
				pio->mpend[pio_port] = 1;
			pio_recalc();
			return;
		}
		if ((val & 0x0F) == 0x07) {
			pio->icw[pio_port] = val >> 4;
			return;
		}
		return;
	} else {
		pio->data[pio_port] = val;
		switch(pio->mode[pio_port]) {
		case 0:
		case 2:	/* Not really emulated */
			pio_data_write(pio, pio_port, val);
			pio_strobe(pio, pio_port);
			break;
		case 1:
			break;
		case 3:
			/* Force input lines to floating high */
			val |= pio->mask[pio_port];
			pio_data_write(pio, pio_port, val);
			break;
		}
	}
}

static uint8_t pio_read(uint8_t addr)
{
	uint8_t pio_port = (addr & 2) >> 1;
	uint8_t val;
	uint8_t rx;

	/* Output lines */
	val = pio->data[pio_port];
	rx = pio_data_read(pio, pio_port);

	switch(pio->mode[pio_port]) {
	case 0:
		/* Write only */
		break;
	case 1:
		/* Read only */
		val = rx;
		break;
	case 2:
		/* Bidirectional (not really emulated) */
	case 3:
		/* Control mode */
		val &= ~pio->mask[pio_port];
		val |= rx & pio->mask[pio_port];
		break;
	}
	return val;
}

static uint8_t pio_remap[4] = {
	0,
	2,
	1,
	3
};

/* With the C/D and A/B lines the other way around */
static void pio_write2(uint8_t addr, uint8_t val)
{
	pio_write(pio_remap[addr], val);
}

static uint8_t pio_read2(uint8_t addr)
{
	return pio_read(pio_remap[addr]);
}

static void pio_reset(void)
{
	/* Input mode */
	pio->mask[0] = 0xFF;
	pio->mask[1] = 0xFF;
	/* Mode 1 */
	pio->mode[0] = 1;
	pio->mode[1] = 1;
	/* No output data value */
	pio->data[0] = 0;
	pio->data[1] = 0;
	/* Nothing pending */
	pio->mpend[0] = 0;
	pio->mpend[1] = 0;
	/* Clear icw */
	pio->icw[0] = 0;
	pio->icw[1] = 0;
	/* No interrupt */
	pio->irq[0] = 0;
	pio->irq[1] = 0;
}


/*
 *	Emulate the switchable ROM card. We switch between the ROM and
 *	two banks of RAM (any two will do providing it's not the ones we
 *	pretended the bank mapping used for the top 32K). You can't mix the
 *	512K ROM/RAM with this card anyway.
 */
static void toggle_rom(void)
{
	if (bankreg[0] == 0) {
		if (trace & TRACE_ROM)
			fprintf(tracef, "[ROM out]\n");
		bankreg[0] = 34;
		bankreg[1] = 35;
	} else {
		if (trace & TRACE_ROM)
			fprintf(tracef, "[ROM in]\n");
		bankreg[0] = 0;
		bankreg[1] = 1;
	}
}

/*
 *	Emulate the Z80SBC64 and ZRCC CPLDs. These are close relatives
 *	but ZRCC has an additional ROM control bits
 */

static uint8_t sbc64_cpld_status;
static uint8_t sbc64_cpld_char;

static void sbc64_cpld_timer(void)
{
	/* Don't allow overruns - hack for convenience when pasting hex files */
	if (!(sbc64_cpld_status & 1)) {
		if (check_chario() & 1) {
			sbc64_cpld_status |= 1;
			sbc64_cpld_char = next_char();
		}
	}
}

static uint8_t sbc64_cpld_uart_rx(void)
{
	sbc64_cpld_status &= ~1;
	if (trace & TRACE_CPLD)
		fprintf(tracef, "CPLD rx %02X.\n", sbc64_cpld_char);
	return sbc64_cpld_char;
}

static uint8_t sbc64_cpld_uart_status(void)
{
//	if (trace & TRACE_CPLD)
//		fprintf(tracef, "CPLD status %02X.\n", sbc64_cpld_status);
	return sbc64_cpld_status;
}

static void sbc64_cpld_uart_ctrl(uint8_t val)
{
	if (trace & TRACE_CPLD)
		fprintf(tracef, "CPLD control %02X.\n", val);
}

static void sbc64_cpld_uart_tx(uint8_t val)
{
	static uint16_t bits;
	static uint8_t bitcount;
	/* This is umm... fun. We should do a clock based analysis and
	   bit recovery. For the moment cheat to get it tested */
	val &= 1;
	if (bitcount == 0) {
		if (val & 1)
			return;
		/* Look mummy a start a bit */
		bitcount = 1;
		bits = 0;
		if (trace & TRACE_CPLD)
			fprintf(tracef, "[start]");
		return;
	}
	/* This works because all the existing code does one write per bit */
	if (bitcount == 9) {
		if (val & 1) {
			if (trace & TRACE_CPLD)
				fprintf(tracef, "[stop]");
			putchar(bits);
			fflush(stdout);
		} else	/* Framing error should be a stop bit */
			putchar('?');
		bitcount = 0;
		bits = 0;
		return;
	}
	bits >>= 1;
	bits |= val ? 0x80: 0x00;
	if (trace & TRACE_CPLD)
		fprintf(tracef, "[%d]", val);
	bitcount++;
}

static void sbc64_cpld_bankreg(uint8_t val)
{
	if (cpuboard == CPUBOARD_ZRCC)
		bankreg[1] |=  val & 0x10;
	/* Bit 2 is the LED */
	val &= 3;
	if (bankreg[0] != val) {
		if (trace & TRACE_CPLD)
			fprintf(tracef, "Bank set to %02X\n", val);
		bankreg[0] = val;
	}
}

static uint8_t z84c15_read(uint8_t port)
{
	switch(port) {
	case 0xEE:
		return z84c15.scrp;
	case 0xEF:
		switch(z84c15.scrp) {
		case 0:
			return z84c15.wcr;
		case 1:
			return z84c15.mwbr;
		case 2:
			return z84c15.csbr;
		case 3:
			return z84c15.mcr;
		default:
			fprintf(tracef, "Read invalid SCRP  %d\n", z84c15.scrp);
			return 0xFF;
		}
		break;
	/* Watchdog: not yet emulated */
	case 0xF0:
	case 0xF1:
		return 0xFF;
	}
	return 0xFF;
}

static void z84c15_write(uint8_t port, uint8_t val)
{
	if (trace & TRACE_Z84C15)
		fprintf(tracef, "z84c15: write %02X <- %02X\n",
			port, val);
	switch(port) {
	case 0xEE:
		z84c15.scrp = val;
		break;
	case 0xEF:
		switch(z84c15.scrp) {
		case 0:
			z84c15.wcr = val;
			break;
		case 1:
			z84c15.mwbr = val;
			break;
		case 2:
			z84c15.csbr = val;
			break;
		case 3:
			z84c15.mcr = val;
			break;
		default:
			fprintf(tracef, "Read invalid SCRP  %d\n", z84c15.scrp);
		}
		break;
	/* Watchdog: not yet emulated */
	case 0xF0:
	case 0xF1:
		return;
	case 0xF4:
		z84c15.intpr = val;
		break;
	}
}

/* Z84C90 KIO. The CTC, PIO and SIO bundled together with some other bits */
static uint8_t kio_read(uint8_t addr)
{
	if (addr < 0x04)
		return pio_read(addr & 3);
	if (addr < 0x08)
		return ctc_read(addr & 3);
	if (addr < 0x0C)
		return sio2_read((addr & 3) ^ 1);
	/* PIA and KIO control - TODO */
	return 0xFF;
}

static void kio_write(uint8_t addr, uint8_t val)
{
	if (addr < 0x04)
		pio_write(addr & 3, val);
	else if (addr < 0x08)
		ctc_write(addr & 3, val);
	else if (addr < 0x0C)
		sio2_write((addr & 3) ^ 1, val);
	/* PIA and KIO control - TODO */
}

static void fdc_log(int debuglevel, char *fmt, va_list ap)
{
	if ((trace & TRACE_FDC) || debuglevel == 0)
		vfprintf(tracef, "fdc: ", ap);
}

static void fdc_write(uint8_t addr, uint8_t val)
{
	switch(addr) {
	case 1:	/* Data */
		fprintf(tracef, "FDC Data: %02X\n", val);
		fdc_write_data(fdc, val);
		break;
	case 2:	/* DOR */
		fprintf(tracef, "FDC DOR %02X [", val);
		if (val & 0x80)
			fprintf(tracef, "SPECIAL ");
		else
			fprintf(tracef, "AT/EISA ");
		if (val & 0x20)
			fprintf(tracef, "MOEN2 ");
		if (val & 0x10)
			fprintf(tracef, "MOEN1 ");
		if (val & 0x08)
			fprintf(tracef, "DMA ");
		if (!(val & 0x04))
			fprintf(tracef, "SRST ");
		if (!(val & 0x02))
			fprintf(tracef, "DSEN ");
		if (val & 0x01)
			fprintf(tracef, "DSEL1");
		else
			fprintf(tracef, "DSEL0");
		fprintf(tracef, "]\n");
		fdc_write_dor(fdc, val);
#if 0		
		if ((val & 0x21) == 0x21)
			fdc_set_motor(fdc, 2);
		else if ((val & 0x11) == 0x10)
			fdc_set_motor(fdc, 1);
		else
			fdc_set_motor(fdc, 0);
#endif			
		break;
	case 3:	/* DCR */
		fprintf(tracef, "FDC DCR %02X [", val);
		if (!(val & 4))
			fprintf(tracef, "WCOMP");
		switch(val & 3) {
		case 0:
			fprintf(tracef, "500K MFM RPM");
			break;
		case 1:
			fprintf(tracef, "250K MFM");
			break;
		case 2:
			fprintf(tracef, "250K MFM RPM");
			break;
		case 3:
			fprintf(tracef, "INVALID");
		}
		fprintf(tracef, "]\n");
		fdc_write_drr(fdc, val & 3);	/* TODO: review */
		break;
	case 4:	/* TC */
		fdc_set_terminal_count(fdc, 0);
		fdc_set_terminal_count(fdc, 1);
		fprintf(tracef, "FDC TC\n");
		break;
	case 5:	/* RESET */
		fprintf(tracef, "FDC RESET\n");
		break;
	default:
		fprintf(tracef, "FDC bogus %02X->%02X\n", addr, val);
	}
}

static uint8_t fdc_read(uint8_t addr)
{
	uint8_t val = 0x78;
	switch(addr) {
	case 0:	/* Status*/
		fprintf(tracef, "FDC Read Status: ");
		val = fdc_read_ctrl(fdc);
		break;
	case 1:	/* Data */
		fprintf(tracef, "FDC Read Data: ");
		val = fdc_read_data(fdc);
		break;
	case 4:	/* TC */
		fprintf(tracef, "FDC TC: ");
		break;
	case 5:	/* RESET */
		fprintf(tracef, "FDC RESET: ");
		break;
	default:
		fprintf(tracef, "FDC bogus read %02X: ", addr);
	}
	fprintf(tracef, "%02X\n", val);
	return val;
}


static uint32_t z512_wdog;

static uint8_t z512_read(uint8_t addr)
{
	return z512_control;
}

static void z512_write(uint8_t addr, uint8_t val)
{
	uint8_t old = z512_control;
	z512_control = val;
	if ((old & 0x1F) != (val & 0x1f)) {
		unsigned int b = 7372800;
		if (val & 0x10)
			b /= 3;
		if (val & 0x08)
			b >>= 8;
		if (val & 0x04)
			b >>= 4;
		if (val & 0x02)
			b >>= 2;
		if (val & 0x01)
			b >>= 1;
		if (trace & TRACE_SIO)
			fprintf(tracef, "Z512 SIO serial clock: %d\n", b);
	}
}

static void z512_write_wd(uint8_t addr, uint8_t val)
{
	/* 1.6 seconds */
	z512_wdog = 3200;
}

/* PS/2 keyboard and mouse - only keyboard bits for now */

static uint8_t ps2_read(void)
{
	uint8_t r = 0x00;
	if (ps2_get_clock(ps2))
		r |= 0x04;
	if (ps2_get_data(ps2))
		r |= 0x08;
	return r;
}

static void ps2_write(uint8_t val)
{
	ps2_set_lines(ps2, !!(val & 0x01) , !!(val & 0x02));
}

static uint8_t io_read_2014(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(tracef, "read %02x\n", addr);
	if (copro && (addr & 0xFC) == 0xBC)
		return z80copro_ioread(copro, addr);
	if ((addr & 0xFF) == 0xBA) {
		return 0xCC;
	}
	if (zxkey && (addr & 0xFC) == 0xFC)
		return zxkey_scan(zxkey, addr);

	addr &= 0xFF;

	if (addr >= 0x80 && addr <= 0x9F && have_kio)
		return kio_read(addr & 0x1F);
	if (addr >= 0x48 && addr < 0x50) 
		return fdc_read(addr & 7);
	if ((addr == 0x42 || addr == 0x43) && amd9511)
		return amd9511_read(amd9511, addr);
	if ((addr >= 0xA0 && addr <= 0xA7) && acia && acia_narrow == 1)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow == 2)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0x87) && sio2 && !have_kio)
		return sio2_read(addr & 3);
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x27 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && have_wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr >= 0x68 && addr <= 0x6F && have_pio)
		return pio_read2(addr & 3);

	if (addr == 0xBB && ps2)
		return ps2_read();
	if (addr == 0xC0 && rtc)
		return rtc_read(rtc);
	/* Scott Baker is 0x90-93, suggested defaults for the
	   Stephen Cousins boards at 0x88-0x8B. No doubt we'll get
	   an official CTC board at another address  */
	if (addr >= 0x88 && addr <= 0x8B && have_ctc)
		return ctc_read(addr & 3);
	if ((addr == 0x98 || addr == 0x99) && vdp)
		return tms9918a_read(vdp, addr & 1);
	if (addr >= 0xA0 && addr <= 0xA7 && have_16x50)
		return uart_read(&uart[0], addr & 7);
	if (addr == 0x6D && is_z512)
		return z512_read(addr);
	if (trace & TRACE_UNK)
		fprintf(tracef, "Unknown read from port %04X\n", addr);
	return 0x78;	/* 78 is what my actual board floats at */
}

static void io_write_2014(uint16_t addr, uint8_t val, uint8_t known)
{
	if (trace & TRACE_IO)
		fprintf(tracef, "write %02x <- %02x\n", addr, val);

	if (copro && (addr & 0xFC) == 0xBC) {
		z80copro_iowrite(copro, addr, val);
		return;
	}
	if ((addr & 0xFF) == 0xBA) {
		/* Quart */
		return;
	}
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x9F && have_kio)
		kio_write(addr & 0x1F, val);
	else if (addr >= 0x48 && addr < 0x50)
		fdc_write(addr & 7, val);
	else if ((addr == 0x42 || addr == 0x43) && amd9511)
		amd9511_write(amd9511, addr, val);
	else if ((addr >= 0xA0 && addr <= 0xA7) && acia && acia_narrow == 1)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow == 2)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x80 && addr <= 0x87) && sio2 && !have_kio)
		sio2_write(addr & 3, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x27 && ide == 2)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && have_wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr >= 0x68 && addr <= 0x6F && have_pio)
		pio_write2(addr & 3, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (bank512 && addr >= 0x78 && addr <= 0x7B) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(tracef, "Bank %d set to %d\n", addr & 3, val);
	} else if (bank512 && addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_512)
			fprintf(tracef, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0xBB && ps2)
		ps2_write(val);
	else if (addr == 0xC0 && rtc)
		rtc_write(rtc, val);
	else if (addr >= 0x88 && addr <= 0x8B && have_ctc)
		ctc_write(addr & 3, val);
	else if ((addr == 0x98 || addr == 0x99) && vdp)
		tms9918a_write(vdp, addr & 1, val);
	else if (addr >= 0xA0 && addr <= 0xA7 && have_16x50)
		uart_write(&uart[0], addr & 7, val);
	else if (addr == 0x6D && is_z512)
		z512_write(addr, val);
	else if (addr == 0x6F && is_z512)
		z512_write_wd(addr, val);
	/* The switchable/pageable ROM is not very well decoded */
	else if (switchrom && (addr & 0x7F) >= 0x38 && (addr & 0x7F) <= 0x3F)
		toggle_rom();
	else if (addr == 0xFD) {
		trace &= 0xFF00;
		trace |= val;
		fprintf(tracef, "trace set to %04X\n", trace);
	} else if (addr == 0xFE) {
		trace &= 0xFF;
		trace |= val << 8;
		printf("trace set to %d\n", trace);
	} else if (!known && (trace & TRACE_UNK))
		fprintf(tracef, "Unknown write to port %04X of %02X\n", addr, val);
}

static uint8_t io_read_4(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(tracef, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x83)
		return sio2_read((addr & 3) ^ 1);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && have_wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0xC0 && rtc)
		return rtc_read(rtc);
	if (addr >= 0x88 && addr <= 0x8B)
		return ctc_read(addr & 3);
	if (trace & TRACE_UNK)
		fprintf(tracef, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_4(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(tracef, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x83)
		sio2_write((addr & 3) ^ 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x28 && addr <= 0x2C && have_wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (bank512 && addr >= 0x78 && addr <= 0x7B) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(tracef, "Bank %d set to %d\n", addr & 3, val);
	} else if (bank512 && addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_512)
			fprintf(tracef, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0xC0 && rtc)
		rtc_write(rtc, val);
	else if (addr >= 0x88 && addr <= 0x8B)
		ctc_write(addr & 3, val);
	else if (addr == 0xFC) {
		putchar(val);
		fflush(stdout);
	} else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(tracef, "Unknown write to port %04X of %02X\n", addr, val);
}

static uint8_t io_read_5(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(tracef, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr >= 0x18 && addr <= 0x1B)
		return sio2_read((addr & 3) ^ 1);
	if ((addr >= 0x90 && addr <= 0x97) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && have_wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0xC0 && rtc)
		return rtc_read(rtc);
	if (addr >= 0x10 && addr <= 0x13)
		return ctc_read(addr & 3);
	if (addr >= 0xEE && addr <= 0xF1)
		return z84c15_read(addr);
	if (addr >= 0x1C && addr <= 0x1F)
		return pio_read(addr & 3);
	if (trace & TRACE_UNK)
		fprintf(tracef, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_5(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(tracef, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr >= 0x18 && addr <= 0x1B)
		sio2_write((addr & 3) ^ 1, val);
	else if ((addr >= 0x90 && addr <= 0x97) && ide)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x28 && addr <= 0x2C && have_wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (bank512 && addr >= 0x78 && addr <= 0x7B) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(tracef, "Bank %d set to %d\n", addr & 3, val);
	} else if (bank512 && addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_512)
			fprintf(tracef, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0xC0 && rtc)
		rtc_write(rtc, val);
	else if (addr >= 0x10 && addr <= 0x13)
		ctc_write(addr & 3, val);
	else if (addr >= 0x1C && addr <= 0x1F)
		pio_write(addr & 3, val);
	else if ((addr >= 0xEE && addr <= 0xF1) || addr == 0xF4)
		z84c15_write(addr, val);
	else if (addr == 0xFC) {
		putchar(val);
		fflush(stdout);
	} else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(tracef, "Unknown write to port %04X of %02X\n", addr, val);
}

static void io_write_1(uint16_t addr, uint8_t val)
{
	if ((addr & 0xFF) == 0x38) {
		val &= 0x81;
		if (val != port38 && (trace & TRACE_ROM))
			fprintf(tracef, "Bank set to %02X\n", val);
		port38 = val;
		return;
	}
	io_write_2014(addr, val, 0);
}

static void io_write_3(uint16_t addr, uint8_t val)
{
	switch(addr & 0xFF) {
	case 0xf9:
		sbc64_cpld_uart_tx(val);
		break;
	case 0xf8:
		sbc64_cpld_uart_ctrl(val);
		break;
	case 0x1f:
		sbc64_cpld_bankreg(val);
		break;
	default:
		io_write_2014(addr, val, 0);
		break;
	}
}

static uint8_t io_read_2(uint16_t addr)
{
	switch (addr & 0xFC) {
	case 0x28:
		return 0x80;
	default:
		return io_read_2014(addr);
	}
}

static uint8_t io_read_3(uint16_t addr)
{
	switch(addr & 0xFF) {
	case 0xf9:
		return sbc64_cpld_uart_rx();
	case 0xf8:
		return sbc64_cpld_uart_status();
	default:
		return io_read_2014(addr);
	}
}

static void io_write_2(uint16_t addr, uint8_t val)
{
	uint16_t r = addr & 0xFC;	/* bits 0/1 not decoded */
	uint8_t known = 0;

	switch (r) {
	case 0x08:
		if (val & 1)
			printf("[LED off]\n");
		else
			printf("[LED on]\n");
		return;
	case 0x20:
		if (trace & TRACE_UART) {
			if (val & 1)
				fprintf(tracef, "[RTS high]\n");
			else
				fprintf(tracef, "[RTS low]\n");
		}
		known = 1;
		break;
	case 0x28:
		known = 1;
		break;
	case 0x30:
		if (trace & TRACE_ROM)
			fprintf(tracef, "RAM Bank set to %02X\n", val);
		port30 = val;
		return;
	case 0x38:
		if (trace & TRACE_ROM)
			fprintf(tracef, "ROM Bank set to %02X\n", val);
		port38 = val;
		return;
	}
	io_write_2014(addr, val, known);
}

static uint8_t io_read_micro80(uint16_t addr)
{
	uint8_t r = addr & 0xFF;
	if (r >= 0x10 && r <= 0x13)
		return ctc_read(addr & 3);
	else if (r >= 0x18 && r <= 0x1B)
		return sio2_read((r & 3) ^ 1);
	else if (r >= 0x1C && r <= 0x1F)
		return pio_read(r & 3);
	else if (r >= 0xEE && r <= 0xF1)
		return z84c15_read(r);
	else if (r >= 0x90 && r <= 0x97)
		return my_ide_read(r & 7);
	else if (trace & TRACE_UNK)
		fprintf(tracef, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_micro80(uint16_t addr, uint8_t val)
{
	uint16_t r = addr & 0xFF;
	if (r >= 0x10 && r <= 0x13)
		ctc_write(addr & 3, val);
	else if (r >= 0x18 && r <= 0x1B)
		sio2_write((r & 3) ^ 1, val);
	else if (r >= 0x1C && r <= 0x1F)
		pio_write(r & 3, val);
	else if ((r >= 0xEE && r <= 0xF1) || r == 0xF4)
		z84c15_write(r, val);
	else if (r >= 0x90 && r <= 0x97)
		my_ide_write(r & 0x07, val);
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(tracef, "Unknown write to port %04X of %02X\n", addr, val);
}

static void io_write_pdog(uint16_t addr, uint8_t val)
{
	if ((addr & 0xFB) == 0x78) {	/* 78 or 7C */
		if (cpuboard == CPUBOARD_PDOG512)
			val &= 0x8F;
		pick_bank = val;
	} else
		io_write_2014(addr, val, 0);
}


void io_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z80:
		io_write_2014(addr, val, 0);
		break;
	case CPUBOARD_SC108:
		io_write_1(addr, val);
		break;
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		io_write_2(addr, val);
		break;
	case CPUBOARD_Z80SBC64:
	case CPUBOARD_ZRCC:
		io_write_3(addr, val);
		break;
	case CPUBOARD_EASYZ80:
		io_write_4(addr, val);
		break;
	case CPUBOARD_MICRO80:
		io_write_micro80(addr, val);
		break;
	case CPUBOARD_TINYZ80:
		io_write_5(addr, val);
		break;
	case CPUBOARD_PDOG128:
	case CPUBOARD_PDOG512:
		io_write_pdog(addr, val);
		break;
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

uint8_t io_read(int unused, uint16_t addr)
{
	switch (cpuboard) {
	case CPUBOARD_Z80:
	case CPUBOARD_SC108:
	case CPUBOARD_PDOG128:
	case CPUBOARD_PDOG512:
		return io_read_2014(addr);
	case CPUBOARD_SC114:
	case CPUBOARD_SC121:
		return io_read_2(addr);
	case CPUBOARD_Z80SBC64:
	case CPUBOARD_ZRCC:
		return io_read_3(addr);
	case CPUBOARD_EASYZ80:
		return io_read_4(addr);
	case CPUBOARD_MICRO80:
		return io_read_micro80(addr);
	case CPUBOARD_TINYZ80:
		return io_read_5(addr);
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

static void poll_irq_event(void)
{
	if (have_im2) {
		if (acia)
			acia_check_irq(acia);
		uart_check_irq(&uart[0]);
		if (!live_irq) {
			if (!sio2_check_im2(sio))
			        if (!sio2_check_im2(sio + 1))
					ctc_check_im2();
		}
		/* TMS9918A no IM2 handling */
	} else {
		if (acia)
			acia_check_irq(acia);
		uart_check_irq(&uart[0]);
		if (!sio2_check_im2(sio))
		      sio2_check_im2(sio + 1);
		ctc_check_im2();
		if (vdp && tms9918a_irq_pending(vdp))
			Z80INT(&cpu_z80, 0xFF);
	}
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(tracef, "RETI\n");
	if (have_im2) {
		switch(live_irq) {
		case IRQ_SIOA:
			sio2_reti(sio);
			break;
		case IRQ_SIOB:
			sio2_reti(sio + 1);
			break;
		case IRQ_CTC:
		case IRQ_CTC + 1:
		case IRQ_CTC + 2:
		case IRQ_CTC + 3:
			ctc_reti(live_irq - IRQ_CTC);
			break;
		}
	} else {
		/* If IM2 is not wired then all the things respond at the same
		   time. I think they can also fight over the vector but ignore
		   that */
		if (sio2) {
			sio2_reti(sio);
			sio2_reti(sio + 1);
		}
		if (have_ctc || have_kio) {
			ctc_reti(0);
			ctc_reti(1);
			ctc_reti(2);
			ctc_reti(3);
		}
	}
	live_irq = 0;
	poll_irq_event();
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	emulator_done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "rc2014: [-a] [-A] [-b] [-c] [-f] [-i idepath] [-R] [-m mainboard] [-r rompath] [-e rombank] [-s] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	int rombank = 0;
	char *rompath = "rc2014.rom";
	char *sdpath = NULL;
	char *idepath = NULL;
	char *copro_rom;
	int save = 0;
	int have_acia = 0;
	int indev;
	char *patha = NULL, *pathb = NULL;

#define INDEV_ACIA	1
#define INDEV_SIO	2
#define INDEV_CPLD	3
#define INDEV_16C550A	4
#define INDEV_KIO	5

	tracef = stderr;

	uint8_t *p = ramrom;
	while (p < ramrom + sizeof(ramrom))
		*p++= rand();

	while ((opt = getopt(argc, argv, "19Aabcd:D:e:fF:i:I:km:pPr:sRS:Tuw8C:Zz")) != -1) {
		switch (opt) {
		case 'a':
			have_acia = 1;
			indev = INDEV_ACIA;
			acia_narrow = 0;
			sio2 = 0;
			break;
		case 'A':
			have_acia = 1;
			acia_narrow = 1;
			indev = INDEV_ACIA;
			sio2_input = 0;
			break;
		case '8':
			have_acia = 1;
			acia_narrow = 2;
			indev = INDEV_ACIA;
			sio2 = 0;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 's':
			sio2 = 1;
			sio2_input = 1;
			indev = INDEV_SIO;
			if (!acia_narrow)
				have_acia = 0;
			break;
		case 'S':
			sdpath = optarg;
			have_pio = 1;
			break;
		case 'e':
			rombank = atoi(optarg);
			break;
		case 'b':
			bank512 = 1;
			switchrom = 0;
			rom = 0;
			break;
		case 'p':
			bankenable = 1;
			break;
		case 'P':
			have_ps2 = 1;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
			break;
		case 'I':
			ide = 2;
			idepath = optarg;
			break;
		case 'c':
			have_ctc = 1;
			break;
		case 'u':
		case '1':
			have_16x50 = 1;
			indev = INDEV_16C550A;
			break;
		case 'k':
			have_kio = 1;
			break;
		case 'm':
			/* Default Z80 board */
			if (strcmp(optarg, "z80") == 0)
				cpuboard = CPUBOARD_Z80;
			else if (strcmp(optarg, "sc108") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_SC108;
			} else if (strcmp(optarg, "sc114") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_SC114;
			} else if (strcmp(optarg, "z80sbc64") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_Z80SBC64;
				bankreg[0] = 3;
			} else if (strcmp(optarg, "z80mb64") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_Z80SBC64;
				bankreg[0] = 3;
				/* Triple RC2014 rate */
				tstate_steps *= 3;
			} else if (strcmp(optarg, "easyz80") == 0) {
				bank512 = 1;
				cpuboard = CPUBOARD_EASYZ80;
				switchrom = 0;
				rom = 0;
				have_acia = 0;
				have_ctc = 1;
				sio2 = 1;
				sio2_input = 1;
				have_im2 = 1;
				tstate_steps = 400;
			} else if (strcmp(optarg, "sc121") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_SC121;
				sio2 = 1;
				sio2_input = 1;
				have_ctc = 1;
				rom = 0;
				have_acia = 0;
				have_im2 = 1;
				/* FIXME: SC122 is four ports */
			} else if (strcmp(optarg, "micro80") == 0) {
				cpuboard = CPUBOARD_MICRO80;
				have_ctc = 1;
				sio2 = 1;
				sio2_input = 1;
				have_im2 = 1;
				have_acia = 0;
				rom = 1;
				switchrom = 0;
				tstate_steps = 800;	/* 16MHz */
			} else if (strcmp(optarg, "zrcc") == 0) {
				switchrom = 0;
				bank512 = 0;
				cpuboard = CPUBOARD_ZRCC;
				bankreg[0] = 3;
				/* 22MHz CPU */
				tstate_steps *= 3;
			} else if (strcmp(optarg, "tinyz80") == 0) {
				bank512 = 1;
				cpuboard = CPUBOARD_TINYZ80;
				switchrom = 0;
				rom = 0;
				have_acia = 0;
				have_ctc = 1;
				sio2 = 1;
				sio2_input = 1;
				have_im2 = 1;
				tstate_steps = 500;
			} else if (strcmp(optarg, "pdog128") == 0) {
				cpuboard = CPUBOARD_PDOG128;
				switchrom = 0;
				bank512 = 0;
				romsize = 131072;
				rom = 1;
			} else if (strcmp(optarg, "pdog512") == 0) {
				cpuboard = CPUBOARD_PDOG512;
				switchrom = 0;
				bank512 = 0;
				romsize = 524288;
				rom = 1;
			} else {
				fputs("rc2014: supported cpu types z80, easyz80, sc108, sc114, sc121, z80sbc64, z80mb64.\n",
						stderr);
				exit(EXIT_FAILURE);
			}
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'D':
			tracef = fopen(optarg, "w");
			if (! tracef) {
				fprintf(stderr, "rc2014: can't open trace file.\n");
				exit(1);
			}
		case 'f':
			fast = 1;
			break;
		case 'R':
			rtc = rtc_create();
			break;
		case 'w':
			have_wiznet = 1;
			break;
		case 'C':
			have_copro = 1;
			copro_rom = optarg;
			break;
		case 'F':
			if (pathb) {
				fprintf(stderr, "rc2014: too many floppy disks specified.\n");
				exit(1);
			}
			if (patha)
				pathb = optarg;
			else
				patha = optarg;
			break;
		case 'Z':
			is_z512 = 1;
			break;
		case 'z':
			zxkey = zxkey_create();
			break;
		case 'T':
			have_tms = 1;
			break;
		case '9':
			if (amd9511 == NULL)
				amd9511 = amd9511_create();
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (have_kio) {
		sio2 = 1;
		have_ctc = 0;
		have_pio = 0;
		have_im2 = 1;
		indev = INDEV_SIO;
	}
	if (cpuboard == CPUBOARD_Z80SBC64 || cpuboard == CPUBOARD_ZRCC) {
		have_cpld_serial = 1;
		indev = INDEV_CPLD;
	} else if (have_acia == 0 && sio2 == 0 && have_16x50 == 0 ) {
		if (cpuboard != 3) {
			fprintf(stderr, "rc2014: no UART selected, defaulting to 68B50\n");
			have_acia = 1;
			indev = INDEV_ACIA;
		}
	}
	if (rom == 0 && bank512 == 0) {
		fprintf(stderr, "rc2014: no ROM\n");
		exit(EXIT_FAILURE);
	}

	if (rom && cpuboard != CPUBOARD_Z80SBC64 && cpuboard != CPUBOARD_ZRCC) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		bankreg[0] = 0;
		bankreg[1] = 1;
		bankreg[2] = 32;
		bankreg[3] = 33;
		if (lseek(fd, 8192 * rombank, SEEK_SET) < 0) {
			perror("lseek");
			exit(1);
		}
		if (read(fd, ramrom, romsize) < 8192) {
			fprintf(stderr, "rc2014: short rom '%s'.\n", rompath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}
	/* ZRCC has a 64byte wired in CPLD ROM so we don't use the ROM
	   option in the same way */

	/* SBC64 has battery backed RAM and what really happens is that you
	   use the CPLD to load a loader and then to load ZMON which in turn
	   hides in bank 3. This is .. tedious .. with an emulator so we allow
	   you to load bank 3 with the CPLD loader (so you can play with it
	   and loading Zmon, or with Zmon loaded, or indeed anything else in
	   the reserved space notably SCMonitor).

	   Quitting saves the memory state. If you screw it all up then use
	   the loader bin file instead to get going again.

	   Mark states read only with chmod and it won't save back */

	if (cpuboard == CPUBOARD_Z80SBC64) {
		int len;
		save = 1;
		fd = open(rompath, O_RDWR);
		if (fd == -1) {
			save = 0;
			fd = open(rompath, O_RDONLY);
			if (fd == -1) {
				perror(rompath);
				exit(EXIT_FAILURE);
			}
		}
		/* Could be a short bank 3 save for bootstrapping or a full
		   save from the emulator exit */
		len = read(fd, ramrom, 4 * 0x8000);
		if (len < 4 * 0x8000) {
			if (len < 255) {
				fprintf(stderr, "rc2014:short ram '%s'.\n", rompath);
				exit(EXIT_FAILURE);
			}
			memmove(ramrom + 3 * 0x8000, ramrom, 32768);
			printf("[loaded bank 3 only]\n");
		}
	}

	if (cpuboard == CPUBOARD_MICRO80 || cpuboard == CPUBOARD_TINYZ80)
		z84c15_init();

	if (bank512) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom, 524288) != 524288) {
			fprintf(stderr, "rc2014: banked rom image should be 512K.\n");
			exit(EXIT_FAILURE);
		}
		bankenable = 1;
		close(fd);
	}

	if (ide == 1 ) {
		ide0 = ide_allocate("cf");
		if (ide0) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			}
			if (ide_attach(ide0, 0, ide_fd) == 0) {
				ide = 1;
				ide_reset_begin(ide0);
			}
		} else
			ide = 0;
	}

	/* FIXME: merge IDE handling once cf is a driver */
	if (ide == 2) {
		ppide = ppide_create("ppide");
		int ide_fd = open(idepath, O_RDWR);
		if (ide_fd == -1) {
			perror(idepath);
			ide = 0;
		} else
			ppide_attach(ppide, 0, ide_fd);
		if (trace & TRACE_PPIDE)
			ppide_trace(ppide, 1);
	}
	/* SD mapping */
	if (cpuboard == CPUBOARD_MICRO80) {
		sd_clock = 0x04;
		sd_mosi = 0x02;
		sd_port = 1;
		sd_miso = 1;
	}
	if (sdpath) {
		sdcard = sd_create("sd0");
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
		if (trace & TRACE_SD)
			sd_trace(sdcard, 1);
	}

	if (have_acia) {
		acia = acia_create();
		if (trace & TRACE_ACIA)
			acia_trace(acia, 1);
	}
	if (rtc && (trace & TRACE_RTC))
		rtc_trace(rtc, 1);
	if (sio2)
		sio_reset();
	if (have_ctc)
		ctc_init();
	if (have_pio)
		pio_reset();
	if (have_kio) {
		sio_reset();
		ctc_init();
		pio_reset();
	}
	if (have_16x50)
		uart_init(&uart[0], indev == INDEV_16C550A ? 1: 0);
	if (have_tms) {
		vdp = tms9918a_create();
		tms9918a_trace(vdp, !!(trace & TRACE_TMS9918A));
		vdprend = tms9918a_renderer_create(vdp);
	}
	if (have_ps2) {
		ps2 = ps2_create(7);
		ps2_trace(ps2, trace & TRACE_PS2);
	}

	if (have_wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

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

	if (have_copro) {
		int eprom_fd;
		copro = z80copro_create();
		eprom_fd = open(copro_rom, O_RDONLY);
		if (eprom_fd == -1) {
			perror(copro_rom);
			exit(1);
		}
		if (read(eprom_fd, z80copro_eprom(copro), 32768) != 32768) {
			fprintf(stderr, "%s: should be 32K.\n", copro_rom);
			exit(1);
		}
		close(eprom_fd);
		/* Assume same speed as main processor */
		copro->tstates = tstate_steps;
		z80copro_trace(copro, (trace >> 17) & 3);
	}

	switch(indev) {
	case INDEV_ACIA:
		acia_set_input(acia, 1);
		break;
	case INDEV_SIO:
		sio2_input = 1;
		break;
	case INDEV_CPLD:
		break;
	case INDEV_16C550A:
		break;
	default:
		fprintf(stderr, "Invalid input device %d.\n", indev);
	}

	pio_reset();

	/* 2.5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON | ECHO);
		term.c_cc[VMIN] = 0;
		term.c_cc[VTIME] = 1;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VSTOP] = 0;
		tcsetattr(0, TCSADRAIN, &term);
	}

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;
	cpu_z80.trace = z80_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 365 cycles per I/O check, do that 50 times then poll the
	   slow stuff and nap for 20ms to get 50Hz on the TMS99xx */
	while (!emulator_done) {
		if (cpu_z80.halted && ! cpu_z80.IFF1) {
			/* HALT with interrupts disabled, so nothing left
			   to do, so exit simulation. If NMI was supported,
			   this might have to change. */
			emulator_done = 1;
			break;
		}
		int i;
		/* 36400 T states for base RC2014 - varies for others */
		for (i = 0; i < 50; i++) {
			int j;
			for (j = 0; j < 100; j++) {
				Z80ExecuteTStates(&cpu_z80, (tstate_steps + 5)/ 10);
				if (copro)
					z80copro_run(copro);
				if (ps2)
					ps2_event(ps2, (tstate_steps + 5) / 10);
			}
			if (acia)
				acia_timer(acia);
			if (sio2)
				sio2_timer();
			if (have_16x50)
				uart_event(&uart[0]);
			if (have_cpld_serial)
				sbc64_cpld_timer();
			if (have_ctc || have_kio) {
				if (cpuboard != CPUBOARD_MICRO80)
					ctc_tick(tstate_steps);
				else	/* Micro80 it's not off the CPU clock */
					ctc_tick(184);
			}
			if (cpuboard == CPUBOARD_EASYZ80 || cpuboard == CPUBOARD_TINYZ80) {
				/* Feed the uart clock into the CTC */
				int c;
				/* 10Mhz so calculate for 500 tstates.
				   CTC 2 runs at half uart clock */
				for (c = 0; c < 46; c++) {
					ctc_receive_pulse(0);
					ctc_receive_pulse(1);
					ctc_receive_pulse(2);
					ctc_receive_pulse(0);
					ctc_receive_pulse(1);
				}
			}
			fdc_tick(fdc);
			/* We want to run UI events regularly it seems */
			ui_event();
		}

		if (is_z512 && (z512_control & 0x20)) {
			if (z512_wdog <= 5) {
				fprintf(stderr, "Watchdog reset.\n");
				emulator_done = 1;
				break;
			}
			z512_wdog -= 5;
		}
		/* TODO: coprocessor int to main if we implement it */

		/* 50Hz which is near enough */
		if (vdp) {
			tms9918a_rasterize(vdp);
			tms9918a_render(vdprend);
		}
		if (have_wiznet)
			w5100_process(wiz);
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending Z80 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
			if (!live_irq || !have_im2)
				poll_irq_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z80.IFF1|cpu_z80.IFF2))
				int_recalc = 0;
		}
	}
	if (cpuboard == 3 && save) {
		lseek(fd, 0L, SEEK_SET);
		if (write(fd, ramrom, 0x8000 * 4) != 0x8000 * 4) {
			fprintf(stderr, "rc2014: state save failed.\n");
			exit(1);
		}
		close(fd);
	}
	fd_eject(drive_a);
	fd_eject(drive_b);
	fdc_destroy(&fdc);
	fd_destroy(&drive_a);
	fd_destroy(&drive_b);
	exit(0);
}
