#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "duart.h"

/* 68681 DUART */

struct duart_port {
	uint8_t mr1;
	uint8_t mr2;
	uint8_t sr;
	uint8_t csr;
	uint8_t rx;
	uint8_t mrp;
	uint8_t txdis;
	uint8_t rxdis;
};

struct duart {
	struct duart_port port[2];
	uint8_t ipcr;
	uint8_t isr;
	int32_t ct;		/* We overflow this temporarily */
	uint16_t ctr;
	uint8_t ctstop;
	uint8_t imr;
	uint8_t ivr;
	uint8_t opcr;
	uint8_t opr;
	uint8_t ip;
	uint8_t acr;
	uint8_t irq;
	int input;		/* Which port if any is console */
	int trace;		/* Debug trace */
};

static void duart_irq_calc(struct duart *d)
{
	d->irq = d->isr & d->imr;
	if (d->trace) {
		if (d->irq)
			fprintf(stderr, "DUART IRQ asserted.\n");
		else
			fprintf(stderr, "DUART IRQ released.\n");
	}
	recalc_interrupts();
}

static void duart_irq_raise(struct duart *d, uint8_t m)
{
	if (!(d->isr & m)) {
		d->isr |= m;
		if (d->trace)
			fprintf(stderr, "DUART IRQ raised %02X\n", m);
		duart_irq_calc(d);
	}
}

static void duart_irq_lower(struct duart *d, uint8_t m)
{
	if (d->isr & m) {
		d->isr &= ~m;
		if (d->trace)
			fprintf(stderr, "DUART IRQ lowered %02X\n", m);
		duart_irq_calc(d);
	}
}

static uint8_t duart_input(struct duart *d, int port)
{
	if (d->port[port].sr & 0x01) {
		d->port[port].sr ^= 0x01;
		duart_irq_lower(d, 2 << (4 * port));
	}
	return d->port[port].rx;
}

static void duart_output(struct duart *d, int port, int value)
{
	duart_irq_lower(d, 1 << (4 * port));
	if (d->port[port].txdis)
		return;
	if (d->port[port].sr & 0x04) {
		if (d->input - 1 == port) {
			uint8_t v = value & 0xFF;
			write(1, &v, 1);
		}
		d->port[port].sr &= 0xF3;
		duart_irq_calc(d);
	}
}

static void duart_command(struct duart *d, int port, int value)
{
	switch ((value & 0xE0) >> 4) {
	case 0:
		break;
	case 1:
		d->port[port].mrp = 0;
		break;
	case 2:
		/* Reset RX A */
		d->port[port].rxdis = 1;
		break;
	case 3:
		break;
	case 4:
		d->port[port].sr &= 0x0F;
		break;
	case 5:
		duart_irq_lower(d, 4 << (4 * port));
		duart_irq_calc(d);
		break;
	case 6:
		break;		/* Literally start break */
	case 7:
		break;		/* Stop break */
	}
	if (value & 1)
		d->port[port].rxdis = 0;
	if (value & 2)
		d->port[port].rxdis = 1;
	if (value & 4)
		d->port[port].txdis = 0;
	if (value & 8)
		d->port[port].txdis = 1;
}

/* Simulate 1/100th second of action */
static void duart_count(struct duart *d, int n)
{
	/* We are clocked at ??? so divide as needed */
	uint16_t clock = 184;	/* 1843200 so not entirely accurate
				   FIXME: we could track partial clocks */
	if (n == 16)
		clock /= 16;	/* Again needs accuracy sorting */

	/* Counter mode can be stopped */
	if (!(d->acr & 0x40))
		if (d->ctstop)
			return;

	d->ct -= clock;
	if (d->ct < 0) {
		/* Our couunt overran so we compute the remainder */
		if (d->ctr) {
			d->ct %= d->ctr;	/* Negative clocks left */
			d->ct += d->ctr;	/* Plus next cycle */
		} else
			d->ct = 0;
		/* And raise the event */
		duart_irq_raise(d, 0x08);
	}
}

void duart_tick(struct duart *d)
{
	uint8_t r = check_chario();
	if (r & 1) {
		if (d->input == 1 && d->port[0].rxdis == 0) {
			d->port[0].rx = next_char();
			d->port[0].sr |= 0x01;
			duart_irq_raise(d, 0x02);
		} else if (d->input == 2 && d->port[1].rxdis == 0) {
			d->port[1].rx = next_char();
			d->port[1].sr |= 0x01;
			duart_irq_raise(d, 0x20);
		}
	}
	if (r & 2) {
		if (!d->port[0].txdis && !(d->port[0].sr & 0x04))
			duart_irq_raise(d, 0x01);
		d->port[0].sr |= 0x0C;
		if (!d->port[1].txdis && !(d->port[1].sr & 0x04))
			duart_irq_raise(d, 0x10);
		d->port[1].sr |= 0x0C;
	}
	switch ((d->acr & 0x70) >> 4) {
		/* Clock and timer modes */
	case 0:		/* Counting IP2 */
		break;
	case 1:		/* Counting TxCA */
		break;
	case 2:		/* Counting TxCB */
		break;
	case 3:		/* Counting EXT/x1 clock  / 16 */
		duart_count(d, 16);
		break;
	case 4:		/* Timer on IP2 */
		break;
	case 5:		/* Timer on IP2/16 */
		break;
	case 6:		/* Timer on X1/CLK */
		duart_count(d, 1);
		break;
	case 7:		/* Timer on X1/CLK / 16 */
		duart_count(d, 16);
		break;
	}
}

void duart_reset(struct duart *d)
{
	d->ctr = 0xFFFF;
	d->ct = 0x0000;
	d->acr = 0xFF;
	d->opr = 0;   /* Specified by section 2.4 RESET. */
	d->isr = 0;
	d->imr = 0;
	d->ivr = 0xF;
	d->ip = 0x00; /* Data sheet does not specify this. */
	d->port[0].mrp = 0;
	d->port[0].sr = 0x00;
	d->port[1].mrp = 0;
	d->port[1].sr = 0x00;
}

uint8_t do_duart_read(struct duart *d, uint16_t address)
{
	switch (address & 0x0F) {
	case 0x00:		/* MR1A/MR2A */
		if (d->port[0].mrp)
			return d->port[0].mr2;
		d->port[0].mrp = 1;
		return d->port[0].mr1;
	case 0x01:		/* SRA */
		return d->port[0].sr;
	case 0x02:		/* BRG test */
	case 0x03:		/* RHRA */
		return duart_input(d, 0);
	case 0x04:		/* IPCR */
		/*
		** Section 4.3.14.1: [The state change bits] are cleared
		** when the CPU reads the input port change register.
		*/
		d->ipcr &= 0xF;
		/*
		** Section 4.3.15.1: This [ISR] bit [7] is cleared
		** when the CPU reads the input port change register.
		*/
		d->isr &= 0x7F;
		return d->ipcr;
	case 0x05:		/* ISR */
		return d->isr;
	case 0x06:		/* CTU */
		return d->ct >> 8;
	case 0x07:		/* CTL */
		return d->ct & 0xFF;
	case 0x08:		/* MR1B/MR2B */
		if (d->port[1].mrp)
			return d->port[1].mr2;
		d->port[1].mrp = 1;
		return d->port[1].mr1;
	case 0x09:		/* SRB */
		return d->port[1].sr;
	case 0x0A:		/* 1x/16x Test */
	case 0x0B:		/* RHRB */
		return duart_input(d, 1);
	case 0x0C:		/* IVR */
		return d->ivr;
	case 0x0D:		/* IP */
		return d->ip;
	case 0x0E:		/* START */
		d->ct = d->ctr;
		d->ctstop = 0;
		return 0xFF;
	case 0x0F:		/* STOP */
		d->ctstop = 1;
		duart_irq_lower(d, 0x08);
		return 0xFF;
	}
	return 0xFF;
}

uint8_t duart_read(struct duart *d, uint16_t address)
{
	uint8_t value = do_duart_read(d, address);
	if (d->trace)
		fprintf(stderr, "duart: read reg %02X -> %02X\n",
			address >> 1, value);
	return value;
}

void duart_write(struct duart *d, uint16_t address, uint8_t value)
{
	int bgrc = 0;

	value &= 0xFF;

	if (d->trace)
		fprintf(stderr, "duart: write reg %02X <- %02X\n",
			address >> 1, value);

	switch (address & 0x0F) {
	case 0x00:
		if (d->port[0].mrp)
			d->port[0].mr2 = value;
		else
			d->port[0].mr1 = value;
		break;
	case 0x01:
		d->port[0].csr = value;
		bgrc = 1;
		break;
	case 0x02:
		duart_command(d, 0, value);
		break;
	case 0x03:
		duart_output(d, 0, value);
		break;
	case 0x04:
		d->acr = value;
		duart_irq_calc(d);
		bgrc = 1;
		break;
	case 0x05:
		d->imr = value;
		duart_irq_calc(d);
		break;
	case 0x06:
		d->ctr &= 0xFF;
		d->ctr |= value << 8;
		break;
	case 0x07:
		d->ctr &= 0xFF00;
		d->ctr |= value;
		break;
	case 0x08:
		if (d->port[1].mrp)
			d->port[1].mr2 = value;
		else
			d->port[1].mr1 = value;
		break;
	case 0x09:
		d->port[1].csr = value;
		break;
	case 0x0A:
		duart_command(d, 1, value);
		break;
	case 0x0B:
		duart_output(d, 1, value);
		break;
	case 0x0C:
		d->ivr = value;
		break;
	case 0x0D:
		d->opcr = value;
		break;
	case 0x0E:
		d->opr |= value;
		duart_signal_change(d, d->opr);
		break;
	case 0x0F:
		d->opr &= ~value;
		duart_signal_change(d, d->opr);
		break;
	}
	if (bgrc && d->trace) {
		fprintf(stderr, "BGR %d\n", d->acr >> 7);
		fprintf(stderr, "CSR %d\n", d->port[0].csr >> 4);
	}
}

void duart_set_input_pin(struct duart *d, const int pin_number)
{
	if (pin_number < 0 || pin_number > 5) return;
	uint8_t before = d->ip;
	const uint8_t pin_mask = 1 << pin_number;
	if (pin_number < 4) {
		/* Indicate state change if pin value is currently 0. */
		if (!(d->ip & pin_mask)) {
			d->ipcr |= (pin_mask << 4);
			d->isr |= 0x80; /* Set ISR[7] on pin state change. */
		}
		d->ipcr |= pin_mask;
	}
	d->ip |= pin_mask;
	if (d->trace) fprintf(stderr, "DUART IP set pin %d: 0x%02x -> 0x%02x\n", pin_number, before, d->ip);
	duart_irq_calc(d);
}

void duart_clear_input_pin(struct duart *d, const int pin_number)
{
	if (pin_number < 0 || pin_number > 5) return;
	uint8_t before = d->ip;
	const uint8_t pin_mask = 1 << pin_number;
	if (pin_number < 4) {
		/* Indicate state change if pin value is currently 1. */
		if (d->ip & pin_mask) {
			d->ipcr |= (pin_mask << 4);
			d->isr |= 0x80; /* Set ISR[7] on pin state change */
		}
		d->ipcr &= ~pin_mask;
	}
	d->ip &= ~pin_mask;
	if (d->trace) fprintf(stderr, "DUART IP clear pin %d: 0x%02x -> 0x%02x\n", pin_number, before, d->ip);

	duart_irq_calc(d);
}

void duart_set_input(struct duart *duart, int port)
{
	duart->input = port;
}

void duart_trace(struct duart *duart, int onoff)
{
	duart->trace = onoff;
}

uint8_t duart_irq_pending(struct duart *d)
{
	return d->irq;
}

struct duart *duart_create(void)
{
	struct duart *d = malloc(sizeof(struct duart));
	if (d == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(d, 0, sizeof(*d));
	duart_reset(d);
	return d;
}

void duart_free(struct duart *d)
{
	free(d);
}

uint8_t duart_vector(struct duart *d)
{
	return d->ivr;
}
