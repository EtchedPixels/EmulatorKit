#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "serialdevice.h"
#include "system.h"
#include "acia.h"

struct acia {
	uint8_t status;
	uint8_t config;
	uint8_t rxchar;
	uint8_t inint;
	uint8_t inreset;
	uint8_t trace;
	struct serial_device *dev;
};


static void acia_irq_compute(struct acia *acia)
{
	/* Recalculate the interrupt bit */
	acia->status &= 0x7F;
	if ((acia->status & 0x01) && (acia->config & 0x80))
		acia->status |= 0x80;
	if ((acia->status & 0x02) && (acia->config & 0x60) == 0x20)
		acia->status |= 0x80;
	/* Now see what should happen */
	if (!(acia->config & 0x80) || !(acia->status & 0x80)) {
		if (acia->inint && acia->trace)
			fprintf(stderr, "ACIA interrupt end.\n");
		acia->inint = 0;
		acia->status &= 0x7F;
		return;
	}
	if (acia->inint == 0 && (acia->trace))
		fprintf(stderr, "ACIA interrupt.\n");
	acia->inint = 1;
	recalc_interrupts();
}

static void acia_receive(struct acia *acia)
{
	if (acia->inreset)
		return;
	/* Already a character waiting so set OVRN */
	if (acia->status & 1)
		acia->status |= 0x20;
	acia->rxchar = acia->dev->get(acia->dev);
	if (acia->trace)
		fprintf(stderr, "ACIA rx.\n");
	acia->status |= 0x01;	/* IRQ, and rx data full */
}

static void acia_transmit(struct acia *acia)
{
	if (!(acia->status & 2)) {
		if (acia->trace)
			fprintf(stderr, "ACIA tx is clear.\n");
		acia->status |= 0x02;	/* IRQ, and tx data empty */
	}
}

void acia_timer(struct acia *acia)
{
	int s = acia->dev->ready(acia->dev);
	if (s & 1)
		acia_receive(acia);
	if (s & 2)
		acia_transmit(acia);
	if (s)
		acia_irq_compute(acia);
}

uint8_t acia_read(struct acia *acia, uint16_t addr)
{
	if (acia->trace)
		fprintf(stderr, "acia_read %d ", addr);
	switch (addr) {
	case 0:
		if (acia->inreset) {
			if (acia->trace)
				fprintf(stderr, "= 0 (reset).\n");
			return 0;
		}
		/* Reading the ACIA status has no effect on the bits */
		if (acia->trace)
			fprintf(stderr, "acia->status %d\n", acia->status);
		return acia->status;
	case 1:
		/* Reading the ACIA character clears the receive ready
		   and also updates the error bits to match the new byte */
		/* Clear receive ready and rx overrun */
		acia->status &= ~0x21;
		acia_irq_compute(acia);
		if (acia->trace)
			fprintf(stderr, "acia_char %d\n", acia->rxchar);
		return acia->rxchar;
	default:
		fprintf(stderr, "acia: bad addr.\n");
		exit(1);
	}
}

void acia_write(struct acia *acia, uint16_t addr, uint8_t val)
{
	if (acia->trace)
		fprintf(stderr, "acia_write %d %d\n", addr, val);
	switch (addr) {
	case 0:
		/* bit 7 enables interrupts, bits 5-6 are tx control
		   bits 2-4 select the word size and 0-1 counter divider
		   except 11 in them means reset */
		acia->config = val;
		if ((acia->config & 3) == 3)
			acia->inreset = 1;
		else if (acia->inreset) {
			acia->inreset = 0;
			acia->status = 2;
		}
		if (acia->trace)
			fprintf(stderr, "ACIA config %02X\n", val);
		acia_irq_compute(acia);
		return;
	case 1:
		acia->dev->put(acia->dev, val);
		/* Clear TDRE - we now have a byte */
		acia->status &= ~0x02;
		acia_irq_compute(acia);
		break;
	}
}

void acia_attach(struct acia *acia, struct serial_device *dev)
{
	acia->dev = dev;
}

void acia_reset(struct acia *acia)
{
	memset(acia, 0, sizeof(struct acia));
	acia->status = 2;
	acia_irq_compute(acia);
}

uint8_t acia_irq_pending(struct acia *acia)
{
	return acia->inint;
}

struct acia *acia_create(void)
{
	struct acia *acia = malloc(sizeof(struct acia));
	if (acia == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	acia_reset(acia);
	return acia;
}

void acia_free(struct acia *acia)
{
	free(acia);
}

void acia_trace(struct acia *acia, int onoff)
{
	acia->trace = onoff;
}
