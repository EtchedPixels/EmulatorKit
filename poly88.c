/*
 *	Poly88: An early 8080 and S/100 system.
 *
 *	8080 at 1.8432MHz
 *	512 bytes of RAM
 *	3K onboad EPROM, 1K used for system monitor.
 *
 *	As with a lot of early systems the design didn't quite fit
 *	CP/M when it arrived so mods and reworks were done. In the case
 *	of the Poly88 this mostly consisted of moving the video to F800
 *	(as became default anyway in the 4.0 monitor), and also some
 *	small changes to allow the paging out of the internal memory
 *	using spare BRG bits.
 *
 *	The "official" Poly88 CP/M had an 8K RAM at moved from E000-FFFF
 *	to 0000-1FFF when the internal devices paged out, and there was even
 *	a rare "twin" two user system where you could switch between two
 *	video/memory banks between 2000-DFFF and the keyboard I/O switched
 *	with it.
 *
 *	Generally however CP/M was used with the BRG latch providing
 *	PHANTOM and disabling the internal ROM and I/O when needed, whilst
 *	keeping the video at F800.
 *
 *	The default emulation is of a non revision F board that has been
 *	modded for CP/M with the BRG latch change for PHANTOM. The -t
 *	option allows the selection of the System 88 style change where
 *	instead the RAM at E000-FFFF remaps over the I/O space using a
 *	different set of port toggles and the BRG bit controls which "user"
 *	side of the machine (RAM bank, keyboard, video) is mapped.
 *
 *	For I/O we emulate the tarbell FDC, this is fine for CP/M but
 *	the Poly88 DOS would require we emulated the original Polymorphic
 *	single sided FDC, which is a complex beast.
 *
 *	TODO:
 *	- 8251 interrupt emulation
 *	- Tape load as serial dev plugin
 *	- Correct the few differing font symbols
 *	- How did the keyboard interrupt work with two keyboards
 *	- Support two keyboards
 *
 *	Mysteries:
 *	- The Poly88 twin mode is untested with real software. It's not
 *	  clear if the original software exists any more as it would have
 *	  only been on a small number of machines.
 *	- The hard disk controller is a complete unknown. It appears to have
 *	  been a PRIAM "intelligent" controller and a simple interface card
 *	  so quite possibly SASI.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "intel_8080_emulator.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "i8251.h"
#include "ide.h"
#include "asciikbd.h"
#include "nasfont.h"	/* Near enough correct as makes no difference */
#include "wd17xx.h"
#include "tarbell_fdc.h"

static uint8_t rom[3072];
static uint8_t highrom[4096];		/* High ROM off the I/O bus */
static uint8_t iram[512];
static unsigned rom_size;
/* The real font is 7 x 9 but we expand it all into words for
   convenience and to deal with the 10x15 block graphics */
static uint16_t font[4096];
static uint8_t ram[2][65536];		/* External RAM */
static uint16_t ramsize = 63488;	/* Up to the video and ROM space */

static unsigned int fast;
static volatile int done;
static unsigned emulator_done;


static unsigned twinsys;	/* Twin system */
static unsigned twin;		/* Twin side in use */

static struct ide_controller *ide;
static struct i8251 *uart;
static struct wd17xx *fdc;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	4
#define TRACE_SERIAL	8
#define TRACE_IRQ	16
#define TRACE_CPU	32
#define TRACE_FDC	64
#define TRACE_PRIAM	128

static int trace = 0;

static unsigned live_irq;

static uint8_t idle_bus = 0xFF;
static uint16_t iobase = 0x0000;
static uint16_t vidbase = 0xF800; /* F800 and 8800 commonly used */
static uint8_t brg_latch;
static uint8_t sstep;		/* TODO : need to make ifetches visible
				   from emulation */
static unsigned cpmflipflop;
static unsigned rtc_int;
static uint8_t vram[2][1024];	/* 512byte or 1K video RAM */

/* 7 x 9 for char matrix in a 10 x 15 field */
#define CWIDTH 10
#define CHEIGHT 15

static SDL_Window *window[2];
static SDL_Renderer *render[2];
static SDL_Texture *texture[2];
static uint32_t texturebits[2][64 * CWIDTH * 16 * CHEIGHT];
static struct asciikbd *kbd[2];

/* Simple setup to begin with */
static uint8_t *mem_map(uint16_t addr, bool wr)
{
	/* On the twin system (and maybe some others) the "official" CP/M
	   solution has the 8K shared RAM at E000 also appear at 0000 when
	   the CP/M state is toggled. TODO: do we need to split this from
	   twinsys into it's own mode */
	if (twinsys && cpmflipflop) {
		if (addr < 0x2000)
			return &ram[0][addr + 0xE000];
	}
	if (twinsys || !(brg_latch & 0x20)) {
		if ((addr & 0xF000) == iobase) {
			addr &= 0x0FFF;
			if (addr < 0x0C00) {
				if (!wr) {
					if (addr >= rom_size)
						return &idle_bus;
					return rom + addr;
				}
				return NULL;
			}
			return iram + (addr & 0x01FF);
		}
	}
	/* TODO: base system brg latch doesn't affect video. Later setups
	   it is paged with internal I/O, twin system two videos are paged
	   this way ! */
	if ((addr & 0xFC00) == vidbase)
		return &vram[twin][addr & 0x03FF];	/* Assume 1K fitted */
	/* On the clasic set up there is no RAM between 1000-2000 as it's
	   used for video and other I/O */
	if (twinsys && addr < 0x2000) {
		if (wr)
			return NULL;
		return &idle_bus;
	}
	/* Upper ROM from an I/O card */
	if ((addr & 0xFC00) == 0xFC00) {
		if (wr)
			return NULL;
		return highrom + (addr & 0x3FF);
	}
	/* Twin system */
	if (twinsys && addr >= 0xE000)
		return &ram[0][addr];
	/* External memory goes here */
	if (addr < ramsize || addr >= 0xF000)
		return &ram[twin][addr];
	if (wr)
		return NULL;
	return &idle_bus;
}

uint8_t i8080_read(uint16_t addr)
{
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	r = *mem_map(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);
	return r;
}

uint8_t i8080_debug_read(uint16_t addr)
{
	return *mem_map(addr, 0);
}

void i8080_write(uint16_t addr, uint8_t val)
{
	uint8_t *p;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	p = mem_map(addr,  1);
	if (p)
		*p = val;
}

static unsigned poly_irq(void);

static uint8_t irqvec;

uint8_t i8080_get_vector(void)
{
	return irqvec;
}

static uint8_t calc_vector(void)
{
	/* No single step yet - RST 7 */
	if (rtc_int)
		return 0xF7;	/* RST 6 */
	if (asciikbd_ready(kbd[twin]))
		return 0xEF;	/* RST 5 */
	/* TODO i8251 interrupt support to do RST 4 */
	if (poly_irq())
		return 0xD7;	/* RST 2 */
	return 0xFF;		/* Beats me */
}

/* TODO: vectors and vector support on 8080.c */
void recalc_interrupts(void)
{
	/* No single step yet. TODO add 8251 int support */
	live_irq = asciikbd_ready(kbd[twin]) | rtc_int | poly_irq();
	if (live_irq) {
		i8080_set_int(INT_IRQ);
		irqvec = calc_vector();
	} else
		i8080_clear_int(INT_IRQ);
}

static void poll_irq_event(void)
{
	recalc_interrupts();
}

/* TODO: recalc ints if we touch the 8251 */
static uint8_t onboard_in(uint8_t addr)
{
	if (addr < 4)
		return i8251_read(uart, addr & 1);
	if (addr >= 8 && addr < 0x0C)
		cpmflipflop = 0;
	if (addr >= 0x0C)
		cpmflipflop = 1;
	return 0xFF;
}

static void onboard_out(uint8_t addr, uint8_t val)
{
	if (addr < 4)
		i8251_write(uart, addr & 1, val);
	else if (addr < 8) { 
		brg_latch = val;
		if (twinsys)
			twin = (brg_latch & 0x20) ? 1: 0;
		/* FIXME: the other latch effects change too - no RAM
		   over low memory in twin mode that's driven by the
		   CP/M ports */
	} else if (addr < 0x0C) {
		rtc_int = 0;
		recalc_interrupts();
	} else
		sstep = 2;
}

/* The video I/O ports are used for the parallel keyboard */
static uint8_t video_in(uint8_t addr)
{
	if (addr & 1)
		return 0xFF;	/* TODO: decode option on some boards */
	asciikbd_ack(kbd[twin]);
	recalc_interrupts();
	return asciikbd_read(kbd[twin]);
}

static void video_out(uint8_t addr, uint8_t val)
{
}

/*
 *	Polymorphic FDC. Actually a 6852 and an 8255 but we avoid
 *	emulating the 6852 mostly in particular
 *
 *	Very minimal emulation
 */

static unsigned poly_sec;
static unsigned poly_track;
static unsigned poly_byte;	/* Track through bytes as we read/write */
static unsigned poly_secint;	/* Sector change */
static unsigned poly_ppia;
static unsigned poly_ppic;
static unsigned poly_ppictrl;
static uint8_t fdbuf[256];
static int poly_fd[2] = { -1, -1 };
static int poly_drive = -1;

static unsigned poly_irq(void)
{
	/* No sector int pending */
	if ((poly_secint & 1) == 0)
		return 0;
	/* int isn't enabled anyway */
	if ((poly_ppic & 4) == 0)
		return 0;
	return 1;
}

static void poly_tick(void)
{
	if (poly_ppic & 8) {	/* Only if motor is on */
		poly_sec++;
		if (poly_sec == 10)
			poly_sec = 0;
		poly_byte = 0;
		poly_secint = 3;
		if (trace & TRACE_FDC)
			fprintf(stderr, "polyfd: sec %u\n", poly_sec);
	}
}

/* Figure out what is coming back from the drive */
/* Read side we don't see the 10 sync up 0 bytes */
static uint8_t poly_rbyte(void)
{
	static uint16_t poly_cs;
	off_t off;

	if (poly_drive == -1)
		return 0xFF;

	if (poly_byte == 0) {
		poly_cs = 0;
		poly_byte++;
		off = (poly_track * 10 + poly_sec) * 256;
		if (trace & TRACE_FDC)
			fprintf(stderr, "polyfd%u: reading offset %lu\n",
				poly_drive, (long)off);
		if (lseek(poly_fd[poly_drive], off, 0) < 0 ||
			read(poly_fd[poly_drive], fdbuf, 256) != 256) {
			fprintf(stderr, "polyfd: I/O error reading %02X:%1X\n",
				poly_track, poly_sec);
			return 0xFF; /* Force an error */
		} else
			return poly_sec | 0x80;
	}
	if (poly_byte == 1) {
		poly_byte++;
		return poly_track;
	}
	if (poly_byte == 258) {
		poly_byte++;
		return ~poly_cs;
	}
	if (poly_byte == 259)
		return ~(poly_cs >> 8);

	/* It's the 256 data bytes */
	if (poly_byte & 1)
		poly_cs += fdbuf[poly_byte - 2] << 8;
	else
		poly_cs += fdbuf[poly_byte - 2];
	return fdbuf[poly_byte++ - 2];
}

static void poly_wbyte(uint8_t v)
{
	if (poly_drive == -1)
		return;

	/* 10 zeros, 2 sync, sec, track , and checksum we ignore */
	if (poly_byte < 14 || poly_byte > 269) {
		if (++poly_byte == 272) {
			off_t off = (poly_track * 10 + poly_sec) * 256;
			if (lseek(poly_fd[poly_drive], off, 0) < 0 ||
				write(poly_fd[poly_drive], fdbuf, 256) != 256) {
				fprintf(stderr, "polyfd: I/O error writing %02X:%1X\n",
					poly_track, poly_sec);
			}
		}
		return;
	}
	/* Data */
	fdbuf[poly_byte++ - 14] = v;
}

static void poly_step(void)
{
	if (!(poly_ppic & 0x40)) {
		if (poly_track > 0)
			poly_track--;
	} else {
		if (poly_track < 39)
			poly_track++;
	}
	if (trace & TRACE_FDC)
		fprintf(stderr, "polyfd: track now %d\n", poly_track);
}

static uint8_t poly_6852_ready(void)
{
	uint8_t r = 0;

	if (poly_drive >= 0 && (poly_ppic & 0x08)) {
		if ((poly_ppic & 0x20 ) && poly_byte < 260)
			r |= 1;	/* Read stream */
		else if (poly_ppic & 0x10)
			r |= 2; /* Write stream */
	}
	return r;
}

static uint8_t do_polyfd_read(uint8_t addr)
{
	uint8_t r;

	switch(addr) {
	case 0x00:
		return poly_6852_ready();
	case 0x01:
		return poly_rbyte();
	case 0x08:
		return poly_ppia;
	case 0x09:
		/* WP is 0x20 TODO */
		r = poly_sec;
		if (poly_track == 0)
			r |= 0x10;
		poly_secint = 0;
		poll_irq_event();
		/* DS sets 0x80 TODO */
		return r;
	case 0x0A:
		return (poly_ppic & 0xFC) | poly_secint;
	case 0x0B:
		return poly_ppictrl;
	}
	return 0xFF;
}

static uint8_t polyfd_read(uint8_t addr)
{
	uint8_t r = do_polyfd_read(addr);
	if (trace & TRACE_FDC)
		fprintf(stderr, "polyfd_read: %02X <- %02X\n",
			addr + 0x20, r);
	return r;
}

static void polyfd_write(uint8_t addr, uint8_t val)
{
	uint8_t old_ppic = poly_ppic;
	uint8_t delta;
	uint8_t bit;

	if (trace & TRACE_FDC)
		fprintf(stderr, "polyfd_write: %02X -> %02X\n",
			addr + 0x20, val);

	switch(addr) {
	case 0x00:
		break;
	case 0x01:
		poly_wbyte(val);
		break;
	case 0x08:
		poly_ppia = val;
		break;
	case 0x09:
		break;
	case 0x0A:
		poly_ppic &= 3;
		poly_ppic |= val & ~0xFC;
		break;
	case 0x0B:
		if (val & 0x80) {
			poly_ppictrl = val;
			break;
		}
		bit = 1 << ((val >> 1) & 0x07);
		if (val & 1)
			poly_ppic |= bit;
		else
			poly_ppic &= ~bit;
		poll_irq_event();
		break;
	}
	delta = poly_ppic ^ old_ppic;
	if (delta & old_ppic & 0x80)
		poly_step();
	switch(poly_ppia & 3) {
	case 0:
	case 3:
		poly_drive = -1;
		break;
	case 1:
		poly_drive = 0;
		break;
	case 2:
		poly_drive = 1;
		break;
	}
	if (trace & TRACE_FDC) {
		if (delta & 0x08)
			fprintf(stderr, "polyfd: motor %s\n",
				(poly_ppic & 0x08) ? "on":"off");
	}
}

static void polyfd_attach(unsigned drive, const char *path)
{
	poly_fd[drive] = open(path, O_RDWR);
	if (poly_fd[drive] == -1)
		perror(path);
	fprintf(stderr, "polyfd drive %u fd %d path %s\n", drive, poly_fd[drive], path);
}

/*
 *	First guesses at the Priam controller: untested
 *
 *	It seems to be sort of SASI but without much of the bus logic
 *	skipped. This should be sufficient code to start poking around
 *	with Exec and the HD driver to see what else is going on.
 */
 
static uint8_t priam_bus;
static uint8_t priam_cmd[6];
static uint8_t priam_status[2];
static uint8_t priam_data[256];
static uint8_t *priam_rptr;
static uint8_t *priam_wptr;
static unsigned priam_rxc;
static unsigned priam_txc;
static int priam_fd = -1;

/*
 *	A read of a block of data has finished. Right now that has
 *	to be
 */
static void priam_read_done(void)
{
	/* Read command in progress ? */
	if (!(priam_bus & 0x08) && priam_cmd[0] == 0x08) {
		/* Count through blocks */
		if (priam_cmd[4]) {
			priam_cmd[4]--;
			if (read(priam_fd, priam_data, 256) == 256) {
				priam_rptr = priam_data;
				priam_rxc = 256;
				return;
			}
			priam_status[0] = 0x02;
			priam_bus |= 0x08;
			/* TODO: sense data */
		} else {
			fprintf(stderr, "priam: read completed go to status\n");
			priam_status[0] = 0x00;
			priam_status[1] = 0x00;
		}
		/* Status so set up for status read */
		priam_bus |= 0x08;
		priam_rptr = priam_status;
		priam_rxc = 2;
		return;
	}
	/* Status so now go idle */
	priam_bus = 0x40;
	/* Wait for command */
	priam_wptr = priam_cmd;
	priam_txc = 6;
}

/* A SASI READ command was issued */
/* TODO: check disk size properly esp for write */
static void priam_do_read(void)
{
	unsigned block;

	block = (priam_cmd[1] & 0x0F) << 16;
	block |= priam_cmd[2] << 8;
	block |= priam_cmd[3];

	if (trace & TRACE_PRIAM)
		fprintf(stderr, "priam: reading block %u\n", block);

	if (lseek(priam_fd, block * 256, 0) == -1 ||
		read(priam_fd, priam_data, 256) != 256) {
		priam_status[0] = 0x02;
		priam_status[1] = 0x00;
		priam_bus |= 0x08;
		priam_rptr = priam_status;
		priam_rxc = 2;
		fprintf(stderr, "priam read failed\n");
	} else {
		priam_rptr = priam_data;
		priam_rxc = 256;
		priam_bus &= ~0x08;
		fprintf(stderr, "priam read begins (%u blocks)\n", priam_cmd[4]);
		priam_cmd[4]--;
	}
}

/* A disk write of a block has completed writing. Push it to disk and
   see what to do next */
static void priam_write_block(void)
{
	/* Count through blocks */
	if (priam_cmd[4]) {
		priam_cmd[4]--;
		if (write(priam_fd, priam_data, 256) == 256) {
			priam_wptr = priam_data;
			priam_txc = 256;
			return;
		}
		priam_status[0] = 0x02;
		/* TODO: sense data */
	} else {
		priam_status[0] = 0x00;
		priam_status[1] = 0x00;
	}
	/* Status so set up for status read */
	priam_bus |= 0x08;
	priam_rptr = priam_status;
	priam_rxc = 2;
}

/* Begin a SASI WRITE command: TODO check disk size */
static void priam_write_begin(void)
{
	unsigned block;

	block = (priam_cmd[1] & 0x0F) << 16;
	block |= priam_cmd[2] << 8;
	block |= priam_cmd[3];

	if (trace & TRACE_PRIAM)
		fprintf(stderr, "priam: writingg block %u\n", block);

	if (lseek(priam_fd, block * 256, 0) == -1) {
		priam_status[0] = 0x02;
		priam_status[1] = 0x00;
		priam_bus |= ~0x08;
		priam_rptr = priam_status;
		priam_rxc = 2;
	} else {
		priam_wptr = priam_data;
		priam_txc = 256;
		priam_bus &= ~0x08;
	}
}

/* We've finished writing the expected block. That might be a command or
   SASI write data */
static void priam_write_done(void)
{
	/* Six bytes written to the command buffer */
	if (priam_wptr == priam_cmd + 6) {
		/* Issue a command */
		priam_status[0] = 0;
		priam_status[1] = 0;

		if (trace & TRACE_PRIAM)
			fprintf(stderr, "priam_cmd: %02X %02X %02X %02X %02X %02X\n",
				priam_cmd[0], priam_cmd[1], priam_cmd[2], priam_cmd[3],
				priam_cmd[4], priam_cmd[5]);
		/* TODO: sense etc */
		switch(priam_cmd[0]) {
		case 0x00: /* TUR */
		case 0x01: /* REZERO */
			break;
		case 0x08: /* READ */
			priam_do_read();
			return;
		case 0x0A: /* WRITE */
			priam_write_begin();
			return;
		case 0xE0:	/* Diagnostics */
		case 0xE4:
			break;
		default:
			priam_status[0] = 0x02;
			break;
		}
		priam_rptr = priam_status;
		priam_rxc = 2;
		priam_bus |= 0x08;
		return;
	}
	/* Data block */
	if (priam_cmd[0]  == 0x10 && (priam_bus & 0x08)) {
		priam_write_block();	/* Updates status etc itself */
		return;
	} else {
		/* Fudge up an error code - may need to implement sense */
		priam_status[0] = 0x02;	/* Sense available */
		priam_status[1] = 0x00;
	} 
	priam_rptr = priam_status;
	priam_rxc = 2;
	priam_bus |= 0x08;
}

static uint8_t priam_read(uint8_t addr)
{
	uint8_t r;

	if (priam_fd == -1)
		return 0xFF;

	switch(addr) {
	case 0:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "priam: read bus state %02X\n", priam_bus);
		return priam_bus;
	case 1:
		if (priam_rxc) {
			priam_rxc--;
			r = *priam_rptr++;
			if (trace & TRACE_PRIAM)
				fprintf(stderr, "priam: read data %u\n", r);
			if (priam_rxc == 0)
				priam_read_done();
			return r;
		}
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "priam read data: no data\n");
		return 0xFF;	/* >?? */
	}
	return 0xFF;
}

static void priam_write(uint8_t addr, uint8_t val)
{
	switch(addr) {
	case 0:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "priam: write bus %u\n", val);
		/* Write bus: only known case is write 1 if bus 0x80 */
		priam_bus |= 0x40;	/* Until we figure this out */
		break;
	case 1:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "priam: write data %u (txc %u)\n", val, priam_txc);
		/* Write data */
		if (priam_txc) {
			priam_txc--;
			*priam_wptr++ = val;
			if (priam_txc == 0)
				priam_write_done();
		}
		break;
	case 3:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "priam: write reset %u\n", val);
		/* Reset ? Write of 0 done initially */
		priam_bus = 0x00;
		priam_txc = 6;	/* Command block wait */
		priam_wptr = priam_cmd;
		priam_rxc = 0;
		break;
	}
}

static void priam_attach(const char *path)
{
	priam_fd = open(path, O_RDWR);
	if (priam_fd == -1) {
		perror(path);
		exit(1);
	}
}

uint8_t i8080_inport(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr & 0xF0) == (iobase >> 8))
		return onboard_in(addr);
	if ((addr & 0xFC) == (vidbase >> 8))
		return video_in(addr);
	if (addr >= 0x20 && addr <= 0x2F)
		return polyfd_read(addr & 0x0F);
	if (addr >= 0x34 && addr <= 0x37)
		return priam_read(addr & 3);
	if (ide && addr >= 0xC0 && addr <= 0xC7)
		return ide_read8(ide, addr);
	if (fdc && addr >= 0xE8 && addr <= 0xEF)
		return tbfdc_read(fdc, addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void i8080_outport(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr & 0xF0) == (iobase >> 8))
		onboard_out(addr, val);
	else if ((addr & 0xFC) == (vidbase >> 8))
		video_out(addr, val);
	else if (addr >= 0x20 && addr <= 0x2F)
		polyfd_write(addr & 0x0F, val);
	else if (addr >= 0x34 && addr <= 0x37)
		priam_write(addr & 3, val);
	else if (ide && addr >= 0xC0 && addr <= 0xC7)
		ide_write8(ide, addr, val);
	else if (fdc && addr >= 0xE8 && addr <= 0xEF)
		tbfdc_write(fdc, addr & 7, val);
	else if (addr == 0xFD)
		trace = val;
	else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
	poll_irq_event();
}

static void raster_char(unsigned int unit, unsigned int y, unsigned int x, uint8_t c)
{
	uint16_t *fp = &font[16 * c];
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits[unit] + x * CWIDTH + 64 * CWIDTH * y * CHEIGHT;
	for (rows = 0; rows < CHEIGHT; rows++) {
		uint16_t bits = *fp++;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x8000)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 63 */
		pixp += 63 * CWIDTH;
	}
}

static void poly_rasterize(unsigned unit)
{
	unsigned int lines, cols;
	uint8_t *ptr = vram[unit];

	for (lines = 0; lines < 16; lines ++) {
		for (cols = 0; cols < 64; cols ++)
			raster_char(unit, lines, cols, *ptr++);
	}
}

static void poly_render(unsigned unit)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = 64 * CWIDTH;
	rect.h = 16 * CHEIGHT;

	SDL_UpdateTexture(texture[unit], NULL, texturebits[unit], 64 * CWIDTH * 4);
	SDL_RenderClear(render[unit]);
	SDL_RenderCopy(render[unit], texture[unit], NULL, &rect);
	SDL_RenderPresent(render[unit]);
}

static void make_graphics(void)
{
	/* The lower font is really made by logic and is 10x15 pixels
	   in a 2 x 3 grid */
	unsigned c;
	uint16_t v;
	uint16_t *p = font;
	uint8_t *n = nascom_font_raw;

	for (c = 0; c < 127; c++) {
		switch(c & 0x24) {
		case 0x00:
			v = 0xFFC;
			break;
		case 0x04:
			v = 0xF80;
			break;
		case 0x20:
			v = 0x07C;
			break;
		case 0x24:
			v = 0x000;
			break;
		}
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		switch(c & 0x12) {
		case 0x00:
			v = 0xFFC;
			break;
		case 0x02:
			v = 0xF80;
			break;
		case 0x10:
			v = 0x07C;
			break;
		case 0x12:
			v = 0x000;
			break;
		}
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		switch(c & 0x09) {
		case 0x00:
			v = 0xFFC;
			break;
		case 0x01:
			v = 0xF80;
			break;
		case 0x08:
			v = 0x07C;
			break;
		case 0x09:
			v = 0x000;
			break;
		}
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		*p++ = v;
		p++;
	}
	p = font + 128 * 16;
	for (c = 0; c < 128 * 16; c++)
		*p++ = (uint16_t)(*n++) << 8;
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "poly88: [-A disk] [-B disk] [-f] [-r path] [-m mem Kb] [-v vidbase] [-d debug] [-h hd] [-i ide] [-p] [-t]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "poly88.rom";
	char *drive_a = NULL, *drive_b = NULL;
	char *priampath = NULL;
	char *idepath = NULL;
	unsigned n;
	unsigned polyfdc = 0;

	while ((opt = getopt(argc, argv, "A:B:d:F:fh:i:m:pr:tv:")) != -1) {
		switch (opt) {
		case 'A':
			drive_a = optarg;
			break;
		case 'B':
			drive_b = optarg;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'h':
			priampath = optarg;
			break;
		case 'i':
			idepath = optarg;
			break;
		case 'm':
			ramsize = 1024 * atoi(optarg);
			break;
		case 'p':
			polyfdc = 1;
			break;
		case 't':
			twinsys = 1;
			break;
		case 'v':
			vidbase = strtoul(optarg, NULL, 0) & 0xFFC0;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	rom_size = read(fd, rom, 3072);
	if (rom_size < 1024) {
		fprintf(stderr, "poly88: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (polyfdc) {
		if (drive_a)
			polyfd_attach(0, drive_a);
		if (drive_b)
			polyfd_attach(1, drive_b);
	} else {
		if (drive_a || drive_b) {
			fdc = tbfdc_create();
			if (trace & TRACE_FDC)
				wd17xx_trace(fdc, 1);
			if (drive_a)
				wd17xx_attach(fdc, 0, drive_a, 1, 80, 26, 128);
			if (drive_b)
				wd17xx_attach(fdc, 0, drive_b, 1, 80, 26, 128);
		}
	}

	if (priampath)
		priam_attach(priampath);

	/* TODO so for now make it look like bus idle */
	memset(highrom, 0xFF, sizeof(highrom));

	/* Build the render font from the logic tables and also the
	   MCM667x */
	make_graphics();

	if (idepath) {
		ide = ide_allocate("cf0");
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		}
		ide_attach(ide, 0, fd);
	}

	atexit(SDL_Quit);
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "poly88: unable to initialize SDL: %s\n",
			SDL_GetError());
		exit(1);
	}
	for (n = 0; n <= twinsys; n++) {
		window[n] = SDL_CreateWindow("Poly88",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			64 * CWIDTH, 16 * CHEIGHT,
			SDL_WINDOW_RESIZABLE);
		if (window[n] == NULL) {
			fprintf(stderr, "poly88: unable to open window: %s\n",
			SDL_GetError());
			exit(1);
		}
		render[n] = SDL_CreateRenderer(window[n], -1, 0);
		if (render[n] == NULL) {
			fprintf(stderr, "poly88: unable to create renderer: %s\n",
				SDL_GetError());
			exit(1);
		}
		texture[n] = SDL_CreateTexture(render[n], SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			64 * CWIDTH, 16 * CHEIGHT);
		if (texture[n] == NULL) {
			fprintf(stderr, "poly88: unable to create texture: %s\n",
				SDL_GetError());
			exit(1);
		}
		SDL_SetRenderDrawColor(render[n], 0, 0, 0, 255);
		SDL_RenderClear(render[n]);
		SDL_RenderPresent(render[n]);
		SDL_RenderSetLogicalSize(render[n], 64 * CWIDTH,  16 * CHEIGHT);
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	/* 20ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON | ECHO);
		term.c_cc[VMIN] = 0;
		term.c_cc[VTIME] = 1;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VSTOP] = 0;
		tcsetattr(0, TCSADRAIN, &term);
	}

	uart = i8251_create();
	if (trace & TRACE_SERIAL)
		i8251_trace(uart, 1);
	i8251_attach(uart, &console);

	kbd[0] = asciikbd_create();
	if (twinsys)
		kbd[1] = asciikbd_create();

	i8080_reset();
	if (trace & TRACE_CPU)
		i8080_log = stderr;

	/* We run 1843200 t-states per second */
	while (!emulator_done) {
		int i;
		for (i = 0; i < 144; i++) {
			i8080_exec(256);
			i8251_timer(uart);
			poll_irq_event();
		}
		/* We want to run UI events regularly it seems */
		poly_rasterize(0);
		poly_render(0);
		asciikbd_event(kbd[0]);
		if (twinsys) {
			poly_rasterize(1);
			poly_render(1);
			/* FIXME */
/* Not yet possible without fixing our SDL layer asciikbd_event(kbd[1]); */
		}
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (fdc)
			wd17xx_tick(fdc, 20);
		poly_tick();
		rtc_int = 1;
		poll_irq_event();
	}
	exit(0);
}
