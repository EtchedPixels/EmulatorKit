/*
 *	Z80 co-processor card model - only one for now
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "libz80/z80.h"
#include "z80copro.h"

/*
 *	Coprocessor internal memory and I/O model
 */

#define TRACE_IO	1
#define TRACE_MEM	2

static struct z80copro *copro[MAX_COPRO];
static int copro_next;

static struct z80copro *get_copro(int n)
{
	if (n < 0 || n >= MAX_COPRO || !copro[n]) {
		fprintf(stderr, "Bad copro %d.\n", n);
		exit(1);
	}
	return copro[n];
}

static uint8_t sec_ior(int unit, uint16_t addr)
{
	struct z80copro *c = get_copro(unit);
	if (c->trace & TRACE_IO)
		fprintf(stderr,"C[%X] copro reads %02X\n", unit, (unsigned int)(c->masterbits & 0xFF));
	return c->masterbits & 0xFF;
}

static void sec_iow(int unit, uint16_t addr, uint8_t data)
{
	struct z80copro *c = get_copro(unit);
	c->latches = (addr & 0xFF00) | data;
	c->rambank = (c->latches >> 11) & 0x07;
	if (c->trace & TRACE_IO)
		fprintf(stderr,"C[%X] latches to %04X\n", unit, (unsigned int)(c->latches));
}

static uint8_t *mdecode(struct z80copro *c, uint16_t addr, uint8_t wr)
{
	if (addr < 0x8000 && (c->latches & ROMEN) == 0) {
		if (wr)
			return NULL;
		return &c->eprom[addr];
	}
	return &c->ram[c->rambank][addr];
}

static uint8_t sec_memr(int unit, uint16_t addr)
{
	struct z80copro *c = get_copro(unit);
	uint8_t *p = mdecode(c, addr, 0);
	if (c->trace & TRACE_MEM)
		fprintf(stderr, "R[%X] %04X = %02X\n", unit, addr, *p);
	return *p;
}

static void sec_memw(int unit, uint16_t addr, uint8_t val)
{
	struct z80copro *c = get_copro(unit);
	uint8_t *p = mdecode(c, addr, 1);
	if (p == NULL) {
		fprintf(stderr, "C[%X] ROM write attempted %04X\n", unit,
			addr);
		return;
	}
	if (c->trace & TRACE_MEM)
		fprintf(stderr, "W[%X] %04X <- %02X\n", unit, addr, val);
	*p = val;
}

/*
 *	Reset the device, as on power up. Unlike a pure CPU reset this
 *	also clears all the latches
 */
void z80copro_reset(struct z80copro *c)
{
	Z80RESET(&c->cpu);
	c->cpu.ioRead = sec_ior;
	c->cpu.ioWrite = sec_iow;
	c->cpu.memRead = sec_memr;
	c->cpu.memWrite = sec_memw;
	c->cpu.memParam = c->unit;
	c->cpu.ioParam = c->unit;
	c->latches = 0;
	c->masterbits = 0;
	c->tstates = 37;
	c->rambank = 0;
	c->irq_pending = 1;
	c->nmi_pending = 1;
}

/*
 *	Return the EPROM image base
 */
uint8_t *z80copro_eprom(struct z80copro *c)
{
	return c->eprom;
}

/*
 *	Briefly run the co-processor.
 */
void z80copro_run(struct z80copro *c)
{
	/* CPU is held in reset */
	if (!(c->masterbits & CORESET))
		return;
	if (c->irq_pending)
		Z80INT(&c->cpu, 0xFF);	/* Vector really not defined */
	/* FIXME: edge triggered ? so should catch on latch writes only */
	if (c->nmi_pending)
		Z80NMI(&c->cpu);
	Z80ExecuteTStates(&c->cpu, c->tstates);
}

/*
 *	Main system view of the co-processor card. The latches and their
 *	control effects
 */
void z80copro_iowrite(struct z80copro *c, uint16_t addr, uint8_t bits)
{
	c->masterbits = (addr & 0xFF00) | bits;
	if (!(c->masterbits & CORESET))
		Z80RESET(&c->cpu);
	c->nmi_pending = (c->masterbits & CONMI) ? 0 : 1;
	c->irq_pending = (c->masterbits & COIRQ) ? 0 : 1;
	if (c->trace & TRACE_IO)
		fprintf(stderr,"C[%X] host writes %04X\n", c->unit, (unsigned int)c->masterbits);
}

uint8_t z80copro_ioread(struct z80copro *c, uint16_t addr)
{
	if (c->trace & TRACE_IO)
		fprintf(stderr,"C[%X] host reads %02X\n", c->unit, (unsigned int)(c->latches & 0xFF));
	return c->latches & 0xFF;
}

int z80copro_intraised(struct z80copro *c)
{
	return (c->latches & MAINT) ? 0 : 1;
}

/*
 *	Create a coprocessor card
 */
struct z80copro *z80copro_create(void)
{
	struct z80copro *c;

	if (copro_next >= MAX_COPRO) {
		fprintf(stderr, "Too many co-processor cards.\n");
		exit(1);
	}
	c = malloc(sizeof(struct z80copro));
	if (c == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(c, 0, sizeof(struct z80copro));
	c->unit = copro_next++;
	copro[c->unit] = c;
	z80copro_reset(c);
	return c;
}

void z80copro_free(struct z80copro *c)
{
	/* FIXME: we don't reuse slots */
	copro[c->unit] = NULL;
	free(c);
}

void z80copro_trace(struct z80copro *c, int onoff)
{
	c->trace = onoff;
}
