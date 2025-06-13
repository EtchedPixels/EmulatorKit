#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "serialdevice.h"
#include "system.h"
#include "6551.h"

struct m6551 {
	uint8_t tdr;
	uint8_t rdr;
	uint8_t status;
	uint8_t cmd;
	uint8_t ctrl;
	unsigned trace;
	unsigned inint;
	struct serial_device *dev;
};


static void m6551_irq_compute(struct m6551 *m6551)
{
	unsigned oi = m6551->inint;
	m6551->status &= ~0x80;

	if (m6551->status & 0x08)
		m6551->status |= 0x80;
	if ((m6551->status & 0x10) && (m6551->cmd & 0x0C) == 0x04)
		m6551->status |= 0x80;

	//If the IRQB is asserted, and the transmit data regiter is empty, then
	//set initial int for next go-round.
	if ((m6551->status & 0x80) && (m6551->cmd & 0x10))
		m6551->inint = 1;
	else
		m6551->inint = 0;
	if (m6551->trace && m6551->inint != oi)
		fprintf(stderr, "m6551: irq state now %d\n", m6551->inint);
	recalc_interrupts();
}

static void m6551_receive(struct m6551 *m6551)
{
	/* Already a character waiting so set OVRN */
	if (m6551->status & 0x08)
		m6551->status |= 0x04;
	m6551->rdr = m6551->dev->get(m6551->dev);
	if (m6551->trace)
		fprintf(stderr, "m6551 rx %02X.\n", m6551->rdr);
	m6551->status |= 0x08;	/* RX data full */
	/* Loopback */
	if (m6551->cmd & 0x10)
		m6551->dev->put(m6551->dev, m6551->rdr);
}

static void m6551_transmit(struct m6551 *m6551)
{
	if (!(m6551->status & 0x10)) {
		if (m6551->trace)
			fprintf(stderr, "m6551 tx is clear.\n");
		m6551->status |= 0x10;	/* TX data empty */
	}
}

void m6551_timer(struct m6551 *m6551)
{
	int s = m6551->dev->ready(m6551->dev);
	if (s & 1)
		m6551_receive(m6551);
	if (s & 2)
		m6551_transmit(m6551);
	if (s)
		m6551_irq_compute(m6551);
}

uint8_t m6551_read(struct m6551 *m6551, uint16_t addr)
{
	if (m6551->trace)
		fprintf(stderr, "m6551_read %d\n", addr);
	switch (addr & 3) {
	case 0:
		m6551->status &= ~0x0F;
		m6551_irq_compute(m6551);
		if (m6551->trace)
			fprintf(stderr, "m6551 rx %d\n", m6551->rdr);
		return m6551->rdr;
	case 1:
		return m6551->status;
	case 2:
		return m6551->cmd;
	case 3:
		return m6551->ctrl;
	}
	/* Unreachable */
	return 0xFF;
}

void m6551_write(struct m6551 *m6551, uint16_t addr, uint8_t val)
{
	if (m6551->trace)
		fprintf(stderr, "m6551_write %d %d\n", addr, val);
	switch (addr & 3) {
	case 0:
		m6551->tdr = val;
		/* And transmit */
		m6551->dev->put(m6551->dev, val);
		m6551_irq_compute(m6551);
		break;
	case 1:
		m6551_reset(m6551);
		break;
	case 2:
		m6551->cmd = val;
		if (m6551->trace)
			fprintf(stderr, "6551: command %02X\n", val);
		m6551_irq_compute(m6551);
		break;
	case 3:
		m6551->ctrl = val;
		if (m6551->trace)
			fprintf(stderr, "6551: control %02X\n", val);
		m6551_irq_compute(m6551);
		break;
	}
}

void m6551_attach(struct m6551 *m6551, struct serial_device *dev)
{
	m6551->dev = dev;
}

void m6551_reset(struct m6551 *m6551)
{
	m6551->cmd |= 0x1f;
	m6551->status |= 4;
	m6551_irq_compute(m6551);
}

uint8_t m6551_irq_pending(struct m6551 *m6551)
{
	return m6551->inint;
}

struct m6551 *m6551_create(void)
{
	struct m6551 *m6551 = malloc(sizeof(struct m6551));
	if (m6551 == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(m6551, 0, sizeof(struct m6551));
	return m6551;
}

void m6551_free(struct m6551 *m6551)
{
	free(m6551);
}

void m6551_trace(struct m6551 *m6551, int onoff)
{
	m6551->trace = onoff;
}
