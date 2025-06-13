/*
 *	NASCOM emulator
 *
 *	Z80 CPU at 2MHz (NASCOM 1) or 4MHz (NASCOM 2)
 *	Keyboard 0x00
 *	6402 UART (emulated for serial I/O but not yet tape) 0x01/0x02
 *	Z80 PIO (not emulated) at 0x04
 *	Nascom floppy disk controller 0xE0
 *	Gemini RTC at 0x20
 *
 *	Various memory configurations
 *
 *	TODO:
 *	NMI circuit
 *	Add the NASCOM2 control, shift and lf/ch etc
 *	Native keymap mode
 *	Debug floppy emulation further
 *
 *	There were a bunch of related systems including the
 *	Gemini GM811 which used the same bus but were different
 *	systems. They are not covered here.
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
#include <sys/mman.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "event.h"
#include "keymatrix.h"

#include "nasfont.h"

#include "58174.h"
#include "ide.h"
#include "sasi.h"
#include "wd17xx.h"

#include "libz80/z80.h"
#include "z80dis.h"

#define CWIDTH 8
#define CHEIGHT 15

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;
static uint32_t texturebits[48 * CWIDTH * 16 * CHEIGHT];

struct keymatrix *matrix;

static uint8_t mapmem[32 * 65536];
static uint8_t base_mem[65536];
static uint64_t is_rom;
static uint64_t is_base;
static uint64_t is_present;
static unsigned int cpmmap;
static uint16_t vidbase = 0x0800;

static struct wd17xx *fdc;
static struct mm58174 *rtc;
static uint8_t kbd_row;
static struct sasi_bus *sasi;
static uint8_t sasi_en;
static struct ide_controller *ide;

static unsigned int nascom_ver = 1;
static unsigned int fdc_type;
static unsigned int has_gm802;
static unsigned int has_map80;
static unsigned int has_gm833;

static uint8_t map80 = 0x00;
static uint8_t map_gm802 = 0x11;

#define NASFDC		1
#define GM809		2
#define GM829		3
#define GM849		4
#define GM849A		5

static Z80Context cpu_z80;
static uint8_t fast;
volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_BANK	0x000010
#define TRACE_KEY	0x000020
#define TRACE_FDC	0x000040
#define TRACE_RTC	0x000080
#define TRACE_IDE	0x000100

static int trace = 0;

/* We handle page memory specially as writes are multi-bank and read
   collisions need detecting */
static uint8_t *mmu(uint16_t addr, bool write)
{
	uint64_t block = 1ULL << (addr / 1024);
	/* Base system - eg workspace - wins over cards */
	if (!(is_present & block))
		return NULL;
	if ((is_rom & block) && write)
		return NULL;
	if (is_base & block)
		return base_mem + addr;
	/* Banked memory options */
	if (has_map80) {
		/* Port 0xFE works as follows
			7: 0: 64K 1: 32K
			6: 0: fix lower 32K, 1: fix upper 32K
			5: unused (not that many cards!)
			4-0: select 64K page, and bank */
		if (map80 & 0x80) {
			/* 32K mode */
			if (map80 & 0x40) {
				if (addr >= 0x8000)
					return mapmem + addr;
			} else if (addr < 0x8000)
				return mapmem + addr;
			/* Is being mapped */
			return mapmem + (addr & 0x7FFF) + (map80 & 0x1F) * 0x8000;
		} else {
			return mapmem + addr + (map80 & 0x1E) * 0x8000;
		}
	}
	/* Gemini 80 page mode. Cards are switched by port 0xFF with a simple
	   read enable/write enable bit for each of four card positiosn (11
	   at boot setting card 0 R card 0 W. Multiple card writes are
	   permitted so we handle write specially below as well */
	if (has_gm802) {
		switch(map_gm802 & 0x0F) {
		case 0x01:
			return mapmem + addr;
		case 0x02:
			return mapmem + 0x10000 + addr;
		case 0x04:
			return mapmem + 0x20000 + addr;
		case 0x08:
			return mapmem + 0x30000 + addr;
		default:
			fprintf(stderr, "*** Invalid read state %02X\n", map_gm802);
			return NULL;
		}
	}
	return NULL;
}

uint8_t mem_read(int unused, uint16_t addr)
{
	uint8_t *p = mmu(addr, false);
	if (p == NULL) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X not readable\n", addr);
		return 0xFF;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "%04X -> %02X\n", addr, *p);
	return *p;
}

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t *p = mmu(addr, true);
	uint64_t block = 1ULL << (addr / 1024);

	/* Writes to multiple banks (low 48K is paged) */
	if (has_gm802 && !(block & is_base)) {
		if (map_gm802 & 0x10)
			mapmem[addr] = val;
		if (map_gm802 & 0x20)
			mapmem[addr + 0x10000] = val;
		if (map_gm802 & 0x40)
			mapmem[addr + 0x20000] = val;
		if (map_gm802 & 0x80)
			mapmem[addr + 0x30000] = val;
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X <- %02X\n", addr, val);
	} else if (p) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X <- %02X\n", addr, val);
		*p = val;
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "%04X ROM (write %02X fail)\n", addr, val);
	}
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t *p = mmu(addr, 0);
	if (p == NULL) {
		fprintf(stderr, "??");
		return 0xFF;
	}
	fprintf(stderr, "%02X ", *p);
	nbytes++;
	return *p;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	uint8_t *p = mmu(addr, 0);
	if (p == NULL)
		return 0xFF;
	return *p;
}

static void nascom_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z80.R1.br.A, cpu_z80.R1.br.F,
		cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL,
		cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}

unsigned int check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;

	FD_ZERO(&i);
	FD_SET(0, &i);
	FD_ZERO(&o);
	FD_SET(1, &o);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(2, &i, NULL, NULL, &tv) == -1) {
		if (errno == EINTR)
			return 0;
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

unsigned int next_char(void)
{
	char c;
	if (read(0, &c, 1) != 1) {
		printf("(tty read without ready byte)\n");
		return 0xFF;
	}
	if (c == 0x0A)
		c = '\r';
	return c;
}

/*
 *	Keyboard mapping (NASCOM2)
 */

static SDL_Keycode keyboard[] = {
	0, 0, 0, 0, SDLK_RSHIFT, 0, 0,
	SDLK_h, SDLK_b, SDLK_5, SDLK_f, SDLK_x, SDLK_t, SDLK_UP,
	SDLK_j, SDLK_n, SDLK_6, SDLK_d, SDLK_z, SDLK_y, SDLK_LESS,
	SDLK_k, SDLK_m, SDLK_7, SDLK_e, SDLK_s, SDLK_u, SDLK_DOWN,
	SDLK_l, SDLK_COMMA, SDLK_8, SDLK_w, SDLK_a, SDLK_i, SDLK_RIGHT,
	SDLK_SEMICOLON, SDLK_PERIOD, SDLK_9, SDLK_3, SDLK_q, SDLK_o, SDLK_GREATER,
	SDLK_COLON, SDLK_SLASH, SDLK_0, SDLK_2, SDLK_1, SDLK_p, SDLK_LEFTBRACKET,
	SDLK_g, SDLK_v, SDLK_4, SDLK_c, SDLK_SPACE, SDLK_r, SDLK_RIGHTBRACKET,
	SDLK_BACKSPACE, SDLK_RETURN, SDLK_MINUS, SDLK_LCTRL, SDLK_LSHIFT, SDLK_AT, 0/* FIXME */
};

static uint8_t pioide_data = 0xFF, pioide_status = 0xFF, pioide_ctrl = 0xFF;

#define IDE_ADDR	0x07
#define	IDE_CS0		0x08
#define	IDE_CS1		0x10	/* Not emulated at this point */
#define IDE_R		0x20
#define IDE_W		0x40
#define IDE_RESET	0x80

/* Very minimal model - we need to emulate and check direction and mode etc */
static void pioide_control_w(uint8_t v)
{
	uint8_t delta = pioide_ctrl ^ v;
	/* No chip select no action */
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide: ctrl %02X: ", v);
	if (delta & IDE_RESET) {
		if (!(v & IDE_RESET)) {
			if (trace & TRACE_IDE)
				fprintf(stderr, "reset ");
			ide_reset_begin(ide);
		}
	}
	if (!(v & IDE_RESET) || (v & IDE_CS0)) {
		if (trace & TRACE_IDE)
			fprintf(stderr, "\n");
		return;
	}
	if (delta & IDE_W) {
		if (v & IDE_W) { /* Rising edge - do write */
			if (trace & TRACE_IDE)
				fprintf(stderr, "write %d<-%02X ", v & IDE_ADDR, pioide_data);
			ide_write8(ide, v & IDE_ADDR, pioide_data);
		}
	}
	if (delta & IDE_R) {
		/* Falling edge capture read - not quite right but good enough */
		if (!(v & IDE_R)) {
			pioide_data = ide_read8(ide, v & IDE_ADDR);
			if (trace & TRACE_IDE)
				fprintf(stderr, "read %d->%02X ", v & IDE_ADDR, pioide_data);
		} else
			pioide_data = 0xFF;
	}
	pioide_ctrl = v;
	if (trace & TRACE_IDE)
		fprintf(stderr, "\n");
}

static void z80pio_write(uint8_t addr, uint8_t val)
{
	switch(addr & 3) {
	case 0:
		/* Data A */
		/* IDE control bits */
		if (ide)
			pioide_control_w(val);
		break;
	case 1:
		/* Data B */
		/* TODO: check direction etc */
		pioide_data = val;
		break;
	case 2:
		/* Control A */
		return;
	case 3:
		/* Control B */
		return;
	}
}

static uint8_t z80pio_read(uint8_t addr)
{
	switch(addr & 3) {
	case 0:
		/* Data A */
		return pioide_status;
	case 1:
		/* Data B */
		return pioide_data;
	case 2:
		/* Control A */
		return 0xFF;
	case 3:
		/* Control B */
		return 0xFF;
	}
	return 0xFF;
}

/* We have this wired to the consoie for teletype emulation but it was
   normally wired ot the tape interface which we don't yet cover */
static uint8_t uart_status(void)
{
	uint8_t reg = 0;
	unsigned int r = check_chario();
	if (r & 1)
		reg |= 0x80;
	if (r & 2)
		reg |= 0x40;
	return reg;
}

static uint8_t uart_data(void)
{
	return next_char();
}

/* No interrupts or other magic so for the moment just do this */
static void uart_transmit(uint8_t val)
{
	write(1, &val, 1);
}

/*
 *	 Gemini SASI/SCSI
 */

static void gemini_scsi_write(uint8_t addr, uint8_t val)
{
	if (fdc_type < GM829)
		return;
	if (!sasi)
		return;
	/* 1 = E5 = control */
	if (addr & 1) {
		uint8_t r = 0;
		if (val & 0x01) /* /ATN */
			;
		if (!(val & 0x02)) /* /SEL */
			r |= SASI_SEL;
		if (!(val & 0x04)) /* /RST */
			r |= SASI_RST;
		sasi_en = !!(val & 0x08); /* 0 for output 1 for other master */
		/* The 849 pulses SEL on write of 1, then 849A latches it */
		sasi_bus_control(sasi, r);
		if (fdc_type != GM849A)
			sasi_bus_control(sasi, 0);
	} else {
		/* This auto acks a pending req so no magic needed */
		if (fdc_type == GM829 || sasi_en)
			sasi_write_data(sasi, val);
	}
}

static uint8_t gemini_scsi_read(uint8_t addr)
{
	uint8_t r = 0;
	uint8_t st;
	if (fdc_type < GM829)
		return 0xFF;

	switch(addr) {
	case 0x05:
		/* Top bit is 1 for an 849, 0 for an 829 */
		if (fdc_type == GM829)
			r = 0xE0;
		if (sasi) {
			st = sasi_bus_state(sasi);
			if (!(st & SASI_REQ))
				r |= 1;
			if (st & SASI_IO)
				r |= 2;
			if (st & SASI_CD)
				r |= 4;
			if (!(st & SASI_MSG))
				r |= 8;
			if (!(st & SASI_BSY))
				r |= 16;
		}
		/* 6/5 not used
		   4 busy, 3 /msg, 2 c/d, 1 i/o 0 /req */
		return r;
	case 0x06:
		if (fdc_type == GM829 || sasi_en)
			return sasi_read_data(sasi);
		return 0xFF;		/* SCSI data r/w, generartes auto ACK */
	default:
		return 0xFF;
	}
}

/*
 *	Port offsets
 *	0 : FDC status / command
 *	1 : FDC track
 *	2 : FDC sector
 *	3 : FDC data
 *
 *	4 : Control latch / readback
 *	5 : Status on NREADY INTRQ
 *
 *	Gemini cards  have status readback on 4 instead and SASI/SCSI on 5/6
 *
 *	Non lucas is a WIP as we need to teach the wd17xx emulation things
 */

static uint8_t fdc_latch;
static unsigned int fdc_motor;

static void lucas_fdc_write(uint16_t addr, uint8_t val)
{
	switch(addr) {
	case 0x00:
		wd17xx_command(fdc, val);
		break;
	case 0x01:
		wd17xx_write_track(fdc, val);
		break;
	case 0x02:
		wd17xx_write_sector(fdc, val);
		break;
	case 0x03:
		wd17xx_write_data(fdc, val);
		break;
	case 0x04:
		fdc_latch = val;
		if (trace & TRACE_FDC)
			fprintf(stderr, "fdc: latch set to %x\n", fdc_latch);
		/* For our purposes the 849A is the same */
		if (fdc_type == GM849) {
			/* WD2793 */
			/* Fudge a bit - the 849 can handle 8 drives */
			if (val & 4)
				wd17xx_no_drive(fdc);
			else
				wd17xx_set_drive(fdc, val & 3);
			wd17xx_set_side(fdc, !!(val & 0x08));
			/* 0x10 is density which we ignore (MFM/FM)
			   0x20 is 5/8 inch - ie data rate (8" SD is 5" DD
			        8" DD is 5"/3.5" HD
			   0x40 is unused
			   0x80 is the speed control for 5.25 HD 1.2MB */
		} else if (fdc_type == GM829) {
			if (fdc_latch & 1)
				wd17xx_set_drive(fdc, 0);
			else if (fdc_latch & 2)
				wd17xx_set_drive(fdc, 1);
			else if (fdc_latch & 4)
				wd17xx_set_drive(fdc, 2);
			else if (fdc_latch & 8)
				wd17xx_set_drive(fdc, 3);
			else
				wd17xx_no_drive(fdc);
			/* 0x10 is density, 0x20 is 5.25/8" data rate */
			/* 0x40/80 not used */
			/* Side is absent as this card uses a WD1797 which
			   controls the side select itself */
		} else {
			/* WD1793 */
			/* Now figure out what it actually means */
			if (fdc_latch & 1)
				wd17xx_set_drive(fdc, 0);
			else if (fdc_latch & 2)
				wd17xx_set_drive(fdc, 1);
			else if (fdc_latch & 4)
				wd17xx_set_drive(fdc, 2);
			else if (fdc_latch & 8)
				wd17xx_set_drive(fdc, 3);
			else
				wd17xx_no_drive(fdc);
			wd17xx_set_side(fdc, !!(fdc_latch & 0x10));
			/* Turn on the motor for 10 seconds */
			fdc_motor = 1000;
			wd17xx_motor(fdc, 1);
			/* TODO: D6 is density select */
		}
		break;
	case 0x05:	/* No write on 0xE5 */
		gemini_scsi_write(addr, val);
		break;
	case 0x06:
		gemini_scsi_write(addr, val);
		break;
	}
}

static uint8_t lucas_fdc_read(uint16_t addr)
{
	uint8_t r = 0, rx = 0x00;
	switch(addr) {
	case 0x00:
		return wd17xx_status(fdc);
	case 0x01:
		return wd17xx_read_track(fdc);
	case 0x02:
		return wd17xx_read_sector(fdc);
	case 0x03:
		return wd17xx_read_data(fdc);
	case 0x04:
		if (fdc_type > NASFDC) {
			/* Gemini doesn't support the latch readback but puts
			   a status byte here much like E5 on the Lucas board */
			if (!fdc_motor)
				r |= 0x02;
			if (wd17xx_intrq(fdc))
				r |= 0x01;
			if (wd17xx_status_noclear(fdc) & 0x02)	/* DRQ */
				r |= 0x80;
			return r;
		} else {
			if (trace & TRACE_FDC)
				fprintf(stderr, "fdc: latch read as %x\n", fdc_latch & 0x5F);
			return fdc_latch & 0x5F;
		}
		break;
	case 0x05:
		/* This differs on the Gemini controllers */
		if (fdc_type >= GM809) {
			/* No SASI on the 809 */
			if (fdc_type == GM809)
				return 0xFF;
			return gemini_scsi_read(addr);
		}
		/* This input comes from IC8 and IC8. IC7 provides 00YX where
		   X is INTRQ from the 1793 and Y is a 10s timer, which we don't
		   emulate right now and inverted feeds the 1793 ready
		   IC8 provides the upper bits only bit 7 is used and corresponds
		   to the DRQ pin on the 1793. All of this magic is so that a single
		   in to this port tells you whether to grab a byte from the FDC
		   or write one fast.. that's to allow you to use 8" DD floppies at
		   2MHz */
		r = wd17xx_status_noclear(fdc);
		/* Fudge ready with NOTREADY signal */
		if (!fdc_motor)			/* NOTREADY */
			rx |= 0x02;		/* if ready set high */
		if (r & 0x02)			/* If DRQ set bit 7 */
			rx |= 0x80;
		if (wd17xx_intrq(fdc))		/* If INTRQ set bit 0 */
			rx |= 0x01;
		if (trace & TRACE_FDC)
			fprintf(stderr, "fdc: status read as %x\n", rx);
		return rx;
	case 0x06:
		return gemini_scsi_read(addr);
	}
	return 0xFF;
}

static uint8_t gm833_track;
static uint8_t gm833_sector;
static uint8_t gm833_byte;

/* Emulate a pair of 512K cards */
static uint8_t gm833_ram[32][256][128];

static void gm833_write(uint16_t addr, uint8_t val)
{
	switch(addr) {
	case 0xFB:
		gm833_track = val;
		/* Writing the track bits clears the byte counter, see circuit diagrsm */
		gm833_byte = 0;
		break;
	case 0xFC:
		gm833_sector = val;
		break;
	case 0xFD:
/*		fprintf(stderr, "gm833: w %d %d %d %02X\n", gm833_track, gm833_sector, gm833_byte, val); */
		if (gm833_track < 32)
			gm833_ram[gm833_track][gm833_sector][gm833_byte++] = val;
		gm833_byte &= 0x7F;
		break;
	}
}

static uint8_t gm833_read(uint16_t addr)
{
	uint8_t r = 0xFF;
	if (addr == 0xFD && gm833_track < 32) {
		r = gm833_ram[gm833_track][gm833_sector][gm833_byte++];
/*		fprintf(stderr, "gm833: r %d %d %d %02X\n", gm833_track, gm833_sector, gm833_byte - 1, r); */
		gm833_byte &= 0x7F;
	}
	return r;
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t port = addr & 0xFF;

	if (nascom_ver == 1)
		port &= 0x07;
	if (trace & TRACE_IO)
		fprintf(stderr, "=== OUT %02X, %02X\n", addr & 0xFF, val);
	/* NASCOM base ports */
	switch(port) {
		case 0x00:	/* Keyboard */
			if (val & 1)
				kbd_row = (kbd_row + 1) & 15;
			if (val & 2)
				kbd_row = 0;
//			if (val & 8)
//				prime_nmi();
			break;
		case 0x01:	/* UART TX */
			uart_transmit(val);
			break;
			/* Unused */
		case 0x02:
		case 0x03:
			break;
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			z80pio_write(addr & 3, val);
			break;
	}
	/* GM816 clock module */
	if (rtc && port >= 0x20 && port <= 0x2F)
		mm58174_write(rtc, port & 0x0F, val);
	/* Settable by jumpers but all software used 0xE0 */
	else if (fdc && port >= 0xE0 && port <= 0xE7)
		lucas_fdc_write(addr & 0x07, val);
	else if (has_gm833 && port >= 0xFB && port <= 0xFD)
		gm833_write(port, val);
	else if (has_map80 && port == 0xFE)
		map80 = val;
	/* Page mode */
	else if (port == 0xFF)
		map_gm802 = val;
}

static uint8_t do_io_read(int unused, uint16_t addr)
{
	uint8_t port = addr & 0xFF;

	if (nascom_ver == 1)
		port &= 0x07;
	switch(port) {
		case 0x00:	/* Keyboard */
			if (kbd_row > 8)
				return 0xFF;
			return ~keymatrix_input(matrix, 1 << kbd_row);
		case 0x01:	/* UART */
			return uart_data();
		case 0x02:	/* UART status */
			return uart_status();
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
			return z80pio_read(addr & 3);
	}
	/* GM816 clock module */
	if (rtc && port >= 0x20 && port <= 0x2F) {
		if ((port & 0x0F) == 0x0F)
			Z80NMI_Clear(&cpu_z80);
		return mm58174_read(rtc, port & 0x0F);
	}
	if (fdc && port >= 0xE0 && port <= 0xE7)
		return lucas_fdc_read(addr & 0x07);
	if (has_gm833 && port >= 0xFB && port <= 0xFD)
		return gm833_read(port);
	return 0xFF;
}

uint8_t io_read(int unused, uint16_t addr)
{
	uint8_t r = do_io_read(unused, addr);
	if (trace & TRACE_IO)
		fprintf(stderr, "=== IN %02X = %02X\n", addr & 0xFF, r);
	return r;
}

/*
 *	The nascom video is built from standard logic. It provides a
 *	composite 48x16 display. The memory map is strange in two ways
 *	firstly the top line is the last character line of memory and
 *	secondly like some other systems there is unused memory for the
 *	display margins.
 *
 *	Accessing video memory on a nascom 1 causes white sparkles due to
 *	the video losing the bus. On a nascom 2 they are suppressed
 */

static void raster_char(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp = &nascom_font_raw[16 * c];
	uint32_t *pixp;
	unsigned int rows, pixels;

	if (nascom_ver == 1)
		c &= 0x7F;

	pixp = texturebits + x * CWIDTH + 48 * CWIDTH * y * CHEIGHT;
	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp++;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x80)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 47 */
		pixp += 47 * CWIDTH;
	}
}

static void nascom_rasterize(void)
{
	unsigned int lptr = 0x03CA;
	unsigned int lines, cols;
	uint8_t *ptr;
	for (lines = 0; lines < 16; lines ++) {
		ptr = base_mem + vidbase + lptr;
		for (cols = 0; cols < 48; cols ++) {
			raster_char(lines, cols, *ptr++);
		}
		lptr += 0x40;
		lptr &= 0x03FF;
	}
}

static void nascom_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = 48 * CWIDTH;
	rect.h = 16 * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, 48 * CWIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

/* Most PC layouts don't have a colon key so use # */
static void keytranslate(SDL_Event *ev)
{
	SDL_Keycode c = ev->key.keysym.sym;
	switch(c) {
	case SDLK_HASH:
		c = SDLK_COLON;
		break;
	}
	ev->key.keysym.sym = c;
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	emulator_done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

struct diskgeom {
	const char *name;
	unsigned int size;
	unsigned int sides;
	unsigned int tracks;
	unsigned int spt;
	unsigned int secsize;
	unsigned int sidehack;
};

struct diskgeom disktypes[] = {
	{ "CP/M 77 track DSDD", 788480, 2, 77, 10, 512 , 1},
	{ "CP/M 77 track SSDD", 394240, 1, 77, 10, 512 , 0},
	{ "NAS-DOS DSDD", 655360, 2, 80, 16, 256, 0 },
	{ "NAS-DOS SSDD", 327680, 1, 80, 16, 256, 0 },
	{ "DOS DSDD40", 368640, 2, 40, 9, 512, 0 },
	{ "DOS DSDD80", 737280, 2, 80, 9, 512, 0 },
	{ "DOS-DSHD1.2", 1228800, 2, 80, 15, 512, 0 },
	{ "DOS-DSHD1.44", 1474560, 2, 80, 18, 512, 0 },
	{ NULL,}
};

static struct diskgeom *guess_format(const char *path)
{
	struct diskgeom *d = disktypes;
	struct stat s;
	off_t size;
	if (stat(path, &s) == -1) {
		perror(path);
		exit(1);
	}
	size = s.st_size;
	while(d->name) {
		if (d->size == size)
			return d;
		d++;
	}
	fprintf(stderr, "nascom: unknown disk format size %ld.\n", (long)size);
	exit(1);
}

/* Lots of nascom stuff is in this weird format of its own */

static void nasform(const char *buf, const char *path)
{
	fprintf(stderr, "%s: invalid format: %s", path, buf);
	exit(1);
}

static uint8_t spacehex(const char *p, const char *buf, const char *path)
{
	unsigned int r;
	if (!isspace(*p))
		nasform(buf, path);
	p++;
	if (sscanf(p, "%2x", &r) != 1)
		nasform(buf, path);
	return r;
}

/*
 *	Load a file in .NAS (NASCOM tape) format.
 */
static int nas_load(const char *path, uint8_t *mem, unsigned int base, unsigned int maxsize)
{
	char buf[128];
	FILE *f;
	unsigned int maxa = 0;

	f = fopen(path, "r");
	if (f == NULL) {
		perror(path);
		exit(1);
	}
	while(fgets(buf, 127, f) != NULL) {
		unsigned int i;
		unsigned int addr;
		uint8_t *mp;
		char *p = buf + 4;
		if (*buf == '.')
			break;
		if (sscanf(buf, "%4x", &addr) != 1)
			nasform(buf, path);
		if (addr < base || addr + 8 > base + maxsize) {
			fprintf(stderr, "%s: %04x-%04x is out of range.\n", path, addr, addr + 7);
			exit(1);
		}
		if (addr > maxa)
			maxa = addr;
		mp = mem + addr - base;
		for (i = 0; i < 8; i++) {
			*mp++ = spacehex(p, buf, path);
			p += 3;
		}
	}
	fclose(f);
	return maxa - base + 8;
}

static int romload(const char *path, uint8_t *mem, unsigned int base, unsigned int maxsize)
{
	int fd;
	int size;
	const char *p = strrchr(path, '.');
	if (p && strcmp(p, ".nas") == 0)
		return nas_load(path, mem, base, maxsize);
	if (p && strcmp(p, ".nal") == 0)
		return nas_load(path, mem, base, maxsize);
	fd = open(path, O_RDONLY);
	if (fd == -1) {
		perror(path);
		exit(1);
	}
	size = read(fd, mem, maxsize);
	close(fd);
	return size;
}

static void usage(void)
{
	fprintf(stderr, "nascom: [-f] [-1] [-2] [-3] [-8] [-A|B|C|D disk] [-b basic] [-c] [-e eprom] [-i idepath] [-g] [-r rom] [-m] [-R] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	static int tstates = 200;	/* 2MHz */
	int opt;
	char *rom_path = "nassys3.nal";
	char *basic_path = NULL;
	char *eprom_path = NULL;
	char *ide_path = NULL;
	char *sasi_path = NULL;
	char *fdc_path[4] = { NULL, NULL, NULL, NULL };
	int romsize;
	unsigned int hasrtc = 0;
	unsigned int maxmem = 0;
	static unsigned int need_fdc = 0;

	while ((opt = getopt(argc, argv, "1238b:cd:e:fgi:mr:A:B:C:D:RMS:")) != -1) {
		switch (opt) {
		case '1':
			nascom_ver = 1;
			break;
		case '2':
		case '3':
			nascom_ver = 2;
			break;
		case '8':
			has_map80 = 1;
			has_gm802 = 0;
			break;
		case 'A':
			fdc_path[0] = optarg;
			need_fdc = 1;
			break;
		case 'B':
			fdc_path[1] = optarg;
			need_fdc = 1;
			break;
		case 'C':
			fdc_path[2] = optarg;
			need_fdc = 1;
			break;
		case 'D':
			fdc_path[3] = optarg;
			need_fdc = 1;
			break;
		case 'b':
			basic_path = optarg;
			break;
		case 'c':
			cpmmap = 1;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'e':
			eprom_path = optarg;
			break;
		case 'f':
			fast = 1;
			break;
		case 'g':
			has_map80 = 0;
			has_gm802 = 1;
			break;
		case 'i':
			ide_path = optarg;
			break;
		case 'r':
			rom_path = optarg;
			break;
		case 'm':
			maxmem = 1;
			break;
		case 'R':
			hasrtc = 1;
			break;
		case 'M':
			has_gm833 = 1;
			break;
		case 'S':
			sasi_path = optarg;
			break;
		default:
			usage();
		}
	}
	/* Let the user load stuff off the command line */
	/* Will need reworking to load into paged memory one day */
	while (optind < argc)
		romload(argv[optind++], base_mem, 0, 0xFFFF);

	if (cpmmap) {
		vidbase = 0xF800;
		if (romload(rom_path, base_mem + 0xF000, 0xF000, 0x800) != 0x800) {
			fprintf(stderr, "nascom: invalid bootstrap ROM size\n");
			exit(1);
		}
		if (has_gm802 == 0 && has_map80 == 0) {
			/* Assume a full 64K card is present */
			is_present = 0xFFFFFFFFFFFFFFFFULL;
			is_base = 0xFFFFFFFFFFFFFFFFULL;
		}
		/* Only the F000-F7FF space is ROM */
		is_rom = 0x3ULL << 60;
		/* Workspace and video is at F800-FFFF */
		is_base |= 0xFULL << 60;
		is_present |= is_rom | is_base;
		nascom_ver = 2;
		need_fdc = 1;
	} else {
		/* Start with the nascom 1 setup */
		/* NASBUG low 1K, optional firmware 2nd 1K */
		is_rom = 0x03;
		/* Video RAM 3rd 1K, user 4th 1K */
		/* These are implemented as 4 banks of 8 x 1K SRAM so video contention
		   and noise is specific to 800-BFF */
		is_present = 0x0F;
		is_base = 0x0F;

		romsize = romload(rom_path, base_mem, 0x0000, 2048);
		if (romsize != 1024 && romsize != 2048) {
			fprintf(stderr, "nascom: invalid ROM size '%s'.\n", rom_path);
			exit(EXIT_FAILURE);
		}
		if (basic_path) {
			if (romload(basic_path, base_mem + 0xE000, 0xE000, 0x2000)) {
				is_present |= 0xFFULL << 56;
				is_base |= 0xFFULL << 56;
				is_rom |= 0xFFULL << 56;
			}
		}
		if (eprom_path) {
			if (romload(eprom_path, base_mem + 0xD000, 0xD000, 0x1000)) {
				is_present |= 0xFULL << 52;
				is_base |= 0xFULL << 52;
				is_rom |= 0xFULL << 52;
			}
		}
		if (nascom_ver == 2 || maxmem) {
			/* Plug in the RAM */
			is_present |= 0xFF << 4;
			is_base |= 0xFF << 4;
		}
	}
	/* TODO: Strictly speaking it's a switch */
	if (nascom_ver == 2)
		tstates = 400;

	/* GM802 banked memory wherever there is no existing base memory */
	if (has_gm802)
		is_present |= 0xFFFFFFFFFFFFFFFFULL;
	/* Map80 likewise */
	if (has_map80)
		is_present |= 0xFFFFFFFFFFFFFFFFULL;
	/* Maxmem option - all memory all base */
	if (maxmem) {
		is_present |= 0xFFFFFFFFFFFFULL << 8;
		is_base |= 0xFFFFFFFFFFFFULL << 8;
	}

	if (need_fdc) {
		unsigned i;
		/* For the moment */
		fdc_type = NASFDC;
		switch(fdc_type) {
		case NASFDC:
			fdc = wd17xx_create(1791);
			break;
		case GM809:
		case GM829:
			fdc = wd17xx_create(1797);
			break;
		case GM849:
		case GM849A:
			fdc = wd17xx_create(2793);
			break;
		}
		for (i = 0; i < 4; i++) {
			if (fdc_path[i]) {
				struct diskgeom *d = guess_format(fdc_path[i]);
				printf("[Drive %c, %s.]\n", 'A' + i, d->name);
				if (d->sidehack)
					wd17xx_set_side1(fdc, i, 77);
				wd17xx_attach(fdc, i, fdc_path[i], d->sides, d->tracks, d->spt, d->secsize);
			} else {
				if (cpmmap)
					wd17xx_set_side1(fdc, i, 77);
			}
			wd17xx_set_sector0(fdc, i, 1);
		}
		wd17xx_trace(fdc, trace & TRACE_FDC);
	}
	/* GM816 RTC emulation */
	if (hasrtc) {
		rtc = mm58174_create();
		mm58174_trace(rtc, trace & TRACE_RTC);
	}
	if (ide_path) {
		int ide_fd;
		ide = ide_allocate("pio0");
		ide_fd = open(ide_path, O_RDWR);
		if (ide_fd == -1) {
			perror(ide_path);
			exit(1);
		}
		ide_attach(ide, 0, ide_fd);
		ide_reset_begin(ide);
	}
	if (sasi_path) {
		/* For now - needs to be properly settable */
		if(fdc_type < GM829)
			fdc_type = GM849A;
		sasi = sasi_bus_create();
		sasi_disk_attach(sasi, 0, sasi_path, 512);
		sasi_bus_reset(sasi);
	}

	ui_init();

	matrix = keymatrix_create(9, 7, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_add_events(matrix);
	keymatrix_translator(matrix, keytranslate);

	window = SDL_CreateWindow("Nascom",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			48 * CWIDTH, 16 * CHEIGHT,
			SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "nascom: unable to open window: %s\n",
			SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "nascom: unable to create renderer: %s\n",
			SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		48 * CWIDTH, 16 * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr, "nascom: unable to create texture: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, 48 * CWIDTH,  16 * CHEIGHT);

	/* 10ms - it's a balance between nice behaviour and simulation
	   smoothness */

	tc.tv_sec = 0;
	tc.tv_nsec = 10000000L;

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

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;
	cpu_z80.trace = nascom_trace;

	/* Emulate the jump logic */
	if (cpmmap)
		cpu_z80.PC = 0xF000;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int i;
		/* Each cycle we do 20000 or 40000 T states */
		for (i = 0; i < 100; i++) {
			Z80ExecuteTStates(&cpu_z80, tstates);
		}

		/* We want to run UI events before we rasterize */
		if (ui_event())
			emulator_done = 1;
		nascom_rasterize();
		nascom_render();
		if (fdc_motor) {
			fdc_motor--;
			if (fdc_motor == 0 && (trace & TRACE_FDC))
				fprintf(stderr, "fdc: motor timeout.\n");
		}
		if (rtc) {
			/* Annoyingly the 58174 on the GM816 is wired to NMI */
			mm58174_tick(rtc);
			if (mm58174_irqpending(rtc))
				Z80NMI(&cpu_z80);
		}
		/* Do 10ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (fdc)
			wd17xx_tick(fdc, 10);
	}
	exit(0);
}
