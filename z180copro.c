/*
 *	Z180 co-processor card model - only one for now
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "libz180/z180.h"
#include "sdcard.h"
#include "z180_io.h"
#include "z180copro.h"

/*
 *	Coprocessor internal memory and I/O model
 */

#define TRACE_IO	1
#define TRACE_MEM	2

static struct z180copro *copro[MAX_COPRO];
static int copro_next;

static struct z180copro *get_copro(int n)
{
	if (n < 0 || n >= MAX_COPRO || !copro[n]) {
		fprintf(stderr, "Bad copro %d.\n", n);
		exit(1);
	}
	return copro[n];
}

/* Ugly: need to think how to fix this up */
static struct z180copro *find_copro(struct z180_io *io)
{
	unsigned n;
	for (n = 0; n < MAX_COPRO; n++) {
		if (copro[n] && copro[n]->io == io)
			return copro[n];
	}
	return NULL;
}

/* Coprocessor I/O model */
static uint8_t sec_ior(int unit, uint16_t addr)
{
	struct z180copro *c = get_copro(unit);
	if (z180_iospace(c->io, addr))
		return z180_read(c->io, addr);
	return 0xFF;
}

static void sec_iow(int unit, uint16_t addr, uint8_t data)
{
	struct z180copro *c = get_copro(unit);
	if (z180_iospace(c->io, addr)) {
		printf(">%X %X\n", addr, data);
		z180_write(c->io, addr, data);
	}
	if (c->sdcard && (addr & 0x81) == 0x80) {
		if (data & 1)
			sd_spi_raise_cs(c->sdcard);
		else
			sd_spi_lower_cs(c->sdcard);
	}
}

/* Coprocessor memory model */
static uint8_t *mdecode(struct z180copro *c, uint32_t addr, uint8_t wr)
{
	if (addr < 0x80000)
		return c->shared + (addr & 0x3FF);
	return c->ram + (addr & 0x7FFFF);
}

uint8_t z180_phys_read(int unit, uint32_t addr)
{
	struct z180copro *c = get_copro(unit);
	uint8_t *p = mdecode(c, addr, 0);
	if (c->trace & TRACE_MEM)
		fprintf(stderr, "R[%X] %06X = %02X\n", unit, addr, *p);
	if (addr == 0x3FF)
		c->state &= ~COPRO_IRQ_IN;
	return *p;
}

void z180_phys_write(int unit, uint32_t addr, uint8_t val)
{
	struct z180copro *c = get_copro(unit);
	uint8_t *p = mdecode(c, addr, 1);
	if (p == NULL) {
		fprintf(stderr, "C[%X] ROM write attempted %06X\n", unit,
			addr);
		return;
	}
	if (c->trace & TRACE_MEM)
		fprintf(stderr, "W[%X] %06X <- %02X\n", unit, addr, val);
	if (addr == 0x3FE)
		c->state |= COPRO_IRQ_OUT;
	*p = val;
}

/*
 *	Model CPU accesses starting with a virtual address
 */

static uint8_t do_mem_read(int unit, uint16_t addr, int quiet)
{
	struct z180copro *c = get_copro(unit);
	uint32_t pa = z180_mmu_translate(c->io, addr);
	uint8_t r;
	r = z180_phys_read(0, pa);
	if (!quiet && (c->trace & TRACE_MEM))
		fprintf(stderr, "R %04X[%06X] -> %02X\n", addr, pa, r);
	return r;
}

static uint8_t mem_read(int unit, uint16_t addr)
{
	return do_mem_read(unit, addr, 0);
}

static void mem_write(int unit, uint16_t addr, uint8_t val)
{
	struct z180copro *c = get_copro(unit);
	uint32_t pa = z180_mmu_translate(c->io, addr);
	if (c->trace & TRACE_MEM)
		fprintf(stderr, "W: %04X[%06X] <- %02X\n", addr, pa, val);
	z180_phys_write(0, pa, val);
}

/*
 *	Device interface
 */

#include "bitrev.h"

uint8_t z180_csio_write(struct z180_io *io, uint8_t bits)
{
	uint8_t r;
	struct z180copro *c = find_copro(io);

	if (c->sdcard == NULL)
		return 0xFF;

	r = bitrev[sd_spi_in(c->sdcard, bitrev[bits])];
//	if (trace & TRACE_SPI)
//		fprintf(stderr,	"[SPI %02X:%02X]\n", bitrev[bits], bitrev[r]);
	return r;
}


/*
 *	Reset the device, as on power up
 */
void z180copro_reset(struct z180copro *c)
{
	Z180RESET(&c->cpu);
	c->cpu.ioRead = sec_ior;
	c->cpu.ioWrite = sec_iow;
	c->cpu.memRead = mem_read;
	c->cpu.memWrite = mem_write;
	c->cpu.memParam = c->unit;
	c->cpu.ioParam = c->unit;
	c->state = COPRO_RESET;
	c->tstates = 37;
	c->irq_pending = 0;
}

/*
 *	Briefly run the co-processor.
 */
void z180copro_run(struct z180copro *c)
{
	static int n = 0;
	unsigned used;
	/* CPU is held in reset */
	if (c->state & COPRO_RESET)
		return;
	if (c->state & COPRO_IRQ_IN)
		Z180INT(&c->cpu, 0xFF);	/* Vector really not defined */
	n += c->tstates;
	while(n >= 0) {
		used = z180_dma(c->io);
		if (used == 0)
			used = Z180Execute(&c->cpu);
		n -= used;
	}
}

/*
 *	Main system view of the co-processor card. The latches and their
 *	control effects included
 */
void z180copro_iowrite(struct z180copro *c, uint16_t addr, uint8_t bits)
{
	uint16_t sma = (addr >> 8) | ((addr & 3) << 8);
	c->shared[sma] = bits;
	if (addr & 4)
		c->state &= ~COPRO_RESET;
	else
		c->state |= COPRO_RESET;
	if (sma == 0x3FF)
		c->state |= COPRO_IRQ_IN;
}

uint8_t z180copro_ioread(struct z180copro *c, uint16_t addr)
{
	uint16_t sma;
	sma = (addr >> 8) | ((addr & 3) << 8);
	if (addr & 4)
		c->state &= ~COPRO_RESET;
	else
		c->state |= COPRO_RESET;
	if (sma == 0x3FF)
		c->state &= ~COPRO_IRQ_OUT;
	return c->shared[sma];
}

int z180copro_intraised(struct z180copro *c)
{
	if (c->state & COPRO_RESET)
		return 0;
	return c->state & COPRO_IRQ_OUT;
}

/*
 *	Create a coprocessor card
 */
struct z180copro *z180copro_create(void)
{
	struct z180copro *c;

	if (copro_next >= MAX_COPRO) {
		fprintf(stderr, "Too many co-processor cards.\n");
		exit(1);
	}
	c = malloc(sizeof(struct z180copro));
	if (c == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(c, 0, sizeof(struct z180copro));
	c->unit = copro_next++;
	copro[c->unit] = c;
	c->io = z180_create(&c->cpu);
	z180copro_reset(c);
	return c;
}

void z180copro_free(struct z180copro *c)
{
	/* FIXME: we don't reuse slots */
	copro[c->unit] = NULL;
	free(c);
}

void z180copro_trace(struct z180copro *c, int onoff)
{
	c->trace = onoff;
}

void z180copro_attach_sd(struct z180copro *c, int fd)
{
	char buf[8];
	snprintf(buf, 8, "sd%d", c->unit);
	c->sdcard = sd_create(buf);
	sd_attach(c->sdcard, fd);
}
