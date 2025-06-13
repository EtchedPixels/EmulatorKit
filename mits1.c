#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "serialdevice.h"
#include "system.h"
#include "mits1.h"

/* A very simple TR1402 or similar UART. It can be jumpered for some kinds
   of serial interrupt but this isn't currently used anywhere

   0x80		low if ready for a byte (an tx int)
   0x40		free
   0x20		high if input available
   0x10		overrun
   0x08		frame error
   0x04		parity
   0x02		xmit empty (1)
   0x01		input ready (bit 0 low)

   Later boards used an ACIA at 0x10 instead

 */

struct mits1_uart {
	uint8_t status;
	uint8_t config;
	uint8_t rxchar;
	uint8_t inint;
	uint8_t trace;
	struct serial_device *dev;
};


static void mits1_irq_compute(struct mits1_uart *mu)
{
	mu->inint = 0;
	if (!(mu->config & 1) && (mu->status & 0x02))
		mu->inint |= 1;
	if (!(mu->config & 2) && (mu->status & 0x20))
		mu->inint |= 2;
}

static void mits1_receive(struct mits1_uart *mu)
{
	/* Already a character waiting so set OVRN */
	if (!(mu->status & 0x01))
		mu->status |= 0x20;
	mu->rxchar = mu->dev->get(mu->dev);
	if (mu->trace)
		fprintf(stderr, "ACIA rx.\n");
	mu->status &= ~0x01;	/* Input ready */
	mu->status |= 0x20;	/* Data ready */
}

/* TX Done */
static void mits1_transmit(struct mits1_uart *mu)
{
	if (mu->status & 0x80) {
		if (mu->trace)
			fprintf(stderr, "MITS tx is clear.\n");
		mu->status &= ~0x80;
		mu->status |= 0x02;
	}
}

void mits1_timer(struct mits1_uart *mu)
{
	int s = mu->dev->ready(mu->dev);
	if (s & 1)
		mits1_receive(mu);
	if (s & 2)
		mits1_transmit(mu);
	if (s)
		mits1_irq_compute(mu);
}

uint8_t mits1_read(struct mits1_uart *mu, uint16_t addr)
{
	if (mu->trace)
		fprintf(stderr, "mits1_read %d ", addr);
	switch (addr) {
	case 0:
		if (mu->trace)
			fprintf(stderr, "mu->status %d\n", mu->status);
		return mu->status;
	case 1:
		/* Read a data byte */
		mu->status |= 1;
		mu->status &= ~0x3C;
		mits1_irq_compute(mu);
		if (mu->trace)
			fprintf(stderr, "mits1_char %d\n", mu->rxchar);
		return mu->rxchar;
	default:
		fprintf(stderr, "mits1_uart: bad addr.\n");
		exit(1);
	}
}

void mits1_write(struct mits1_uart *mu, uint16_t addr, uint8_t val)
{
	if (mu->trace)
		fprintf(stderr, "mits1_write %d %d\n", addr, val);
	switch (addr) {
	case 0:
		mu->config = val;
		if (mu->trace)
			fprintf(stderr, "mits1 config %02X\n", val);
		mits1_irq_compute(mu);
		return;
	case 1:
		mu->dev->put(mu->dev, val);
		mu->status |= 0x80;
		mu->status &= ~0x02;
		mits1_irq_compute(mu);
		break;
	}
}

void mits1_attach(struct mits1_uart *mu, struct serial_device *dev)
{
	mu->dev = dev;
}

void mits1_reset(struct mits1_uart *mu)
{
	memset(mu, 0, sizeof(struct mits1_uart));
	mu->status = 0x82;
	mits1_irq_compute(mu);
}

uint8_t mits1_irq_pending(struct mits1_uart *mu)
{
	return mu->inint;
}

struct mits1_uart *mits1_create(void)
{
	struct mits1_uart *mu = malloc(sizeof(struct mits1_uart));
	if (mu == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	mits1_reset(mu);
	return mu;
}

void mits1_free(struct mits1_uart *mu)
{
	free(mu);
}

void mits1_trace(struct mits1_uart *mu, int onoff)
{
	mu->trace = onoff;
}
