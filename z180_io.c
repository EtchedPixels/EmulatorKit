/*
 *	I/O model for the Z180
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "z180_io.h"

struct z180_asci {
    uint8_t tdr;
    uint8_t rdr;
    uint8_t stat;
    uint8_t cntla;
    uint8_t cntlb;
    bool input;
    bool irq;
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
    /* Serial ports */
    struct z180_asci asci[2];

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
    "RES 12H",
    "RES 13H",
    /* Timer = 0x14 */
    "TMDR1L",
    "TMDR1H",
    "RLDR1L",
    "RLDR1H",
    /* Free running counter = 0x18 */
    "FRC",
    /* Unused 0x19->0x1F */
    "RES 19H",
    "RES 1AH",
    "RES 1BH",
    "RES 1CH",
    "RES 1DH",
    "RES 1EH",
    "RES 1FH",
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

    
    
static void z180_asci_recalc(struct z180_io *io, struct z180_asci *asci)
{
    asci->irq = 0;
    /* TODO check for interrupt cases */
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
        z180_asci_recalc(io, asci);
        break;
    case 0x04:
        asci->stat &= 0xF6;
        asci->stat |= val & 0x09;
        break;
    case 0x06:
        /* TDRE was high and tx was enabled */
        if ((asci->cntla & 0x20) && (asci->stat & 0x02)) {
            write(1, &val, 1);
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
    unsigned int r = check_chario();
    if (r & 2)
        asci->stat |= 0x02;
    if (asci->input && (r & 1)) {
        asci->stat |= 0x80;
        asci->rdr = next_char();
    }
}

static void z180_csio_begin(struct z180_io *io)
{
    /* We should time this but for now just do a quick instant hack TODO */
    io->trdr_r = z180_csio_write(io, io->trdr_w);
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
        return z180_asci_read(io, addr);
    /* CSIO */
    case 0x0A:
        return io->cntr;
    case 0x0B:
        return io->trdr_r;
    /* ITC: TODO - UFO bits from CPU core emulation */
    case 0x34:
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
        if (io->cntr & delta & 0x20)	/* Set the RE bit */
            z180_csio_begin(io);
        break;
    case 0x0B:
        io->trdr_w = val;
        break;
    /* ITC */
    case 0x34:
        /* ITC write is a bit more complicated */
        io->itc &= 0xF8;
        io->itc |= val & 0x07;
        if (!(val & 0x80))
            io->itc &= 0x7F;
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

void z180_event(struct z180_io *io)
{
    z180_asci_event(io, io->asci);
    z180_asci_event(io, io->asci+1);
}

struct z180_io *z180_create(void)
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

void z180_set_input(struct z180_io *io, int port, int onoff)
{
    io->asci[port].input = onoff;
}
