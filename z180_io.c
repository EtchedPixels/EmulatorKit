/*
 *	I/O model for the Z180
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "serialdevice.h"
#include "libz180/z180.h"
#include "z180_io.h"

struct z180_asci {
	uint8_t tdr;
	uint8_t rdr;
	uint8_t stat;
	uint8_t cntla;
	uint8_t cntlb;
	uint16_t tc;
	uint8_t ecr;
	bool irq;
	struct serial_device *dev;

};

struct z180_prt {
	uint16_t tmdr;
	uint16_t rldr;
	uint8_t latch;
};

struct z180_io {
	/* CSIO */
	uint8_t cntr;
	uint8_t trdr_r;
	uint8_t trdr_w;
	/* Refresh */
	uint8_t rcr;
	/* MMU */
	uint8_t cbar;
	uint8_t cbr;
	uint8_t bbr;
	/* I/O base etc */
	uint8_t icr;
	uint8_t itc;
	uint8_t il;
	/* Serial ports */
	struct z180_asci asci[2];
	/* Programmable timer */
	struct z180_prt prt[2];
	uint8_t tcr;
	uint8_t frc;
	/* Internal state used for clock divide by 20 */
	unsigned int clockmod;

	/* DMA engine */
	uint32_t sar0;
	uint32_t dar0;
	uint16_t bcr0;

	uint32_t mar1;
	uint16_t iar1;
	uint16_t bcr1;

	uint8_t dstat;
	uint8_t dmode;
	uint8_t dcntl;

	/* CPU control */
	uint8_t ccr;
	uint8_t cmr;

	/* Internal used for steal mode */
	uint8_t dma_state0;

	/* CPU internal context */
	Z180Context *cpu;
	/* Internal IRQ management */
	uint8_t irqpend;
	uint8_t vector;

	unsigned clock;
	int trace;
};

static const char *regnames[64] = {
	/* ASCI = 0 */
	"CNTLA0",
	"CNTLA1",
	"CNTLB0",
	"CNTLB1",
	"STAT0",
	"STAT1",
	"TDR0",
	"TDR1",
	"RDR0",
	"RDR1",
	/* CSIO = 0x0A */
	"CNTR",
	"TRD",
	/* Timer = 0x0C */
	"TMDR0L",
	"TMDR0H",
	"RLDR0L",
	"RLDR0H",
	"TCR",
	/* Unused = 0x11 */
	"RES 11H",
	"ECR0",		/* S180/L180 only */
	"ECR1",		/*   ""    ""     */
	/* Timer = 0x14 */
	"TMDR1L",
	"TMDR1H",
	"RLDR1L",
	"RLDR1H",
	/* Free running counter = 0x18 */
	"FRC",
	/* Unused 0x19->0x1F */
	"RES 19H",
	"TCL0",		/* S180/L180 only */
	"TCH0",    	/*   ""    ""     */
	"TCL1",    	/*   ""    ""     */
	"TCH1",		/*   ""    ""     */
	"CMR",		/*   ""    ""     */
	"CCR",		/*   ""    ""     */
	/* 0x20 DMAC */
	"SAR0L",
	"SAR0H",
	"SAR0B",
	"DAR0L",
	"DAR0H",
	"DAR0B",
	"BCR0L",
	"BCR0H",
	"MAR1L",
	"MAR1H",
	"MAR1B",
	"IAR1L",
	"IAR1H",
	"RES 2DH",
	"BCR1L",
	"BCR1H",
	"DSTAT",
	"DMODE",
	"DCNTL",
	/* Misc = 0x33 */
	"IL",
	"ITC",
	"RES 35H",
	"RCR",
	"RES 37H",
	/* MMU = 0x38 */
	"CBR",
	"BBR",
	"CBAR",
	"RES 3BH",
	"RES 3CH",
	"RES 3DH",
	/* Control = 0x3E */
	"OMCR",
	"ICR"
};


static void z180_next_interrupt(struct z180_io *io)
{
	uint8_t live = io->irqpend & io->itc & 7;

	if (live & 1) {	/* IRQ is highest */
		Z180INT(io->cpu, io->vector);
		return;
	}
	if (live & 2) {
		Z180INT_IM2(io->cpu, io->il | 0x00);
		return;
	}
	if (live & 4) {
		Z180INT_IM2(io->cpu, io->il | 0x02);
		return;
	}
	/* Check for internal interrupts in priority order */
	if ((io->tcr & 0x50) == 0x50) {
		Z180INT_IM2(io->cpu, io->il | 0x04);
		return;
	}
	if ((io->tcr & 0xA0) == 0xA0) {
		Z180INT_IM2(io->cpu, io->il | 0x06);
		return;
	}
	if ((io->dstat & 0x44) == 0x04) {
		Z180INT_IM2(io->cpu, io->il | 0x08);
		return;
	}
	if ((io->dstat & 0x88) == 0x08) {
		Z180INT_IM2(io->cpu, io->il | 0x08);
		return;
	}
	if ((io->cntr & 0xC0) == 0xC0) {
		Z180INT_IM2(io->cpu, io->il | 0x0C);
		return;
	}
	if (io->asci[0].irq) {
		Z180INT_IM2(io->cpu, io->il | 0x0E);
		return;
	}
	if (io->asci[1].irq) {
		Z180INT_IM2(io->cpu, io->il | 0x10);
		return;
	}
	Z180NOINT(io->cpu);
}

void z180_interrupt(struct z180_io *io, uint8_t pin, uint8_t vec, bool on)
{
	io->irqpend &= ~(1 << pin);
	if (on)
		io->irqpend |= (1 << pin);

	/* Pin 0 is the INT0 line which acts like a Z80 */
	if (pin == 0)
		io->vector = vec;

	z180_next_interrupt(io);
}

static void z180_asci_recalc(struct z180_io *io, struct z180_asci *asci)
{
	asci->irq = 0;
	/* We don't check the error bits as we don't have any emulated errors */
	if ((asci->stat & 0x88) == 0x88)
		asci->irq = 1;
	if ((asci->stat & 0x03) == 0x03)
		asci->irq = 1;
	z180_next_interrupt(io);
}

static uint8_t z180_asci_read(struct z180_io *io, uint8_t addr)
{
	struct z180_asci *asci = &io->asci[addr & 1];
	switch(addr & 0xFE) {
	case 0x00:
		return asci->cntla;
	case 0x02:
		return asci->cntlb;
	case 0x04:
		return asci->stat;
	case 0x06:
		return asci->tdr;
	case 0x08:
		asci->stat &= 0x7F;		/* Clear RDRF */
		z180_asci_recalc(io, asci);
		return asci->rdr;
	default:	/* Can't happen */
		return 0xFF;
	}
}

static void z180_asci_state(struct z180_io *io, struct z180_asci *asci)
{
	unsigned baud;
	if (io->trace == 0)
		return;
	fprintf(stderr, "[ ASCI: ");
	if (asci->cntla & 0x80)
		fprintf(stderr, "MPE ");
	if (asci->cntla & 0x40)
		fprintf(stderr, "RE ");
	if (asci->cntla & 0x20)
		fprintf(stderr, "TE ");
	if (asci->cntlb & 0x80)
		fprintf(stderr, "MPBT ");
	if (asci->cntlb & 0x40)
		fprintf(stderr, "MP ");
	baud = io->clock;
	if (asci->cntlb & 0x20)
		baud /= 30;
	else
		baud /= 10;
	if (asci->cntlb & 0x08)
		baud /= 64;
	else
		baud /= 16;
	baud /= 1 << (asci->cntlb & 0x07);
	fprintf(stderr, "%d %c", baud, (asci->cntla & 0x04) ? '8':'7');
	if (asci->cntla & 0x02)
		fprintf(stderr, "%c", (asci->cntlb & 0x10) ? 'O':'E');
	else
		fprintf(stderr, "N");
	fprintf(stderr, "%d ]\n", 1 + (asci->cntla & 0x01));
}

static void z180_asci_write(struct z180_io *io, uint8_t addr, uint8_t val)
{
	struct z180_asci *asci = &io->asci[addr & 1];
	switch(addr & 0xFE) {
	case 0x00:
		asci->cntla = val;
		if (val & 0x08)
			asci->stat &= 0x8F;
		z180_asci_recalc(io, asci);
		break;
	case 0x02:
		asci->cntlb = val;
		z180_asci_state(io, asci);
		z180_asci_recalc(io, asci);
		break;
	case 0x04:
		asci->stat &= 0xF6;
		asci->stat |= val & 0x09;
		break;
	case 0x06:
		/* TDRE was high and tx was enabled */
		if ((asci->cntla & 0x20) && (asci->stat & 0x02)) {
			asci->dev->put(asci->dev, val);
			asci->stat &= ~0x02;
		}
		z180_asci_recalc(io, asci);
		break;
	case 0x08:
		/* TODO: strictly can be written when RDRF is off ?? */
		break;
	}
}

static void z180_asci_event(struct z180_io *io, struct z180_asci *asci)
{
	unsigned int r = asci->dev->ready(asci->dev);
	if (r & 2)
		asci->stat |= 0x02;
	if (r & 1) {
		asci->stat |= 0x80;
		asci->rdr = asci->dev->get(asci->dev);
	}
	z180_asci_recalc(io, asci);
}

static void z180_csio_begin(struct z180_io *io, uint8_t val)
{
	/* We should time this but for now just do a quick instant hack TODO */
	io->trdr_r = z180_csio_write(io, val);
	io->cntr |= 0x80;
	io->cntr &= ~0x30;
	z180_next_interrupt(io);
}

static void z180_prt_event(struct z180_io *io, struct z180_prt *prt, unsigned int clocks, unsigned int shift)
{
	/* Disabled */
	if (!(io->tcr & (1 << shift)))
		return;

	/* Not yet overflowed */
	if (prt->tmdr > clocks) {
		prt->tmdr -= clocks;
		return;
	}
	clocks -= prt->tmdr;	/* Cycles after the overflow */
	prt->tmdr = prt->rldr - clocks;	/* Set up with what is left */
	io->tcr |= 0x40 << shift;
}

bool z180_iospace(struct z180_io *io, uint16_t addr)
{
	if (addr & 0xFF00)
		return 0;
	if ((addr & 0xC0) == (io->icr & 0xC0))
		return 1;
	return 0;
}

static uint8_t z180_do_read(struct z180_io *io, uint8_t addr)
{
	addr &= 0x3F;
	switch(addr) {
	/* ASCI */
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
	case 0x08:
	case 0x09:
		return z180_asci_read(io, addr);
	/* CSIO */
	case 0x0A:
		return io->cntr;
	case 0x0B:
		io->cntr &= 0x7F;
		z180_next_interrupt(io);
		return io->trdr_r;
	/* Timers */
	case 0x0C:
		io->prt[0].latch = io->prt[0].tmdr >> 8;
		io->tcr &= ~0x40;
		z180_next_interrupt(io);
		return io->prt[0].tmdr;
	case 0x0D:
		io->tcr &= ~0x40;
		z180_next_interrupt(io);
		return io->prt[0].latch;
	case 0x0E:
		return io->prt[0].rldr;
	case 0x0F:
		return io->prt[0].rldr >> 8;
	case 0x10:
		return io->tcr;
	case 0x12:
		return io->asci[0].ecr;
	case 0x13:
		return io->asci[1].ecr;
	case 0x14:
		io->prt[1].latch = io->prt[1].tmdr >> 8;
		io->tcr &= ~0x80;
		z180_next_interrupt(io);
		return io->prt[1].tmdr;
	case 0x15:
		io->tcr &= ~0x80;
		z180_next_interrupt(io);
		return io->prt[1].latch;
	case 0x16:
		return io->prt[1].rldr;
	case 0x17:
		return io->prt[1].rldr >> 8;
	case 0x18:
		return io->frc;
	case 0x1A:
		return io->asci[0].tc;
	case 0x1B:
		return io->asci[0].tc >> 8;
	case 0x1C:
		return io->asci[1].tc;
	case 0x1D:
		return io->asci[1].tc >> 8;
	case 0x1E:
		return io->cmr | 0x7F;
	case 0x1F:
		return io->ccr;
	/* DMA */
	case 0x20:
		return io->sar0;
	case 0x21:
		return io->sar0 >> 8;
	case 0x22:
		return io->sar0 >> 16;
	case 0x23:
		return io->dar0;
	case 0x24:
		return io->dar0 >> 8;
	case 0x25:
		return io->dar0 >> 16;
	case 0x26:
		return io->bcr0;
	case 0x27:
		return io->bcr0 >> 8;
	case 0x28:
		return io->mar1;
	case 0x29:
		return io->mar1 >> 8;
	case 0x2A:
		return io->mar1 >> 16;
	case 0x2B:
		return io->iar1;
	case 0x2C:
		return io->iar1 >> 8;
	case 0x2E:
		return io->bcr1;
	case 0x2F:
		return io->bcr1 >> 8;
	case 0x30:
		return io->dstat | 0x30;
	case 0x31:
		return io->dmode;
	case 0x32:
		return io->dcntl;
	/* IL */
	case 0x33:
		return io->il;
	case 0x34:
		io->itc &= ~0xC0;
		switch(io->cpu->UFO) {
		case 2:
			io->itc |= 0xC0;
			break;
		case 1:
			io->itc |= 0x80;
			break;
		}
		return io->itc;
	/* Refresh */
	case 0x36:
		return io->rcr;
	/* MMU */
	case 0x38:
		return io->cbr;
	case 0x39:
		return io->bbr;
	case 0x3A:
		return io->cbar;
	/* IO Control */
	case 0x3F:	/* ICR */
		return io->icr;
	default:
		fprintf(stderr, "Unemulated Z180 I/O Read %s(0x%02X)\n",
				regnames[addr], addr);
		return 0xFF;
	}
}

uint8_t z180_read(struct z180_io *io, uint8_t addr)
{
	uint8_t r = z180_do_read(io, addr);
	if (io->trace)
		fprintf(stderr, "R %s -> %02X\n", regnames[addr & 0x3F], r);
	return r;
}

void z180_write(struct z180_io *io, uint8_t addr, uint8_t val)
{
	uint8_t delta;

	addr &= 0x3F;

	if (io->trace)
		fprintf(stderr, "W %s <- %02X\n", regnames[addr], val);

	switch(addr) {
	/* ASCI */
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
		z180_asci_write(io, addr, val);
		break;
	/* CSIO */
	case 0x0A:
		delta = io->cntr ^ val;
		io->cntr = val;
		if (io->cntr & delta & 0x10)	/* Set the TE bit */
			z180_csio_begin(io, io->trdr_w);
		else if (io->cntr & delta & 0x20)	/* Set the RE bit */
			z180_csio_begin(io, 0xFF);
		break;
	case 0x0B:
		io->cntr &= 0x7F;
		io->trdr_w = val;
		z180_next_interrupt(io);
		break;
	/* Timers */
	case 0x0C:
		io->prt[0].tmdr &= 0xFF00;
		io->prt[0].tmdr |= val;
		break;
	case 0x0D:
		io->prt[0].tmdr &= 0x00FF;
		io->prt[0].tmdr |= val << 8;
		break;
	case 0x0E:
		io->prt[0].rldr &= 0xFF00;
		io->prt[0].rldr |= val;
		break;
	case 0x0F:
		io->prt[0].rldr &= 0x00FF;
		io->prt[0].rldr |= val << 8;
		break;
	case 0x10:
		io->tcr = val;
		break;
	case 0x12:
		io->asci[0].ecr = val;
		break;
	case 0x13:
		io->asci[1].ecr = val;
		break;
	case 0x14:
		io->prt[1].tmdr &= 0xFF00;
		io->prt[1].tmdr |= val;
		break;
	case 0x15:
		io->prt[1].tmdr &= 0x00FF;
		io->prt[1].tmdr |= val << 8;
		break;
	case 0x16:
		io->prt[1].rldr &= 0xFF00;
		io->prt[1].rldr |= val;
		break;
	case 0x17:
		io->prt[1].rldr &= 0x00FF;
		io->prt[1].rldr |= val << 8;
		break;
	case 0x18:
		break;
	case 0x1A:
		io->asci[0].tc &= 0xFF00;
		io->asci[0].tc |= val;
		return;
	case 0x1B:
		io->asci[0].tc &= 0x00FF;
		io->asci[0].tc |= val << 8;
		return;
	case 0x1C:
		io->asci[1].tc &= 0xFF00;
		io->asci[1].tc |= val;
		return;
	case 0x1D:
		io->asci[1].tc &= 0x00FF;
		io->asci[1].tc |= val << 8;
		return;
	case 0x1E:
		io->cmr = val & 0x80;
		break;
	case 0x1F:
		io->ccr = val;
		break;
	/* DMA */
	case 0x20:
		io->sar0 &= 0xFFF00;
		io->sar0 |= val;
		break;
	case 0x21:
		io->sar0 &= 0xF00FF;
		io->sar0 |= val << 8;
		break;
	case 0x22:
		io->sar0 &= 0xFFFF;
		io->sar0 |= (val & 0x0F) << 16;
		break;
	case 0x23:
		io->dar0 &= 0xFFF00;
		io->dar0 |= val;
		break;
	case 0x24:
		io->dar0 &= 0xF00FF;
		io->dar0 |= val << 8;
		break;
	case 0x25:
		io->dar0 &= 0xFFFF;
		io->dar0 |= (val & 0x0F) << 16;
		break;
	case 0x26:
		io->bcr0 &= 0xFF00;
		io->bcr0 |= val;
		break;
	case 0x27:
		io->bcr0 &= 0x00FF;
		io->bcr0 |= val << 8;
		break;
	case 0x28:
		io->mar1 &= 0xFFF00;
		io->mar1 |= val;
		break;
	case 0x29:
		io->mar1 &= 0xF00FF;
		io->mar1 |= val << 8;
		break;
	case 0x2A:
		io->mar1 &= 0xFFFF;
		io->mar1 |= (val & 0x0F) << 16;
		break;
	case 0x2B:
		io->iar1 &= 0xFF00;
		io->iar1 |= val;
		break;
	case 0x2C:
		io->iar1 &= 0x00FF;
		io->iar1 |= val << 8;
		break;
	case 0x2E:
		io->bcr1 &= 0xFF00;
		io->bcr1 |= val;
		break;
	case 0x2F:
		io->bcr1 &= 0x00FF;
		io->bcr1 |= val << 8;
		break;
	case 0x30:
		/* bits 3,2 are the DIE bits and work normally */
		io->dstat &= ~0x0C;
		io->dstat |= val & 0x0C;
		/* To enable a channel you must write 1 to the DE bit and 0 to the
		   DWE bit */
		if ((val & 0xA0) == 0x80)
			io->dstat |= 0x81;
		if ((val & 0x50) == 0x40) {
			io->dstat |= 0x41;
			if (io->trace)
				fprintf(stderr, "DMA0 begin mode %02X from %X to %X for %X\n",
					io->dmode, io->sar0, io->dar0, io->bcr0);
		}
		/* To disable a channel you must write 0 to the DE bit and 0 to the
		   DWE bit */
		if ((val & 0xA0) == 0x00)
			io->dstat &= ~0x80;
		if ((val & 0x50) == 0x00)
			io->dstat &= ~0x40;
		break;
	case 0x31:
		io->dmode = val;
		break;
	case 0x32:
		io->dcntl = val;
		break;
	/* IL */
	case 0x33:
		io->il = val & 0xE0;
		break;
	/* ITC */
	case 0x34:
		/* ITC write is a bit more complicated */
		io->itc &= 0xF8;
		io->itc |= val & 0x07;
		if (!(val & 0x80))
			io->itc &= 0x7F;
		z180_next_interrupt(io);
		break;
	/* Refresh */
	case 0x36:
		io->rcr = val;	/* Not emulated - FIXME we don't adjust timing for WS */
		break;
	/* MMU */
	case 0x38:
		io->cbr = val;
		break;
	case 0x39:
		io->bbr = val;
		break;
	case 0x3A:
		/* Should we check for BA < CA ? */
		io->cbar = val;
		break;
	/* IO Control */
	case 0x3F:	/* ICR */
		io->icr = val;
		break;
	default:
		fprintf(stderr, "Unemulated Z180 I/O Write %s(0x%02X) <- 0x%02X\n",
			regnames[addr], addr, val);
		break;
	}
}

uint32_t z180_mmu_translate(struct z180_io *io, uint16_t addr)
{
	/* Common area 0: direct mapped */
	if ((addr >> 12) < (io->cbar & 0x0F))
		return addr;
	/* Common area 1 */
	if ((addr >> 12) >= (io->cbar >> 4))
		return addr + (io->cbr << 12);
	/* Bank area */
	return addr + (io->bbr << 12);
}

void z180_event(struct z180_io *io, unsigned int clocks)
{
	z180_asci_event(io, io->asci);
	z180_asci_event(io, io->asci + 1);

	/* Divide by 20 */
	io->clockmod += clocks;
	if (io->clockmod > 20) {
		io->frc += io->clockmod / 20;
		z180_prt_event(io, io->prt, io->clockmod / 20, 0);
		z180_prt_event(io, io->prt + 1, io->clockmod / 20, 1);
		io->clockmod %= 20;
	}
}

/*
 *	DMA engine
 *	We model transfers between memory and I/O space
 *
 *	We do yet not model
 *	DMA driven internal I/O (ASCI, CSIO etc)
 *	DMA toggles
 *	DREQ0/1 being high
 *	NMI halting DMA
 *
 *	Our cycle stealing isn't quite correct
 */

static unsigned int z180_dma_0(struct z180_io *io)
{
	unsigned int cost = 6;	/* Cost of each transfer */
	uint8_t byte;

	/* TODO: model wait states */

	/* We do a DMA then the CPU gets a go. Really we interleave with each
	   machine cycle but this will do for now */
	if (!(io->dmode & 2)) {
		io->dma_state0++;
		if (io->dma_state0 & 1)
			return 0;
	}

	/* Fetch a byte */
	/* TODO: when sar0/dar0 crosses a 64K boundary add 4 clocks */
	switch(io->dmode & 0x0C) {
	case 0x00:
		byte = z180_phys_read(io->cpu->ioParam, io->sar0++);
		io->sar0 &= 0xFFFFF;
		break;
	case 0x04:
		byte = z180_phys_read(io->cpu->ioParam, io->sar0--);
		io->sar0 &= 0xFFFFF;
		break;
	case 0x08:
		byte = z180_phys_read(io->cpu->ioParam, io->sar0);
		break;
	case 0x0C:
		byte = io->cpu->ioRead(io->cpu->ioParam, io->sar0);
		break;
	}
	/* Store the byte */
	switch(io->dmode & 0x30) {
	case 0x00:
		z180_phys_write(io->cpu->ioParam, io->dar0++, byte);
		io->dar0 &= 0xFFFFF;
		break;
	case 0x10:
		z180_phys_write(io->cpu->ioParam, io->dar0--, byte);
		io->dar0 &= 0xFFFFF;
		break;
	case 0x20:
		z180_phys_write(io->cpu->ioParam, io->dar0, byte);
		break;
	case 0x30:
		io->cpu->ioWrite(io->cpu->ioParam, io->dar0, byte);
	}

	if (--io->bcr0)
		return cost;
	/* DMA finished - stop engine and flag */
	io->dstat &= ~0x40;
	if (io->trace)
		fprintf(stderr, "DMA0 complete.\n");
	return cost;
}

static unsigned int z180_dma_1(struct z180_io *io)
{
	unsigned int cost = 6;	/* Cost of each transfer */
	uint8_t byte;

	/* TODO: model wait states */


	/* TODO: when mar1 crosses a 64K boundary add 4 clocks */

	/* Fetch a byte */
	switch(io->dcntl & 0x03) {
	case 0x00:
		byte = z180_phys_read(io->cpu->ioParam, io->mar1++);
		io->mar1 &= 0xFFFFF;
		break;
	case 0x01:
		byte = z180_phys_read(io->cpu->ioParam, io->mar1--);
		io->mar1 &= 0xFFFFF;
		break;
	case 0x02:
	case 0x03:
		byte = io->cpu->ioRead(io->cpu->ioParam, io->iar1);
		break;
	}
	/* Store the byte */
	switch(io->dmode & 0x03) {
	case 0x00:
	case 0x01:
		io->cpu->ioWrite(io->cpu->ioParam, io->iar1, byte);
		break;
	case 0x02:
		z180_phys_write(io->cpu->ioParam, io->mar1++, byte);
		io->mar1 &= 0xFFFFF;
		break;
	case 0x03:
		z180_phys_write(io->cpu->ioParam, io->mar1--, byte);
		io->mar1 &= 0xFFFFF;
	}
	if (--io->bcr1)
		return cost;
	io->dstat &= ~0x80;
	if (io->trace)
		fprintf(stderr, "DMA1 complete.\n");
	return cost;
}

/* Run the DMA engines */
unsigned int z180_dma(struct z180_io *io)
{
	/* Engines off */
	if (!(io->dstat & 1))
		return 0;

	/* Channel enables */
	if (io->dstat & 0x40)
		return z180_dma_0(io);
	if (io->dstat & 0x80)
		return z180_dma_1(io);
	return 0;
}

struct z180_io *z180_create(Z180Context *cpu)
{
	struct z180_io *io = malloc(sizeof(struct z180_io));
	if (io == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(io, 0, sizeof(struct z180_io));
	io->cbar = 0xF0;
	io->cbr = 0;
	io->bbr = 0;
	io->icr = 0;
	io->itc = 1;
	io->cntr = 7;
	io->asci[0].stat = 0x02;
	io->asci[1].stat = 0x02;
	io->dstat = 0x30;
	io->dcntl = 0xF0;	/* Manual disagrees with itself here */
	io->cpu = cpu;
	io->clock = 18432000;
	return io;
}

void z180_free(struct z180_io *io)
{
	free(io);
}

void z180_trace(struct z180_io *io, int trace)
{
	io->trace = trace;
}

void z180_ser_attach(struct z180_io *io, int port, struct serial_device *dev)
{
	io->asci[port].dev = dev;
}

void z180_set_clock(struct z180_io *io, unsigned hz)
{
	io->clock = hz;
}
