/*
 *	Lobo Max 80 - initial sketches
 *
 *	TODO
 *	- Video emulation (finish 64 column etc, add the 40 col x 16 mode)
 *	- Timing (60.1Hz tick, 5MHz CPU, wait stated video, slow down on
 *	  I/O range)
 *	- UVC
 *	BUGS
 *	- XHARD fails to reboot the system when it errors
 *	- Changes to WD177x to make tarbell work broke Lobo Max CP/M
 *	  needs investigating.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "libz80/z80.h"
#include "z80dis.h"
#include "sasi.h"
#include "wd17xx.h"

#include <SDL2/SDL.h>
#include "event.h"
#include "serialdevice.h"
#include "z80sio.h"
#include "ttycon.h"
#include "vtcon.h"
#include "keymatrix.h"

static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *texture;

#define CWIDTH	8
#define CHEIGHT	16
#define COLS	80
#define ROWS	25

static uint32_t texturebits[ROWS * COLS * CWIDTH * CHEIGHT];

static uint8_t mlatch;
static uint8_t vlatch;
static uint8_t sysstat;
static uint8_t dipswitches = 1;
static uint8_t bauda;
static uint8_t baudb;

static uint8_t pio_a;
static uint8_t pio_b;

static uint8_t ram[131072];
static uint8_t bootrom[512];
static uint8_t video[2048];
static uint8_t shortchar[1024];
static uint8_t tallchar[1024];

static uint8_t crt_reg[18];
static uint8_t crt_rptr;

static struct keymatrix *matrix;
static struct sasi_bus *sasi;
static struct wd17xx *fdc;
static struct z80_sio *sio;
static Z80Context cpu_z80;

static volatile int emulator_done;
static unsigned fast;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_FDC	4
#define TRACE_SIO	8
#define TRACE_IRQ	16
#define TRACE_CPU	32
#define TRACE_KEY	64
#define TRACE_BANK	128
#define TRACE_CRT	256

static int trace = 0;

static void reti_event(void);

static void cpu_slow(void)
{
	/* TODO */
}

static uint8_t scan_keyboard(uint16_t addr)
{
	return keymatrix_input(matrix, addr);
}

uint8_t maxfdc_read(uint8_t addr)
{
	switch (addr & 3) {
	case 0:
		return wd17xx_status(fdc);
	case 1:
		return wd17xx_read_track(fdc);
	case 2:
		return wd17xx_read_sector(fdc);
	case 3:
		return wd17xx_read_data(fdc);
	}
	return 0xFF;		/* Can't reach */
}

static void maxfdc_write(uint8_t addr, uint8_t val)
{
	switch (addr & 3) {
	case 0:
		wd17xx_command(fdc, val);
		break;
	case 1:
		wd17xx_write_track(fdc, val);
		break;
	case 2:
		wd17xx_write_sector(fdc, val);
		break;
	case 3:
		wd17xx_write_data(fdc, val);
		break;
	}
}

static void maxfdc_mlatch(uint8_t latch)
{
	/* The CPM code claims 0x80 is an NMI enable but it's never used so who
	   knows. It's not in the technical manual */
	/* Low 4 bits drive sel lines */
	switch (latch & 0x0F) {
	case 0:
		wd17xx_no_drive(fdc);
		return;
	case 1:
		wd17xx_set_drive(fdc, 0);
		break;
	case 2:
		wd17xx_set_drive(fdc, 1);
		break;
	case 4:
		wd17xx_set_drive(fdc, 2);
		break;
	case 8:
		wd17xx_set_drive(fdc, 3);
		break;
	default:
		fprintf(stderr, "fdc: invalid drive select %x\n", latch & 0x0F);
	}
	wd17xx_set_density(fdc, (latch & 0x40) ? DEN_DD : DEN_SD);
	wd17xx_set_side(fdc, !!(latch & 0x10));

	/* 0x20 is 8" v 5" but also starts motor for 3 secs */
	if (!(latch & 0x20))
		wd17xx_motor(fdc, 1);
	if (trace & TRACE_FDC)
		fprintf(stderr, "fdc: mlatch %02X\n", mlatch);
}

/* State of data out and control lines */
static uint8_t sasi_data;
static uint8_t sasi_ctrl;

static uint8_t max_sasi_status(void)
{
	uint8_t r = 0;
	unsigned s = sasi_bus_state(sasi);
	if (!(wd17xx_status_noclear(fdc) & 2))	/* DRQ is also routed here */
		r |= 0x80;
	if (!wd17xx_intrq(fdc))
		r |= 0x40;
	if (s & SASI_CD)
		r |= 0x10;
	if (s & SASI_IO)
		r |= 0x08;
	if (s & SASI_BSY)
		r |= 0x04;
	/* Bit 1 : TODO not clear how this works */
	/* Looks like this is ATN and only used by UVC as an "ERR" line */
	if (s & SASI_REQ)
		r |= 0x01;
	/* Hand cranked ACK/REQ emulatiomn. The underlying SASI emulation library
	   assumes controller cranked */
	if (sasi_ctrl & 1)	/* ACK set */
		r &= ~0x01;	/* Hide REQ */
	return r;
}

static uint8_t max_sasi_data(uint8_t addr)
{
	uint8_t r;

	if (addr & 1)		/* Read and ack */
		r = sasi_read_data(sasi);
	else			/* Read only */
		r = sasi_read_bus(sasi);
	return r;
}

/* How do C/D and I/O get set TODO */
static void max_sasi_ctrl(uint8_t val)
{
	uint8_t bus = 0;
	if (val & 0x04)
		bus |= SASI_RST;
	if (val & 0x02)
		bus |= SASI_SEL;
	if (val & 0x01)
		bus |= SASI_ACK;
	sasi_bus_control(sasi, bus);
	if ((val ^ sasi_ctrl) & 1 && (val & 1)) {	/* Hand crank REQ/ACK */
		/* If we are writing then we just hand cranked a byte out */
		if (!(sasi_bus_state(sasi) & SASI_IO))
			sasi_write_data(sasi, sasi_data);
		else		/* We hand cranked an ACK */
			sasi_ack_bus(sasi);
	}
	sasi_ctrl = val;
}

static void max_sasi_writedata(uint8_t addr, uint8_t val)
{
	if (addr & 1)
		sasi_write_data(sasi, val);
	else
		sasi_set_data(sasi, val);
	sasi_data = val;
}

static void crt_data(uint8_t val)
{
	if (crt_rptr < sizeof(crt_reg)) {
		if (trace & TRACE_CRT)
			fprintf(stderr, "crt R%d <-%d\n", crt_rptr, val);
		crt_reg[crt_rptr] = val;
	}
}

static void crt_set_reg(uint8_t val)
{
	crt_rptr = val;
}

static void video_recalc(void)
{
}

static void lpt_byte(uint8_t val)
{
}

/* Clock chip */
static uint8_t rtc_read(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	if ((pio_b & 0x20) == 0)
		return 0xFF;
	switch (pio_b & 0x0F) {
	case 0:
		return tm->tm_sec % 10;
	case 1:
		return tm->tm_sec / 10;
	case 2:
		return tm->tm_min % 10;
	case 3:
		return tm->tm_min / 10;
	case 4:
		return tm->tm_hour % 12 % 10;
	case 5:
		/* AM/PM mode */
		return (tm->tm_hour % 12) / 10 + tm->tm_hour >= 12 ? 4 : 0;
	case 6:
		return tm->tm_wday;
	case 7:
		return tm->tm_mday % 10;
	case 8:
		return tm->tm_mday / 10;
	case 9:
		return tm->tm_mon % 10;
	case 10:
		return tm->tm_mon / 10;
	case 11:
		return tm->tm_year % 10;
	case 12:
		return (tm->tm_year / 10) % 10;
	}
	return 0x0F;
}

/* PIO */
struct z80_pio {
	uint8_t data[2];
	uint8_t mask[2];
	uint8_t mode[2];
	uint8_t intmask[2];
	uint8_t icw[2];
	uint8_t mpend[2];
	uint8_t irq[2];
	uint8_t vector[2];
	uint8_t in[2];
};

static struct z80_pio pio[1];

/* Bus emulation helpers */

static const char *bank_name(unsigned b)
{
	static const char *bname[4] = {
		"Chunk 1 Bank A",
		"Chunk 2 Bank A",
		"Chunk 1 Bank B",
		"Chunk 2 Bank B"
	};
	b &= 3;
	return bname[b];
}

void pio_data_write(struct z80_pio *pio, uint8_t port, uint8_t val)
{
	if (port == 0) {
		if (pio_a != val && (trace & TRACE_BANK))
			fprintf(stderr, "Bank set : Low 32K: %s High 32K %s\n", bank_name(val >> 4), bank_name(val >> 6));
		pio_a = val;
	}
	if (port == 1)
		pio_b = val;
}

void pio_strobe(struct z80_pio *pio, uint8_t port)
{
}

uint8_t pio_data_read(struct z80_pio *pio, uint8_t port)
{
	if (port == 0)
		pio->in[0] = rtc_read();
	return pio->in[port];
}

static void pio_recalc(void)
{
	/* For now we don't model interrupts at all */
}

/* Simple Z80 PIO model. We don't yet deal with the fancy bidirectional mode
   or the strobes in mode 0-2. We don't do interrupt mask triggers on mode 3 */

/* TODO: interrupts, strobes */

static void pio_write(uint8_t addr, uint8_t val)
{
	uint8_t pio_port = (addr & 2) >> 1;
	uint8_t pio_ctrl = addr & 1;

	if (pio_ctrl) {
		if (pio->icw[pio_port] & 1) {
			pio->intmask[pio_port] = val;
			pio->icw[pio_port] &= ~1;
			pio_recalc();
			return;
		}
		if (pio->mpend[pio_port]) {
			pio->mask[pio_port] = val;
			pio_recalc();
			pio->mpend[pio_port] = 0;
			return;
		}
		if (!(val & 1)) {
			pio->vector[pio_port] = val;
			return;
		}
		if ((val & 0x0F) == 0x0F) {
			pio->mode[pio_port] = val >> 6;
			if (pio->mode[pio_port] == 3)
				pio->mpend[pio_port] = 1;
			pio_recalc();
			return;
		}
		if ((val & 0x0F) == 0x07) {
			pio->icw[pio_port] = val >> 4;
			return;
		}
		return;
	} else {
		pio->data[pio_port] = val;
		switch (pio->mode[pio_port]) {
		case 0:
		case 2:	/* Not really emulated */
			pio_data_write(pio, pio_port, val);
			pio_strobe(pio, pio_port);
			break;
		case 1:
			break;
		case 3:
			/* Force input lines to floating high */
			val |= pio->mask[pio_port];
			pio_data_write(pio, pio_port, val);
			break;
		}
	}
}

static uint8_t pio_read(uint8_t addr)
{
	uint8_t pio_port = (addr & 2) >> 1;
	uint8_t val;
	uint8_t rx;

	/* Output lines */
	val = pio->data[pio_port];
	rx = pio_data_read(pio, pio_port);

	switch (pio->mode[pio_port]) {
	case 0:
		/* Write only */
		break;
	case 1:
		/* Read only */
		val = rx;
		break;
	case 2:
		/* Bidirectional (not really emulated) */
	case 3:
		/* Control mode */
		val &= ~pio->mask[pio_port];
		val |= rx & pio->mask[pio_port];
		break;
	}
	return val;
}

static void pio_reset(void)
{
	/* Input mode */
	pio->mask[0] = 0xFF;
	pio->mask[1] = 0xFF;
	/* Mode 1 */
	pio->mode[0] = 1;
	pio->mode[1] = 1;
	/* No output data value */
	pio->data[0] = 0;
	pio->data[1] = 0;
	/* Nothing pending */
	pio->mpend[0] = 0;
	pio->mpend[1] = 0;
	/* Clear icw */
	pio->icw[0] = 0;
	pio->icw[1] = 0;
	/* No interrupt */
	pio->irq[0] = 0;
	pio->irq[1] = 0;
}

static uint8_t *ram_addr(uint8_t bits)
{
	uint8_t *v;
	bits &= 3;
	v = ram + 0x8000 * bits;
	return v;
}

/* Read through from Bank A */

static uint8_t read_banka(uint16_t addr)
{
#if 1
	return ram[addr & 0x7FFF];
#else
	uint8_t r;
	if (addr & 0x8000)
		r = ram_addr((pio_a >> 6) & 1)[addr & 0x7FFF];
	else
		r = ram_addr((pio_a >> 4) & 1)[addr & 0x7FFF];
	return r;
#endif
}

static unsigned sio_port[4] = {
	SIOA_D,
	SIOA_C,
	SIOB_D,
	SIOB_C
};

/* Read from the movable block. debug is 1 if debug trace is reading
   so we don't cause side effects */

static void poll_irq_event(void);

static uint8_t mb_read(uint16_t addr, unsigned debug)
{
	uint8_t r;
	uint16_t paddr = addr;	/* Full physical address */
	addr &= 0x0FFF;

	if (addr & 0x0700)
		cpu_slow();

	if (addr < 0x400) {
		/* SWAP1K */
		switch (vlatch & 7) {
		case 0:
			return bootrom[addr & 0x1FF];
		case 1:
			return read_banka(paddr);
		case 2:
		case 3:
			return video[addr + 0x0400];
		case 4:
		case 5:
			return shortchar[addr];
		default:
			return tallchar[addr];
		}
	}
	if (addr < 0x0700) {
		/* RAM400 */
		return read_banka(paddr);
	}
	if (addr <= 0x07CF) {
		/* SLORAM */
		return read_banka(paddr);
	}
	if (addr >= 0xC00)	/* VIDEO */
		return video[addr & 0x03FF];
	if (addr >= 0x0900)	/* RAM900 */
		return read_banka(paddr);

	/* Below this point is I/O with potential side effects */
	if (debug)
		return 0xFF;

	if (addr >= 0x0800)	/* MATRIX */
		return scan_keyboard(addr);
	/* Misc I/O */
	switch (addr & ~3) {
	case 0x07D0:		/* BAUDA */
	case 0x07D4:		/* BAUDB */
	case 0x07D8:		/* MLATCH */
	case 0x07DC:		/* VLATCH */
		return read_banka(paddr);
	case 0x07E0:		/* SYSCLR and SYSFLG CRTREG/CRTBYT */
		r = sysstat;
		r &= ~0x20;
		if (wd17xx_get_motor(fdc))
			r |= 0x20;
		sysstat ^= 0x40;	/* FIXME: hack to make DSPTMG toggle */
		if (!(addr & 1)) {
			sysstat &= 0x7F;	/* Clear heartbeat */
			poll_irq_event();
		}
		return r;
	case 0x07E4:		/* SIO (DA CA DB CB) */
		return sio_read(sio, sio_port[addr & 3]);
	case 0x07E8:		/* PSTAT/PDATA */
		return 0x10;	/* For now just say "printer ready" */
	case 0x07EC:		/* FDC (inverted) */
		return ~maxfdc_read(addr & 3);
	case 0x07F0:		/* SASI/UVC DATA */
		return max_sasi_data(addr & 1);
	case 0x07F4:		/* SASI/UVC CTRL */
		return max_sasi_status();
	case 0x07F8:		/* DIPSW/BEEP */
		return dipswitches;
	case 0x07FC:		/* PIO (DA CA DB CB) */
		return pio_read(addr & 3);	/* Check order */
	}
	fprintf(stderr, "Unhandled MB read %x\n", addr);
	return 0xFF;
}

/* Write to the movable block */

static unsigned mb_write(uint16_t addr, uint8_t val)
{
	addr &= 0x0FFF;

	if (addr & 0x0700)
		cpu_slow();

	if (addr < 0x400) {
		/* SWAP1K */
		switch (vlatch & 7) {
		case 0:
			return 0;
		case 1:
			return 1;
		case 2:
		case 3:
			video[addr + 0x0400] = val;
			return 0;
		case 4:
		case 5:
			shortchar[addr] = val;
			return 0;
		default:
			tallchar[addr] = val;
			return 0;
		}
	}
	if (addr < 0x0700) {
		/* RAM400 */
		return 1;
	}
	if (addr <= 0x07CF) {
		/* SLORAM */
		return 1;
	}
	if (addr >= 0xC00) {	/* VIDEO */
		video[addr & 0x3FF] = val;
		return 0;
	}
	if (addr >= 0x0900) {	/* RAM900 */
		return 1;
	}
	if (addr >= 0x0800) {	/* MATRIX */
		/* Undocumented. CP/M BIOS reads to this and it seems to work */
		return 0;
	}
	/* Misc I/O */
	switch (addr & ~3) {
	case 0x07D0:		/* BAUDA */
		bauda = val;
		return 1;
	case 0x07D4:		/* BAUDB */
		baudb = val;
		return 1;
	case 0x07D8:		/* MLATCH */
		mlatch = val;
		maxfdc_mlatch(val);
		return 1;
	case 0x07DC:		/* VLATCH */
		vlatch = val;
		if (trace & TRACE_BANK)
			fprintf(stderr, "vlatch now %02x\n", vlatch);
		video_recalc();
		return 1;
	case 0x07E0:		/* SYSCLR and SYSFLG CRTREG/CRTBYT */
		if (addr & 1)
			crt_data(val);
		else
			crt_set_reg(val);
		return 0;
	case 0x07E4:		/* SIO (DA CA DB CB) */
		sio_write(sio, sio_port[addr & 3], val);
		return 0;
	case 0x07E8:		/* PSTAT/PDATA */
		lpt_byte(val);
		return 0;
	case 0x07EC:		/* FDC (inverte) */
		maxfdc_write(addr & 3, ~val);
		return 0;
	case 0x07F0:		/* SASI/UVC DATA */
		max_sasi_writedata(addr & 1, val);
		return 0;
	case 0x07F4:		/* SASI/UVC CTRL */
		max_sasi_ctrl(val);
		return 0;
	case 0x07F8:		/* DIPSW/BEEP */
		return 0;	/* Makes a 2KHz beep for 1/15th sec */
	case 0x07FC:		/* PIO (DA CA DB CB) */
		pio_write(addr & 3, val);	/* Check order */
		return 0;
	}
	fprintf(stderr, "Unhandled MB write %x, %x\n", addr, val);
	return 0;
}

static uint8_t do_max_read(uint16_t addr, unsigned debug)
{
	uint8_t r;
	if (trace & TRACE_MEM)
		fprintf(stderr, "addr %04X, vlatch %02X\n", addr, vlatch);
	if (((addr >> 8) & 0x00F0) == (vlatch & 0xF0))
		r = mb_read(addr, debug);
	else {
		if (addr & 0x8000)
			r = ram_addr(pio_a >> 6)[addr & 0x7FFF];
		else
			r = ram_addr(pio_a >> 4)[addr & 0x7FFF];
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X -> %02X\n", addr, r);
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X <- %02X\n", addr, val);
	if (((addr >> 8) & 0x00F0) == (vlatch & 0xF0)) {
		if (mb_write(addr, val)) {
			/* Write through but into bank A */
			/* This is not well described so some guesswork is applied */
			ram[addr & 0x7FFF] = val;
#if 0
			if (addr & 0x8000)
				ram_addr((pio_a >> 6) & 1)[addr & 0x7FFF] = val;
			else
				ram_addr((pio_a >> 4) & 1)[addr & 0x7FFF] = val;
#endif
		}
	} else if (addr & 0x8000)
		ram_addr(pio_a >> 6)[addr & 0x7FFF] = val;
	else
		ram_addr(pio_a >> 4)[addr & 0x7FFF] = val;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate;
	uint8_t r = do_max_read(addr, 0);

	/* Look for ED with M1, followed directly by 4D and if so trigger
	   the interrupt chain */
	if (cpu_z80.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r == 0xCB) {
			rstate = 2;
			return r;
		}
		/* Look for ED with M1, followed directly by 4D and if so trigger
		   the interrupt chain */
		if (r == 0xED && rstate == 0) {
			rstate = 1;
			return r;
		}
	}
	if (r == 0x4D && rstate == 1)
		reti_event();
	rstate = 0;
	return r;
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO) {
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
	}
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = do_max_read(addr, 1);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return do_max_read(addr, 1);
}

static void z80_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED && (z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while (nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n", cpu_z80.R1.br.A, cpu_z80.R1.br.F, cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL, cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}


static void poll_irq_event(void)
{
	/* IM2 is not supported. Instead you always see 0xFE ?? CHECK */
	if ((sysstat & 0x80) || sio_check_im2(sio) >= 0)
		Z80INT(&cpu_z80, 0xFE);
	else
		Z80NOINT(&cpu_z80);
}

static void reti_event(void)
{
	sio_reti(sio);
	poll_irq_event();
}

static void raster_char(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp;
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + COLS * CWIDTH * y * CHEIGHT;

	if (c & 0x80)
		fp = tallchar + (c & 0x3F) * 8;
	else
		fp = shortchar + c * 8;

	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp++;
		if (rows > 7 && !(c & 0x80)) {
			bits = 0;
			fp--;
		}
		if (rows == 7 && (c & 0x80))
			fp = tallchar + (c & 0x3F) * 8 + 0x200;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x80)
				*pixp++ = 0xFFD0D0D0;
			else
				*pixp++ = 0xFF000000;
			bits <<= 1;
		}
		/* We moved on one char, move on the other 79 */
		pixp += 79 * CWIDTH;
	}
}

/* We should look at and do a complicated emulation of the 46505 but we don't really need to
   for geeral use cases. We do care about
   - width displayed
   - height displayed
   - start address
 */
static void max80_rasterize(void)
{
	unsigned int lptr = crt_reg[12] << 8 | crt_reg[13];
	unsigned int lines, cols;
	unsigned int ptr;
	unsigned int max = crt_reg[6];

	if (max > ROWS) {
		fprintf(stderr, "m6845: set to %d rows!\n", max);
		max = ROWS;
	}
	for (lines = 0; lines < max; lines++) {
		/* TODO: fix this for the correct funky mapping */
		ptr = lptr;
		for (cols = 0; cols < crt_reg[1] && cols < COLS; cols++) {
			raster_char(lines, cols, video[ptr & 0x07FF]);
			ptr++;
		}
		for (; cols < COLS; cols++)
			raster_char(lines, cols, ' ');
		lptr += crt_reg[1];
	}
	for (; lines < ROWS; lines++)
		for (cols = 0; cols < COLS; cols++)
			raster_char(lines, cols, ' ');
}

static void raster_char_wide(unsigned int y, unsigned int x, uint8_t c)
{
	uint8_t *fp;
	uint32_t *pixp;
	unsigned int rows, pixels;

	pixp = texturebits + x * CWIDTH + COLS * CWIDTH * y * CHEIGHT;

	if (c & 0x80)
		fp = tallchar + (c & 0x3F) * 8;
	else
		fp = shortchar + c * 8;

	for (rows = 0; rows < CHEIGHT; rows++) {
		uint8_t bits = *fp++;
		if (rows > 7 && !(c & 0x80)) {
			bits = 0;
			fp--;
		}
		if (rows == 7 && (c & 0x80))
			fp = tallchar + (c & 0x3F) * 8 + 0x200;
		for (pixels = 0; pixels < CWIDTH; pixels++) {
			if (bits & 0x80) {
				*pixp++ = 0xFFD0D0D0;
				*pixp++ = 0xFFD0D0D0;
			} else {
				*pixp++ = 0xFF000000;
				*pixp++ = 0xFF000000;
			}
			bits <<= 1;
		}
		/* We moved on one char, move on the other 79 */
		pixp += 79 * CWIDTH;
	}
}

/* We should look at and do a complicated emulation of the 46505 but we don't really need to
   for geeral use cases. We do care about
   - width displayed
   - height displayed
   - start address
 */
static void max80_rasterize_40(void)
{
	unsigned int lptr = crt_reg[12] << 8 | crt_reg[13];
	unsigned int lines, cols;
	unsigned int ptr;
	unsigned int max = crt_reg[6];

	if (max > ROWS) {
		fprintf(stderr, "m6845: set to %d rows!\n", max);
		max = ROWS;
	}
	for (lines = 0; lines < max; lines++) {
		/* TODO: fix this for the correct funky mapping */
		ptr = lptr;
		for (cols = 0; cols < crt_reg[1] && cols < COLS; cols += 2) {
			raster_char_wide(lines, cols, video[ptr & 0x07FF]);
			ptr += 2;
		}
		for (; cols < COLS; cols += 2) {
			raster_char_wide(lines, cols, ' ');
			ptr++;
		}
		lptr += crt_reg[1];
	}
	for (; lines < ROWS; lines++) {
		for (cols = 0; cols < COLS; cols += 2) {
			raster_char_wide(lines, cols, ' ');
			ptr++;
		}
	}
}



static void max80_render(void)
{
	SDL_Rect rect;

	rect.x = rect.y = 0;
	rect.w = COLS * CWIDTH;
	rect.h = ROWS * CHEIGHT;

	SDL_UpdateTexture(texture, NULL, texturebits, COLS * CWIDTH * 4);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture, NULL, &rect);
	SDL_RenderPresent(render);
}

/*
 *	Keyboard mapping
 */

static SDL_Keycode keyboard[] = {
	SDLK_AT, SDLK_a, SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f, SDLK_g,
	SDLK_h, SDLK_i, SDLK_j, SDLK_k, SDLK_l, SDLK_m, SDLK_n, SDLK_o,
	SDLK_p, SDLK_q, SDLK_r, SDLK_s, SDLK_t, SDLK_u, SDLK_v, SDLK_w,
	SDLK_x, SDLK_y, SDLK_z, SDLK_LEFTBRACKET, SDLK_BACKSLASH, SDLK_RIGHTBRACKET, SDLK_CARET, SDLK_UNDERSCORE,
	SDLK_0, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7,
	SDLK_8, SDLK_9, SDLK_COLON, SDLK_SEMICOLON, SDLK_COMMA, SDLK_MINUS, SDLK_PERIOD, SDLK_SLASH,
	SDLK_RETURN, SDLK_CLEAR, SDLK_PAUSE, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_SPACE,
	SDLK_LSHIFT, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_ESCAPE, 0, SDLK_LCTRL
};

/* Most PC layouts don't have a colon key so use # */
static void keytranslate(SDL_Event *ev)
{
	SDL_Keycode c = ev->key.keysym.sym;
	switch (c) {
	case SDLK_HASH:
		c = SDLK_COLON;
		break;
	case SDLK_RSHIFT:
		c = SDLK_LSHIFT;
		break;
	case SDLK_RCTRL:
		c = SDLK_LCTRL;
		break;
	case SDLK_BACKQUOTE:
		c = SDLK_UNDERSCORE;
		break;
	}
	ev->key.keysym.sym = c;
}

/* Most PC layouts don't have a colon key so use # */

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	SDL_Quit();
}

static void usage(void)
{
	fprintf(stderr, "max80: [-f] [-r path] -[A|B|C|D disk] [-8] [-S sasi] [-d debug]\n");
	exit(EXIT_FAILURE);
}

struct diskgeom {
	const char *name;
	unsigned int size;
	unsigned int sides;
	unsigned int tracks;
	unsigned int spt;
	unsigned int secsize;
	unsigned int sector0;
};

struct diskgeom disktypes[] = {
	{ "CP/M 2.2 40 Track SS", 184320, 1, 40, 18, 256, 0 },
	{ "CP/M 2.2 40 Track DS", 368640, 2, 40, 18, 256, 0 },
	{ "CP/M 3 40 Track SS", 204800, 1, 40, 10, 512, 0 },
	{ "CP/M 3 8\" 77 Track SS", 670208, 1, 77, 17, 512, 0 },
	{ NULL, }
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
	while (d->name) {
		if (d->size == size)
			return d;
		d++;
	}
	fprintf(stderr, "max80: unknown disk format size %ld.\n", (long) size);
	exit(1);
}

static void memrandom(uint8_t *p, unsigned len)
{
	time_t t = time(NULL) ^ getpid();
	srand(t);
	while (len--)
		*p++ = rand() >> 3;
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	int i;
	unsigned eightinch = 0;	/* If set drives are 8" or 5.25" with no motor control */
	char *rompath = "max80.rom";
	char *fdc_path[4] = { NULL, NULL, NULL, NULL };
	char *disk_path = NULL;

	while ((opt = getopt(argc, argv, "8A:B:C:D:S:d:r:f")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'A':
			fdc_path[0] = optarg;
			break;
		case 'B':
			fdc_path[1] = optarg;
			break;
		case 'C':
			fdc_path[2] = optarg;
			break;
		case 'D':
			fdc_path[3] = optarg;
			break;
		case 'S':
			disk_path = optarg;
			break;
		case '8':
			eightinch = 1;
			if (dipswitches == 1)
				dipswitches = 2;
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
	l = read(fd, bootrom, 512);
	if (l < 512) {
		fprintf(stderr, "max80: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	/* Approximation of the true FDC for the moment */
	fdc = wd17xx_create(1791);
	for (i = 0; i < 4; i++) {
		if (fdc_path[i]) {
			struct diskgeom *d = guess_format(fdc_path[i]);
			printf("[Drive %c, %s.]\n", 'A' + i, d->name);
			wd17xx_attach(fdc, i, fdc_path[i], d->sides, d->tracks, d->spt, d->secsize);
			wd17xx_set_sector0(fdc, i, d->sector0);
			/* Double density; required for now TODO */
			wd17xx_set_media_density(fdc, i, DEN_DD);
		}
	}
	wd17xx_trace(fdc, trace & TRACE_FDC);
	wd17xx_set_motor_time(fdc, 3000);	/* Lobo max is a 3 second timer for 5.25" */

	/* SASI bus */
	sasi = sasi_bus_create();
	if (disk_path)
		sasi_disk_attach(sasi, 0, disk_path, 512);
	sasi_bus_reset(sasi);

	pio_reset();

	memrandom(ram, sizeof(ram));
	memrandom(video, sizeof(video));
	memrandom(shortchar, sizeof(shortchar));
	memrandom(tallchar, sizeof(tallchar));

	ui_init();

	window = SDL_CreateWindow("LOBO MAX 80", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, COLS * CWIDTH, ROWS * CHEIGHT, SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "max80: unable to open window: %s\n", SDL_GetError());
		exit(1);
	}
	render = SDL_CreateRenderer(window, -1, 0);
	if (render == NULL) {
		fprintf(stderr, "max80: unable to create renderer: %s\n", SDL_GetError());
		exit(1);
	}
	texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, COLS * CWIDTH, ROWS * CHEIGHT);
	if (texture == NULL) {
		fprintf(stderr, "max80: unable to create texture: %s\n", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
	SDL_RenderClear(render);
	SDL_RenderPresent(render);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(render, COLS * CWIDTH, ROWS * CHEIGHT);

	matrix = keymatrix_create(8, 8, keyboard);
	keymatrix_trace(matrix, trace & TRACE_KEY);
	keymatrix_add_events(matrix);
	keymatrix_translator(matrix, keytranslate);

	sio = sio_create();
	sio_reset(sio);
	if (trace & TRACE_SIO) {
		sio_trace(sio, 0, 1);
		sio_trace(sio, 1, 1);
	}
	sio_attach(sio, 0, vt_create("sioa", CON_VT52));
	sio_attach(sio, 1, vt_create("siob", CON_VT52));

	tc.tv_sec = 0;
	tc.tv_nsec = 1639344L;	/*  about right */

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
	cpu_z80.trace = z80_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* 5.06MHz CPU with a periodic 61.04 Hz interrupt. This is roughly
	   correct */

	while (!emulator_done) {
		int l;
		for (l = 0; l < 10; l++) {
			int i;
			for (i = 0; i < 50; i++) {
				Z80ExecuteTStates(&cpu_z80, 166);
				sio_timer(sio);
			}
			/* ~8295 T states */
			if (ui_event())
				emulator_done = 1;
			/* Do a small block of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
			/* If there is no pending Z80 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
			poll_irq_event();
		}
		/* ~82950 T states */
		wd17xx_tick(fdc, 16);
		if (eightinch) {
			wd17xx_set_motor_time(fdc, 30000);	/* No motor timeout */
			wd17xx_motor(fdc, 1);
		}
		/* Render at about 60Hz */
		if (vlatch & 0x08)
			max80_rasterize_40();
		else
			max80_rasterize();
		max80_render();
		sysstat |= 0x80;
		poll_irq_event();
	}
	exit(0);
}
