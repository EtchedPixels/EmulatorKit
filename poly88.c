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

static uint8_t polydd_ctrl[16];	/* DPRAM on the smart dd controller */
static uint8_t polydd_ram[2048];

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
	if (twinsys) {
		if (addr >= 0x1000 && addr <= 0x17FF)
			return polydd_ram + (addr & 0x07FF);
		if (addr >= 0x1FE0 && addr <= 0x1FEF)
			return polydd_ctrl + (addr & 0x0F);
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
static unsigned poly_sync;	/* Is the 6852 synchronized */
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
		/* The 6852 synchronizes to the bits on the media, or if
		   it was in sync falls out of sync at the frame end */
		if (poly_sync == 2)
			poly_sync--;
		if (trace & TRACE_FDC)
			fprintf(stderr, "polyfd: sec %u sync now %u\n", poly_sec, poly_sync);
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
		if (trace & TRACE_FDC)
			fprintf(stderr, "polyfd: sec %d @%d: ignoring %02X\n", poly_sec, poly_byte, v);
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
	if (trace & TRACE_FDC)
		fprintf(stderr, "polyfd: sec %d @%d: copy %02X\n", poly_sec, poly_byte, v);
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

	/* No drive selected */
	if (poly_drive == -1)
		return 0;
	/* Motor not running */
	if (!(poly_ppic & 0x08))
		return 0;
	/* Read stream enabled and bytes left */
	if ((poly_ppic & 0x20) && poly_byte < 260)
		r |= 1;	/* Read stream */
	/* Write stream enabled and in sync */
	if ((poly_ppic & 0x10) && poly_sync == 1)
		r |= 2; /* Write stream */
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
		if (val == 0xC0) {	/* IDLE */
			poly_sync = 2;
			if (trace & TRACE_FDC)
				fprintf(stderr, "polyfd: looking for sector start sync\n");
		}
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
	if (trace & TRACE_FDC)
		fprintf(stderr, "polyfd drive %u fd %d path %s\n", drive, poly_fd[drive], path);
}

/*
 *	Poly88 double density control. Memory mapped
 *	0x1000-0x17FF	transfer buffer (2K)
 *	0x1FE0	sector number (word)
 *	0x1FE2  memory address for transfer
 *	0x1FE4	disk (4-7)
 *	0x1FE5	sectors to transfer (1-8)
 *	0x1FE6	controller command
 *		0 write
 *		1 read
 *		2 initialize
 *		4 buffer command
 *		5 disk size (handled in driver)
 *	0x1FE7	status (write FF to start I/O)
 *		Once written poll over 660us and see if it's not FF
 *		When it stops being FF copy the status and clear if nonzero
 *	0x1FEE  0x40 = single sided
 *	0x1FEF autodetect (writeable to 0 if controller) FF if idle bus
 *	0x1FF0-1FFF left clear for North Star FPU option
 */

/* Fake the behaviour of the disk side CPU */

static int polydd_fd[4] = { -1, -1, -1. -1 };

static void polydd_xfer_in(uint8_t *tmpbuf)
{
	unsigned addr = polydd_ctrl[0x02] | (polydd_ctrl[0x03] << 8);
	unsigned n;
	addr &= 0x7FF;	/* 2K */
	if (addr <= 0x0700) {	/* Will 256 bytes fit ? */
		memcpy(tmpbuf, polydd_ram + addr, 256);
		polydd_ctrl[0x03]++;	/* Move on 256 */
		/* Does the firmware wrap this - unknown */
	}
	/* Partial blocks needed */
	n = 0x0800 - addr;
	memcpy(tmpbuf, polydd_ram + addr, n);
	memcpy(tmpbuf + n, polydd_ram, 256 - n);
	polydd_ctrl[0x03]++;
}

static void polydd_xfer_out(uint8_t *tmpbuf)
{
	unsigned addr = polydd_ctrl[0x02] | (polydd_ctrl[0x03] << 8);
	unsigned n;
	addr &= 0x7FF;	/* 2K */
	if (addr <= 0x0700) {	/* Will 256 bytes fit ? */
		memcpy(polydd_ram + addr, tmpbuf, 256);
		polydd_ctrl[0x03]++;	/* Move on 256 */
		/* Does the firmware wrap this - unknown */
	}
	/* Partial blocks needed */
	n = 0x0800 - addr;
	memcpy(polydd_ram + addr, tmpbuf, n);
	memcpy(polydd_ram, tmpbuf + n, 256 - n);
	polydd_ctrl[0x03]++;
}

static void polydd_action(void)
{
	int drive;
	unsigned block;
	uint8_t tmpbuf[256];
	unsigned ct = polydd_ctrl[0x05];
	if (polydd_ctrl[0x07] != 0xFF)
		return;
	drive = polydd_ctrl[0x04] - 4;
	if (drive < 0 || drive > 3) {
		polydd_ctrl[0x07] = 0x01;
		return;
	}
	block = polydd_ctrl[0x00] | (polydd_ctrl[0x01] << 8);

	/* We've been told to do something */
	switch(polydd_ctrl[0x06]) {
	case 0:	/* Write */
		if (lseek(polydd_fd[drive], block * 256, 0) < 0)
			break;
		while(ct--) {
			polydd_xfer_out(tmpbuf);
			if (write(polydd_fd[drive], tmpbuf, 256) != 256)
				break;
		}
		polydd_ctrl[0x07] = 0x00;
		return;
	case 1:	/* Read */
		if (lseek(polydd_fd[drive], block * 256, 0) < 0)
			break;
		while(ct--) {
			if (read(polydd_fd[drive], tmpbuf, 256) != 256)
				break;
			polydd_xfer_in(tmpbuf);
		}
		polydd_ctrl[0x07] = 0x00;
		return;
	case 2:
		polydd_ctrl[0x07] = 0x00;	/* Initialize just works right */
		return;
	case 4:;
		/* "buffer command"  - unknown */
	}
	polydd_ctrl[0x07] = 0x01;	/* TODO: figure out error codes */
				/* Driver seems to juse use 0/NZ */
}

static void polydd_attach(unsigned unit, const char *path)
{
	if (path) {
		polydd_fd[unit] = open(path, O_RDWR);
		if (polydd_fd[unit] == -1) {
			perror(path);
			exit(1);
		}
	}
}

/*
 *	PRIAM hard disk controller
 *	0x38-0x3F
 *
 *	0x38	Status
 *	0x39	Data R/W
 *	0x3A	Status	R	Param 0 W
 *		0: data bus enable
 *		1: read/write 1 = input (only valid if drq)
 *		2: drq
 *		3: busy
 *		6: cont completion
 *		7: rejected
 *	0x3B	Result 0 R	Param 1 W
 *	0x3C	Result 1 R	Param 2 W
 *	0x3D	Result 2 R	Param 3 W
 *	0x3E	Result 3 R	Param 4 W
 *	0x3F	Result 4 R	Param 5 W  (presumably - not observed)
 *
 *	There is good documentation for this fortunately.
 *
 *	http://bitsavers.informatik.uni-stuttgart.de/pdf/priam/Peritek_PRM-Q_Programming_Jan81.pdf
 *	http://bitsavers.informatik.uni-stuttgart.de/pdf/priam/SMART_Interface_Product_Specification_Sep80.pdf
 *
 *	We only care about a tiny subset
 *
 *	command
 *	0x50 (write data, with retries)
 *	0x53 (read data, with retries)
 *
 *	For these
 *	param0: drive 0-3
 *	param1: head << 3 | cyl upper nybble
 *	param2: cyl low
 *	param3: sector
 *	param4: count
 *
 *	results: rr1 as param1
 *		 rr2: as param2
 *		 rr3: as param3
 *		 rr4: as param4
 *
 *	The actual PRIAM interface is extremely complex and powerful
 *	so we just fake the bits that are actually used for now.
 */


static uint8_t priam_reg[6];
static uint8_t priam_status = 0x01;
static uint8_t priam_tsr;
static uint8_t priam_cmd;
static uint8_t priam_data[256];
static uint8_t *priam_rxptr;
static uint8_t *priam_txptr;
static unsigned priam_rxct;
static unsigned priam_txct;
static int priam_fd[4];

static int priam_seek(void)
{
	unsigned c = ((priam_reg[1] & 0x0F) << 8) | priam_reg[2];
	unsigned h = priam_reg[1] >> 4;
	unsigned s = priam_reg[3];
	unsigned block = (c * 4 + h) * 16 + s;

	if (lseek(priam_fd[priam_reg[0] & 3], block * 256, 0) < 0) {
		perror("priam_seek");
		return -1;
	}
	return 0;
}

static void priam_data_out(uint8_t v)
{
	if (priam_txct) {
		*priam_txptr++ = v;
		priam_txct--;
		if (priam_txct == 0) {
			priam_status |= 0x40;
			priam_status &= ~0x0E;
			if (priam_cmd == 0x42 || priam_cmd == 0x52) {
				if (priam_seek() < 0)
					priam_tsr |= 0x12;
				else if (write(priam_fd[priam_reg[0]], priam_data, 256) != 256)
					priam_tsr |= 0x13;
				else {
					priam_reg[4]--;
					priam_reg[3]++;
					if (priam_reg[4]) {
						priam_txct = 256;
						priam_txptr = priam_data;
					} else {
						priam_status |= 0x40;
						priam_status &= ~0x0E;
					}
				}
			}
		}
	}
}

static uint8_t priam_data_in(void)
{
	if (priam_rxct) {
		uint8_t r = *priam_rxptr++;
		priam_rxct--;
		if (priam_rxct == 0) {
			switch(priam_cmd) {
			case 0x03:
				/* Read buffer */
				priam_status |= 0x40;
				priam_status &= ~0x0E;
				break;
			case 0x43:
			case 0x53:
				/* Disk read */
				priam_reg[4]--;
				priam_reg[3]++;
				priam_tsr = priam_reg[0] << 6;
				if (priam_reg[4]) {
					if (priam_seek() < 0)
						priam_tsr = 0x12;
					else if (read(priam_fd[priam_reg[0] & 3], priam_data, 256) != 256)
						priam_tsr |= 0x30;
					else {
						priam_rxct = 256;
						priam_rxptr = priam_data;
						break;
					}
				}
				priam_status |= 0x40;
				priam_status &= ~0x0E;
				break;
			}
		}
		return r;
	}
	return 0xFF;
}

static void priam_begin(uint8_t v)
{
	priam_cmd = v;
	priam_rxct = priam_txct = 0;
	priam_rxptr = priam_txptr = priam_data;
	priam_reg[0] &= 3;
	priam_tsr = priam_reg[0] << 6;


	if (priam_fd[priam_reg[0]] == -1 && v != 0x80) {
		priam_tsr |= 0x22;
		priam_status |= 0x40;
		priam_status &= ~0x0E;
		return;
	}

	switch(v) {
	case 0x00:
		/* Completion request */
		priam_status = 0x01;	/* Check */
		break;
	case 0x03:
		/* Read buffer */
		priam_rxct = 256;
		break;
	case 0x04:
		/* Write buffer */
		priam_txct = 256;
		break;
	case 0x80:
		/* Read drive status */
	case 0x82:
		/* Sequence up wait */
	case 0x83:
		/* Sequence up return */
		if (priam_fd[priam_reg[3]])
			priam_reg[1] = 0x03;
		else
			priam_reg[1] = 0x00;
		break;
	case 0x81:
		/* Sequence down (park) */
		break;
	case 0x85:
		/* Get parameters */
		priam_reg[2] = 0x10;	/* Sectors per track */
		priam_reg[1] = 0x42;	/* 4 heads 512 cylinders */
		priam_reg[0] = 0x00;	/* low of cylinders */
	case 0x40:
		/* Restore */
		priam_reg[1] = 0x00;
		priam_reg[2] = 0x00;
		break;
	case 0x41:
		/* Seek */
	case 0x51:
		/* Seek with retry */
		break;
	case 0x42:
		/* Write data */
	case 0x52:
		/* Write data with retry */
		priam_txptr = priam_data;
		priam_txct = 256;
		break;
	case 0x43:
		/* Read data */
	case 0x53:
		/* Read data with retry */
		if (priam_seek() < 0) {
			priam_tsr |= 0x12;	/* Seek fault */
			break;
		}
		if (read(priam_fd[priam_reg[0]], priam_data, 256) != 256) {
			priam_tsr |= 0x30;	/* Sector not found */
			break;
		}
		priam_rxptr = priam_data;
		priam_rxct = 256;
		break;
	case 0x45:
		/* Write ID */
	case 0x55:
		/* Write ID with retry */
	case 0x46:
		/* Read ID */
	case 0x56:
		/* Read ID with retry */
	case 0x47:
		/* Read ID immediate */
	case 0x57:
		/* Read ID immediate with retry */
	case 0x48:
		/* Verify ID */
	case 0x49:
		/* Read defect field */
	case 0x59:
		/* Read defect field with retry */
	case 0x4A:
		/* Write defect field */
	case 0x5A:
		/* Write defect field with retry */
	case 0xA0:
		/* Format disk */
	case 0xA1:
		/* Format cylinder */
	case 0xA2:
		/* Format track */
	case 0xA3:
		/* Verify disc */
	case 0xA4:
		/* Verify cylinder */
	case 0xA5:
		/* Verify track */
	case 0xA8:
		/* Format disk with defect maping */
	case 0xA9:
		/* Specify bad track */
	case 0xAA:
		/* Specify bad sector */
	default:
		/* Bad command */
		priam_status |= 0x80;	/* Rejected */
		priam_tsr |= 0x31;
		break;
	case 0x44:
		/* Verify */
		/* TODO: should leave buffer holding data in case a
		   read buffer is done */
		break;
	}
	/* Figure out status bits */
	if (priam_status & 0x80)
		return;
	if (priam_rxct || priam_txct)
		priam_status |= 0x0C;	/* DRQ, busy */
	if (priam_rxct)
		priam_status |= 0x02;	/* Direction */
	if (priam_rxct == 0 && priam_txct == 0) {
		/* Command finished as we don't emulate time yet */
		priam_status |= 0x40;
		/* Not busy, no DRQ */
		priam_status &= ~0x0E;
	}
}

static void priam_write(uint8_t addr, uint8_t v)
{
	/* 2-6 are param 0-4 */
	if (addr > 1)
		priam_reg[addr - 2] = v;
	else if (addr == 1)
		priam_data_out(v);
	else
		priam_begin(v);
}

static uint8_t priam_read(uint8_t addr)
{
	/* 3-6 are result 0-3 */
	if (addr > 2)
		return priam_reg[addr - 3];
	if (addr == 1)
		return priam_data_in();
	if (addr == 2)
		return priam_tsr;
	return priam_status;
}

static void priam_attach(unsigned unit, const char *path)
{
	if (path) {
		priam_fd[unit] = open(path, O_RDWR);
		if (priam_fd[unit] == -1) {
			perror(path);
			exit(1);
		}
	}
}


/*
 *	First guesses at the later disk controller
 *
 *	It seems to be sort of SASI but without much of the bus logic
 *	skipped. This should be sufficient code to start poking around
 *	with Exec and the HD driver to see what else is going on.
 *
 *	Currently the known bits are
 *
 *	Write to 0x37 with 0 causes a reset
 *	Read from 0x34 is the bus state
 *	Write of 1 to 0x34 appears to do a bus selection in hardware (
 *	or the device is using SASI commands but not bus selection)
 *
 *	Read/write via 0x35 seem to do data transfers and ack generation
 *	on write req/ack on read maybe
 *
 *	0x34:
 *	7:	Device is selected BSY ?
 *	6:	waited on for bus read - REQ ?
 *	5:	never checked except expected 0 post reset
 *	4:	never checked except expected 0 post reset
 *	3:	C/D
 *	2:	}
 *	1:	}	driver masks out
 *	0;	}
 */

static uint8_t sasi_bus;
static uint8_t sasi_cmd[6];
static uint8_t sasi_status[2];
static uint8_t sasi_data[256];
/* For now just hand back illegal request with no block address */
static uint8_t sasi_sense[4] = { 0x05, 0x00, 0x00, 0x00 };
static uint8_t *sasi_rptr;
static uint8_t *sasi_wptr;
static unsigned sasi_rxc;
static unsigned sasi_txc;
static int sasi_fd = -1;

/*
 *	A read of a block of data has finished. Right now that has
 *	to be
 */
static void sasi_read_done(void)
{
	/* Read command in progress ? */
	if (!(sasi_bus & 0x08) && sasi_cmd[0] == 0x08) {
		/* Count through blocks */
		if (sasi_cmd[4]) {
			sasi_cmd[4]--;
			if (read(sasi_fd, sasi_data, 256) == 256) {
				sasi_rptr = sasi_data;
				sasi_rxc = 256;
				return;
			}
			sasi_status[0] = 0x02;
			sasi_bus |= 0x08;
			/* TODO: sense data */
		} else {
			if (trace & TRACE_PRIAM)
				fprintf(stderr, "sasi: read completed go to status\n");
			sasi_status[0] = 0x00;
			sasi_status[1] = 0x00;
		}
		/* Status so set up for status read */
		sasi_bus |= 0x08;
		sasi_rptr = sasi_status;
		sasi_rxc = 2;
		return;
	}
	/* Sense completing ? */
	if (sasi_rptr >= sasi_sense && sasi_rptr <= sasi_sense + 4) {
		sasi_bus |= 0x08;
		sasi_rptr = sasi_status;
		sasi_status[0] = 0x00;
		sasi_status[1] = 0x00;
		sasi_rxc = 2;
		return;
	}
	/* Status so now go idle */
	sasi_bus = 0x00;
	/* Wait for command */
	sasi_wptr = sasi_cmd;
	sasi_txc = 6;
}

/* A SASI READ command was issued */
/* TODO: check disk size properly esp for write */
static void sasi_do_read(void)
{
	unsigned block;

	block = (sasi_cmd[1] & 0x0F) << 16;
	block |= sasi_cmd[2] << 8;
	block |= sasi_cmd[3];

	if (trace & TRACE_PRIAM)
		fprintf(stderr, "sasi: reading block %u\n", block);

	if (lseek(sasi_fd, block * 256, 0) == -1 ||
		read(sasi_fd, sasi_data, 256) != 256) {
		sasi_status[0] = 0x02;
		sasi_status[1] = 0x00;
		sasi_bus |= 0x08;
		sasi_rptr = sasi_status;
		sasi_rxc = 2;
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi read failed\n");
	} else {
		sasi_rptr = sasi_data;
		sasi_rxc = 256;
		sasi_bus &= ~0x08;
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi read begins (%u blocks)\n", sasi_cmd[4]);
		sasi_cmd[4]--;
	}
}

/* A disk write of a block has completed writing. Push it to disk and
   see what to do next */
static void sasi_write_block(void)
{
	/* Count through blocks */
	if (sasi_cmd[4]) {
		sasi_cmd[4]--;
		if (write(sasi_fd, sasi_data, 256) == 256) {
			sasi_wptr = sasi_data;
			sasi_txc = 256;
			return;
		}
		perror("sasi_write_block");
		sasi_status[0] = 0x02;
		/* TODO: sense data */
	} else {
		sasi_status[0] = 0x00;
		sasi_status[1] = 0x00;
	}
	/* Status so set up for status read */
	sasi_bus |= 0x08;
	sasi_rptr = sasi_status;
	sasi_rxc = 2;
}

/* Begin a SASI WRITE command: TODO check disk size */
static void sasi_write_begin(void)
{
	unsigned block;

	block = (sasi_cmd[1] & 0x0F) << 16;
	block |= sasi_cmd[2] << 8;
	block |= sasi_cmd[3];

	if (trace & TRACE_PRIAM)
		fprintf(stderr, "sasi: writing block %u\n", block);

	if (lseek(sasi_fd, block * 256, 0) == -1) {
		sasi_status[0] = 0x02;
		sasi_status[1] = 0x00;
		sasi_bus |= ~0x08;
		sasi_rptr = sasi_status;
		sasi_rxc = 2;
	} else {
		sasi_wptr = sasi_data;
		sasi_txc = 256;
		sasi_bus &= ~0x08;
	}
}

/* We've finished writing the expected block. That might be a command or
   SASI write data */
static void sasi_write_done(void)
{
	/* Six bytes written to the command buffer */
	if (sasi_wptr == sasi_cmd + 6) {
		/* Issue a command */
		sasi_status[0] = 0;
		sasi_status[1] = 0;
		sasi_bus |= 0x80;
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi_cmd: %02X %02X %02X %02X %02X %02X\n",
				sasi_cmd[0], sasi_cmd[1], sasi_cmd[2], sasi_cmd[3],
				sasi_cmd[4], sasi_cmd[5]);
		/* TODO: sense etc */
		switch(sasi_cmd[0]) {
		case 0x00: /* TUR */
		case 0x01: /* REZERO */
			break;
		case 0x03: /* REQUEST SENSE */
			sasi_rptr = sasi_sense;
			sasi_rxc = 4;
			return;
		case 0x08: /* READ */
			sasi_do_read();
			return;
		case 0x0A: /* WRITE */
			sasi_write_begin();
			return;
		case 0x04:	/* FORMAT UNIT */
		case 0x06:	/* FORMAT TRACK */
		case 0x0C:	/* REQUEST DRIVE TYPE */
		case 0xE0:	/* Diagnostics */
		case 0xE4:
			break;
		default:
			sasi_status[0] = 0x02;
			break;
		}
		sasi_rptr = sasi_status;
		sasi_rxc = 2;
		sasi_bus |= 0x08;
		return;
	}
	/* Data block */
	if (sasi_cmd[0]  == 0x0A && !(sasi_bus & 0x08)) {
		sasi_write_block();	/* Updates status etc itself */
		return;
	} else {
		/* Fudge up an error code - may need to implement sense */
		sasi_status[0] = 0x02;	/* Sense available */
		sasi_status[1] = 0x00;
	}
	sasi_rptr = sasi_status;
	sasi_rxc = 2;
	sasi_bus |= 0x08;
}

static uint8_t sasi_read(uint8_t addr)
{
	uint8_t r;

	if (sasi_fd == -1)
		return 0xFF;

	switch(addr) {
	case 0:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi: read bus state %02X\n", sasi_bus);
		return sasi_bus;
	case 1:
		if (sasi_rxc) {
			sasi_rxc--;
			r = *sasi_rptr++;
			if (trace & TRACE_PRIAM)
				fprintf(stderr, "sasi: read data %u\n", r);
			if (sasi_rxc == 0)
				sasi_read_done();
			return r;
		}
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi read data: no data\n");
		return 0xFF;	/* >?? */
	}
	return 0xFF;
}

static void sasi_write(uint8_t addr, uint8_t val)
{
	switch(addr) {
	case 0:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi: write bus %u\n", val);
		/* Write bus: only known case is write 1 if bus 0x80 */
		sasi_bus |= 0x40;	/* Until we figure this out */
		break;
	case 1:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi: write data %u (txc %u)\n", val, sasi_txc);
		/* Write data */
		if (sasi_txc) {
			sasi_txc--;
			*sasi_wptr++ = val;
			if (sasi_txc == 0)
				sasi_write_done();
		}
		break;
	case 3:
		if (trace & TRACE_PRIAM)
			fprintf(stderr, "sasi: write reset %u\n", val);
		/* Reset ? Write of 0 done initially */
		sasi_bus = 0x00;
		sasi_txc = 6;	/* Command block wait */
		sasi_wptr = sasi_cmd;
		sasi_rxc = 0;
		break;
	}
}

static void sasi_attach(const char *path)
{
	sasi_fd = open(path, O_RDWR);
	if (sasi_fd == -1) {
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
		return sasi_read(addr & 3);
	if (addr >= 0x38 && addr <= 0x3F)
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
		sasi_write(addr & 3, val);
	else if (addr >= 0x38 && addr <= 0x3F)
		priam_write(addr & 7, val);
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
	fprintf(stderr, "poly88: [-A|B|C|D disk] [-f] [-r path] [-m mem Kb] [-v vidbase] [-d debug] [-h hd] [-s sasi] [-i ide] [-p] [-t]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "poly88.rom";
	char *drive_a = NULL, *drive_b = NULL;
	char *drive_c = NULL, *drive_d = NULL;
	char *sasipath = NULL;
	char *hdpath = NULL;
	char *idepath = NULL;
	unsigned n;
	unsigned polyfdc = 0;
	unsigned tarbell = 0;

	while ((opt = getopt(argc, argv, "A:B:C:D:d:F:fh:i:m:pr:s:tTv:")) != -1) {
		switch (opt) {
		case 'A':
			drive_a = optarg;
			break;
		case 'B':
			drive_b = optarg;
			break;
		case 'C':
			drive_a = optarg;
			break;
		case 'D':
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
			hdpath = optarg;
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
		case 's':
			sasipath = optarg;
			break;
		case 'T':
			twinsys = 1;
			break;
		case 't':
			tarbell = 1;
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
		polyfd_attach(0, drive_a);
		polyfd_attach(1, drive_b);
		polydd_attach(0, drive_c);
		polydd_attach(1, drive_d);
	} else if (tarbell) {
		if (drive_a || drive_b) {
			fdc = tbfdc_create();
			if (trace & TRACE_FDC)
				wd17xx_trace(fdc, 1);
			if (drive_a)
				wd17xx_attach(fdc, 0, drive_a, 1, 80, 26, 128);
			if (drive_b)
				wd17xx_attach(fdc, 0, drive_b, 1, 80, 26, 128);
		}
	} else {
		polydd_attach(0, drive_a);
		polydd_attach(1, drive_b);
		polydd_attach(2, drive_c);
		polydd_attach(3, drive_d);
	}

	if (sasipath)
		sasi_attach(sasipath);

	priam_attach(0, hdpath);

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
			polydd_action();
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
