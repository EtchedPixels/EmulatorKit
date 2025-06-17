#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sasi.h"

struct ncr5380 {
	struct sasi_bus *bus;
	uint8_t last_data;
	uint8_t icr;
	uint8_t bsr;
	uint8_t mode;
	uint8_t tcr;
	uint8_t ncr;
	uint8_t selen;
	unsigned busbits;
	unsigned trace;
	unsigned arb;
	unsigned irq;
	unsigned dma_rx;
	unsigned dma_tx;
};

static void ncr_set_icr(struct ncr5380 *ncr, uint8_t val)
{
	uint8_t delta = ncr->icr ^ val;

	ncr->icr = val;
	/* We don't emulate bit 0 or 5 */
	if (val & 0x80)
		ncr->busbits |= SASI_RST;
	else
		ncr->busbits &= SASI_RST;
	if (val & 0x02)
		ncr->busbits |= SCSI_ATN;
	else
		ncr->busbits &= SCSI_ATN;
	if (val & 0x04)
		ncr->busbits |= SASI_SEL;
	else
		ncr->busbits &= SASI_SEL;
	if (val & 0x08)
		ncr->busbits |= SASI_BSY;
	else
		ncr->busbits &= ~SASI_BSY;
	if (val & 0x10)
		ncr->busbits |= SASI_ACK;
	else
		ncr->busbits &= ~SASI_ACK;

	/* TODO: should merge the status bits for the totem pole */
	sasi_bus_control(ncr->bus, ncr->busbits);
//	if (delta & ~val & 0x04)
//		sasi_write_data(ncr->bus, ncr->last_data);
	/* Ack strobed - so data byte transferred on bus */
	if ((delta & ~val & 0x10) && !(sasi_bus_state(ncr->bus) & SASI_IO))
		sasi_write_data(ncr->bus, ncr->last_data);
}

static void ncr_dma_end(struct ncr5380 *ncr)
{
	if (ncr->dma_rx || ncr->dma_tx) {
		if (ncr->trace)
			fprintf(stderr, "ncr5380: dma end\n");
		ncr->dma_rx = 0;
		ncr->dma_tx = 0;
		ncr->bsr |= 0x80;
		ncr->bsr &= ~0x40;
		ncr->irq = 1;
	}
}

void ncr5380_activity(struct ncr5380 *ncr)
{
	/* Do the logic in the NCR5380 */
	uint8_t r = sasi_bus_state(ncr->bus);

	/* Busy monitoring */
	if (ncr->mode & 0x04) {
		if (!(r & SASI_BSY)) {
			ncr_set_icr(ncr, ncr->icr & 0xE0);
			ncr->irq = 1;
			ncr->bsr |= 0x04;
			if (ncr->trace)
				fprintf(stderr, "ncr5380: lost busy.\n");
		}
	}

	/* Arbitration : figure out how this clears better */
	if (ncr->mode & 0x01) {
		if (ncr->trace)
			fprintf(stderr, "arb bus was %02X\n", r);
		if (r == 0) {
			ncr->busbits |= SASI_BSY;
			ncr->icr |= 0x40;	/* AIP */
			if (ncr->trace)
				fprintf(stderr, "ncr5380: arbitrated.\n");
;			ncr->busbits |= SCSI_ATN;
			sasi_bus_control(ncr->bus, ncr->busbits);
		}
	}
	/* Selection : not emulated */
	/* Bus phase mismatch is checked in the read/write */
}

static uint8_t bus_phase(struct ncr5380 *ncr)
{
	unsigned bus = sasi_bus_state(ncr->bus);
	uint8_t r = 0;
	if (bus & SASI_MSG)
		r |= 4;
	if (bus & SASI_CD)
		r |= 2;
	if (bus & SASI_IO)
		r |= 1;
	return r;
}

static void ncr_phase_check(struct ncr5380 *ncr)
{
	unsigned b = bus_phase(ncr);
//	static unsigned lb;
	if (b != (ncr->tcr & 7)) {
		if (ncr->trace)
			fprintf(stderr, "ncr5380: bus phase mismatch %d %d\n",
				b, ncr->tcr & 7);
		if ((ncr->mode & 2) /* && (b & SASI_REQ) &&  !(lb & SASI_REQ) */)
			ncr->irq = 1;
		ncr->bsr &= 0xF7;
		ncr_dma_end(ncr);
	} else
		ncr->bsr |= 8;
}

static uint8_t do_ncr5380_read(struct ncr5380 *ncr, unsigned reg)
{
	uint8_t r;
	uint8_t bus;

	switch (reg & 0x0F) {
	case 0:
		/* This is a bit clunky - we should probably model the ACK
		    REQ cycle on the bus in full but it will do for now */
		if (sasi_bus_state(ncr->bus) & SASI_IO)
			return sasi_read_data(ncr->bus);
		/* Return the bus data state live */
		return ncr->last_data;
	case 1:
		/* initiator command register */
		return ncr->icr;
	case 2:
		/* mode register */
		return ncr->mode;
	case 3:
		/* Target mode */
		return ncr->tcr;
	case 4:
		/* Returns the current bus signals */
		r = 0;
		bus = sasi_bus_state(ncr->bus);
		bus |= ncr->busbits & (SASI_BSY|SASI_REQ);
		if (bus & SASI_SEL)
			r |= (1 << 1);
		if (bus & SASI_IO)
			r |= (1 << 2);
		if (bus & SASI_CD)
			r |= (1 << 3);
		if (bus & SASI_MSG)
			r |= (1 << 4);
		if (bus & SASI_REQ)
			r |= (1 << 5);
		if (bus & SASI_BSY)
			r |= (1 << 6);
		if (ncr->icr & 0x80)
			r |= 0x80;
		return r;
	case 5:
		/* bus and status register. Status bits */
		ncr_phase_check(ncr);
		r = ncr->bsr & 0xEC;
		if (ncr->busbits & SCSI_ATN)
			r |= 0x02;
		if (ncr->irq)
			r |= 1 << 4;
		return r;
	case 6:
		/* input data register, reads latched data from ack/dma
		   needs DMA mode bit (port 2 bit 1) set to do stuff. Can
		   be a DMA target */
		/* TODO if mode & 2 must be set to latch new data */
		return ncr->last_data;
	case 7:
		/* Reset parity error int req and busy - what return ?? */
		ncr->irq = 0;
		/* Clear parity, int req and busy error */
		ncr->bsr &= 0xCD;
		return 0x00;
	case 8:
		/* PDMA port */
		if (ncr->dma_rx) {
			r =  sasi_read_data(ncr->bus);
			/* Check BSY */
			ncr5380_activity(ncr);
			ncr_phase_check(ncr);
			return r;
		}
		/* ?? what happens */
		break;
	}
	return 0xFF;
}

uint8_t ncr5380_read(struct ncr5380 *ncr, unsigned reg)
{
	uint8_t r = do_ncr5380_read(ncr, reg);
	if (ncr->trace)
		fprintf(stderr, "ncr5380 R %02X %02X\n", reg, r);
	return r;
}

uint8_t ncr5380_write(struct ncr5380 *ncr, unsigned reg, uint8_t val)
{
	if (ncr->trace)
		fprintf(stderr, "ncr5380 W %02X %02X\n", reg, val);
	switch (reg & 0x0F) {
	case 0:
		/* Set the SCSI bus data lines */
		sasi_set_data(ncr->bus, val);
		ncr->last_data = val;
		break;
	case 1:
		ncr_set_icr(ncr, val);
		break;
	case 2:
		/* Mode register */
		ncr->mode = val;
		if ((ncr->mode & 2) == 0)
			ncr_dma_end(ncr);
		/* If the user wants arbitration */
		if (val & 1)
//			ncr->mode |= 0x40;
//		else
			ncr->mode &= 0xBF;
		break;
	case 3:
		/* Target command register */
		/* Target mode only - does nothing in host */
		ncr->tcr = val;
		break;
	case 4:
		/* Select enable - monitors a single id during selection
		   until we see id, \BSY false \SEL true then interrupts */
		ncr->selen = val;
//		if (val == 0)
//			ncr_clear_select_int();
		break;
	case 5:
		/* Start a DMA send */
		if (ncr->mode & 2) {
			ncr->dma_tx = 1;
			ncr->bsr |= 0x40;
		}
		break;
	case 6:
		/* Start a DMA target receive */
		break;
	case 7:
		/* Start a DMA initiator receive */
		if (ncr->mode & 2) {
			ncr->dma_rx = 1;
			ncr->bsr |= 0x40;
		}
		break;
	case 8:
		/* PDMA port */
		if (ncr->dma_tx) {
			ncr_phase_check(ncr);
			sasi_write_data(ncr->bus, val);
			ncr5380_activity(ncr);
		}
		break;
	}
	return 0xFF;
}

struct ncr5380 *ncr5380_create(struct sasi_bus *sasi)
{
	struct ncr5380 *ncr = malloc(sizeof(*ncr));
	if (ncr == NULL) {
		fprintf(stderr, "ncr5380_create: out of memory.\n");
		exit(1);
	}
	memset(ncr, 0, sizeof(*ncr));
	ncr->bus = sasi;
	return ncr;
}

void ncr5380_free(struct ncr5380 *ncr)
{
	free(ncr);
}

void ncr5380_trace(struct ncr5380 *ncr, unsigned trace)
{
	ncr->trace = !!trace;
}
