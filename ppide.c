#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "system.h"
#include "ppide.h"

/*
 *	Emulate PPIDE. It's not a particularly good emulation of the actual
 *	port behaviour if misprogrammed but should be accurate for correct
 *	use of the device.
 */

void ppide_write(struct ppide *ppide, uint8_t addr, uint8_t val)
{
	/* Compute all the deltas */
	uint8_t changed = ppide->pioreg[addr] ^ val;
	uint8_t dhigh = val & changed;
	uint8_t dlow = ~val & changed;
	uint16_t d;
	unsigned bit;

	switch(addr) {
		case 0:	/* Port A data */
		case 1:	/* Port B data */
			ppide->pioreg[addr] = val;
			if (ppide->trace)
				fprintf(stderr, "Data now %04X\n", (((uint16_t)ppide->pioreg[1]) << 8) | ppide->pioreg[0]);
			break;
		case 2:	/* Port C - address/control lines */
			ppide->pioreg[addr] = val;
			if (ppide->ide == NULL)
				return;
			if (val & 0x80) {
				if (ppide->trace)
					fprintf(stderr, "ide reset state (%02X).\n", val);
				ide_reset_begin(ppide->ide);
				return;
			}
			if ((ppide->trace) && (dlow & 0x80))
				fprintf(stderr, "ide exits reset.\n");

			/* This register is effectively the bus to the IDE device
			   bits 0-2 are A0-A2, bit 3 is CS0 bit 4 is CS1 bit 5 is W
			   bit 6 is R bit 7 is reset */
			d = val & 0x07;
			/* Altstatus and friends */
			if (val & 0x10)
				d += 2;
			if (dlow & 0x20) {
				if (ppide->trace)
					fprintf(stderr, "write edge: %02X = %04X\n", d,
						((uint16_t)ppide->pioreg[1] << 8) | ppide->pioreg[0]);
				ide_write16(ppide->ide, d, ((uint16_t)ppide->pioreg[1] << 8) | ppide->pioreg[0]);
			} else if (dhigh & 0x40) {
				/* Prime the data ports on the rising edge */
				if (ppide->trace)
					fprintf(stderr, "read edge: %02X = ", d);
				d = ide_read16(ppide->ide, d);
				if (ppide->trace)
					fprintf(stderr, "%04X\n", d);
				ppide->pioreg[0] = d;
				ppide->pioreg[1] = d >> 8;
			}
			break;
		case 3: /* Control register */
			/* Check for control commands being used to flip the clock
			   as that is how Will's latest code drives the clock */
			if (val & 0x80) {
				/* We could check the direction bits but we don't */
				ppide->pioreg[addr] = val;
			}
			/* We are doing Port C bitbanging */
			bit = 1 << ((val >> 1) & 0x07);
			if (val & 1)
				val = ppide->pioreg[2] | bit;
			else
				val = ppide->pioreg[2] & ~bit;
			/* Do the equivalent write */
			ppide_write(ppide, 2, val);
			break;
	}
}

uint8_t ppide_read(struct ppide *ppide, uint8_t addr)
{
	if (ppide->trace)
		fprintf(stderr, "ide read %d:%02X\n", addr, ppide->pioreg[addr]);
	return ppide->pioreg[addr];
}

void ppide_reset(struct ppide *ppide)
{
	ide_reset_begin(ppide->ide);
}

struct ppide *ppide_create(const char *name)
{
	struct ppide *ppide = malloc(sizeof(struct ppide));
	if (ppide == NULL || (ppide->ide = ide_allocate(name)) == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	ppide->trace = 0;
	ppide_reset(ppide);
	return ppide;
}

void ppide_free(struct ppide *ppide)
{
	ide_free(ppide->ide);
	free(ppide);
}

void ppide_trace(struct ppide *ppide, int onoff)
{
	ppide->trace = onoff;
}

int ppide_attach(struct ppide *ppide, int drive, int fd)
{
	return ide_attach(ppide->ide, drive, fd);
}
