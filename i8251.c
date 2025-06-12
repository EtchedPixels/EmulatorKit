#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "system.h"
#include "serialdevice.h"
#include "i8251.h"

struct i8251 {
	uint8_t cmd;
	uint8_t config;
	uint8_t status;

	unsigned int reset;
	unsigned int pending;
	unsigned int trace;
	unsigned int irq;
	struct serial_device *dev;
};


static void i8251_irq_compute(struct i8251 *i8251)
{
	/* TODO */
}

void i8251_timer(struct i8251 *i8251)
{
	unsigned  s = i8251->dev->ready(i8251->dev);
	i8251->status = 0;
	if (s & 2)
		i8251->status |= 1;
	if (s & 1)
		i8251->status |= 2;
	i8251_irq_compute(i8251);
}

uint8_t i8251_read(struct i8251 *i8251, uint16_t addr)
{
	if (i8251->trace)
		fprintf(stderr, "i8251_read %d ", addr);

	switch (addr) {
	case 0:
		i8251->status &= ~0x02;
		return i8251->dev->get(i8251->dev);
	case 1:
		return i8251->status;
	default:
		fprintf(stderr, "i8251: bad addr.\n");
		exit(1);
	}
}

void i8251_write(struct i8251 *i8251, uint16_t addr, uint8_t val)
{
	if (i8251->trace)
		fprintf(stderr, "i8251_write %d %d\n", addr, val);
	switch (addr) {
	case 0:
		if (i8251->cmd & 1)	/* TX enabled */
			i8251->dev->put(i8251->dev, val);
		i8251->status &= ~0x01;
		return;
	case 1:
		/* We don't emulate synchronous set ups */
		if (i8251->reset == 1) {
			i8251->reset = 0;
			i8251->config = val;
			return;
		}
		i8251->cmd = val;
		if (val & 0x20)
			i8251->reset = 1;
	}
}

void i8251_attach(struct i8251 *i8251, struct serial_device *dev)
{
	i8251->dev = dev;
}

void i8251_reset(struct i8251 *i8251)
{
	memset(i8251, 0, sizeof(struct i8251));
	i8251->reset = 1;
	i8251_irq_compute(i8251);
}

unsigned i8251_irq_pending(struct i8251 *i8251)
{
	return i8251->irq;
}

struct i8251 *i8251_create(void)
{
	struct i8251 *i8251 = malloc(sizeof(struct i8251));
	if (i8251 == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	i8251_reset(i8251);
	return i8251;
}

void i8251_free(struct i8251 *i8251)
{
	free(i8251);
}

void i8251_trace(struct i8251 *i8251, int onoff)
{
	i8251->trace = onoff;
}
