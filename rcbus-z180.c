/*
 *	Platform features
 *
 *	Z180 at 18.432Hz
 *	512K/512K flat memory card
 *	RTC at 0x0C
 *	CF interface
 *	SD card via CSIO SPI
 *	PPIDE
 *	Floppy
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

#include "system.h"
#include "event.h"
#include "libz180/z180.h"
#include "lib765/include/765.h"
#include "serialdevice.h"
#include "z180_io.h"

#include "ttycon.h"
#include "16x50.h"
#include "acia.h"
#include "ide.h"
#include "ppide.h"
#include "piratespi.h"
#include "rtc_bitbang.h"
#include "sdcard.h"
#include "tms9918a.h"
#include "tms9918a_render.h"
#include "w5100.h"
#include "z80dis.h"
#include "zxkey.h"

static uint8_t ramrom[1024 * 1024];	/* Low 512K is ROM */

#define CPUBOARD_Z180		0
#define CPUBOARD_DYNO		1

static uint8_t cpuboard = CPUBOARD_Z180;

static uint8_t fast = 0;
static uint8_t int_recalc = 0;
static uint8_t wiznet = 0;
static uint8_t has_tms;
static uint8_t leds;
static uint8_t banked;
static uint8_t bankenable;
static uint8_t bankreg[4];
static uint8_t mem_map = 0;
static uint32_t ram_base = 0x80000;
static struct ppide *ppide;
static struct sdcard *sdcard;
static FDC_PTR fdc;
static FDRV_PTR drive_a, drive_b;
static struct tms9918a *vdp;
static struct tms9918a_renderer *vdprend;
struct zxkey *zxkey;
static struct z180_io *io;
static struct acia *acia;
static struct uart16x50 *uart;
static struct piratespi *pspi;
static unsigned int pspi_cs = 0;

static uint16_t tstate_steps = 737;	/* 18.432MHz */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

static Z180Context cpu_z180;

static nic_w5100_t *wiz;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_UNK	0x000004
#define TRACE_RTC	0x000008
#define TRACE_CPU	0x000010
#define TRACE_CPU_IO	0x000020
#define TRACE_IRQ	0x000040
#define TRACE_SD	0x008080
#define TRACE_PPIDE	0x000100
#define TRACE_TMS9918A  0x000200
#define TRACE_FDC	0x000400
#define TRACE_IDE	0x000800
#define TRACE_SPI	0x001000
#define TRACE_ACIA	0x002000
#define TRACE_512	0x004000
#define TRACE_UART	0x008000

static int trace = 0;

static void reti_event(void);

/*
 *	Model the bank registers on the paged memory
 */
static uint32_t bank_translate(uint32_t pa)
{
	unsigned int bank;
	pa &= 0xFFFF;
	bank = pa >> 14;
	if (bankenable == 0)
		return pa;
	pa = (bankreg[bank] << 14) + (pa & 0x3FFF);
	return pa;
}

/*
 *	Model the physical bus interface including wrapping and
 *	the like. This is used directly by the DMA engines
 */
uint8_t z180_phys_read(int unused, uint32_t addr)
{
	if (banked)
		addr = bank_translate(addr);
	if (mem_map == 1) {
		addr &= 0x7FFFF;	/* Only 19 bits on a DIP part */
		if (addr & 0x40000) /* RAM is 128k and wraps */
			addr &= 0x5FFFF;
	}
	return ramrom[addr & 0xFFFFF];
}

void z180_phys_write(int unused, uint32_t addr, uint8_t val)
{
	if (banked)
		addr = bank_translate(addr);
	addr &= 0xFFFFF;
	if (mem_map == 1) {
		addr &= 0x7FFFF;	/* Only 19 bits on a DIP part */
		if (addr & 0x40000) /* RAM is 128k and wraps */
			addr &= 0x5FFFF;
	}
	if (addr >= ram_base)
		ramrom[addr] = val;
	else
		fprintf(stderr, "[%06X: write to ROM from %04X.]\n", addr, cpu_z180.M1PC);
}

/*
 *	Model CPU accesses starting with a virtual address
 */
static uint8_t do_mem_read0(uint16_t addr, int quiet)
{
	uint32_t pa = z180_mmu_translate(io, addr);
	uint8_t r;
	if (banked)
		pa = bank_translate(pa);
	r = z180_phys_read(0, pa);
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X[%06X] -> %02X\n", addr, pa, r);
	return r;
}

static void mem_write0(uint16_t addr, uint8_t val)
{
	uint32_t pa = z180_mmu_translate(io, addr);
	if (banked)
		pa = bank_translate(pa);
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %04X[%06X] <- %02X\n", addr, pa, val);
	z180_phys_write(0, pa, val);
}

uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r;

	switch (cpuboard) {
	case CPUBOARD_Z180:
	case CPUBOARD_DYNO:
		r = do_mem_read0(addr, 0);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
	if (cpu_z180.M1) {
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

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z180:
	case CPUBOARD_DYNO:
		mem_write0(addr, val);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = do_mem_read0(addr, 1);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return do_mem_read0(addr, 1);
}

static void rcbus_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z180.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z180.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z180.R1.br.A, cpu_z180.R1.br.F,
		cpu_z180.R1.wr.BC, cpu_z180.R1.wr.DE, cpu_z180.R1.wr.HL,
		cpu_z180.R1.wr.IX, cpu_z180.R1.wr.IY, cpu_z180.R1.wr.SP);
}

void recalc_interrupts(void)
{
	int_recalc = 1;
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	uint8_t r =  ide_read8(ide0, addr);
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide read %d = %02X\n", addr, r);
	return r;
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide write %d = %02X\n", addr, val);
	ide_write8(ide0, addr, val);
}

struct rtc *rtc;

/* Software SPI test: one device for now */

#include "bitrev.h"

uint8_t z180_csio_write(struct z180_io *io, uint8_t bits)
{
	int r;

	if (pspi_cs == 0 && pspi) {
		r = piratespi_txrx(pspi, bitrev[bits]);
		if (r == -1)
			return 0xFF;
		if (trace & TRACE_SPI)
			fprintf(stderr,	"[SPI2 %02X:%02X]\n", bitrev[bits], r);
		return bitrev[r];
	}

	if (sdcard == NULL)
		return 0xFF;

	/* bitrev will always return a value 0-255 so the trace reverse below
	   is safe */
	r = bitrev[sd_spi_in(sdcard, bitrev[bits])];
	if (trace & TRACE_SPI)
		fprintf(stderr,	"[SPI %02X:%02X]\n", bitrev[bits], bitrev[r]);
	return r;
}

static void fdc_log(int debuglevel, char *fmt, va_list ap)
{
	if ((trace & TRACE_FDC) || debuglevel == 0)
		vfprintf(stderr, "fdc: ", ap);
}

static void fdc_show_dor(uint8_t val)
{
	if (trace & TRACE_FDC) {
		fprintf(stderr, "FDC DOR %02X [", val);
		if (val & 0x80)
			fprintf(stderr, "SPECIAL ");
		else
			fprintf(stderr, "AT/EISA ");
		if (val & 0x20)
			fprintf(stderr, "MOEN2 ");
		if (val & 0x10)
			fprintf(stderr, "MOEN1 ");
		if (val & 0x08)
			fprintf(stderr, "DMA ");
		if (!(val & 0x04))
			fprintf(stderr, "SRST ");
		if (!(val & 0x02))
			fprintf(stderr, "DSEN ");
		if (val & 0x01)
			fprintf(stderr, "DSEL1");
		else
			fprintf(stderr, "DSEL0");
		fprintf(stderr, "]\n");
	}
}

static void fdc_show_dcr(uint8_t val)
{
	if (trace & TRACE_FDC) {
		fprintf(stderr, "FDC DCR %02X [", val);
		if (!(val & 4))
			fprintf(stderr, "WCOMP");
		switch(val & 3) {
		case 0:
			fprintf(stderr, "500K MFM RPM");
			break;
		case 1:
			fprintf(stderr, "250K MFM");
			break;
		case 2:
			fprintf(stderr, "250K MFM RPM");
			break;
		case 3:
			fprintf(stderr, "INVALID");
		}
		fprintf(stderr, "]\n");
	}
}

static void fdc_write(uint8_t addr, uint8_t val)
{
	switch(addr) {
	case 1:	/* Data */
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC Data: %02X\n", val);
		fdc_write_data(fdc, val);
		break;
	case 2:	/* DOR */
		fdc_show_dor(val);
		fdc_write_dor(fdc, val);
		break;
	case 3:	/* DCR */
		fdc_show_dcr(val);
		fdc_write_drr(fdc, val & 3);	/* TODO: review */
		break;
	case 4:	/* TC */
		fdc_set_terminal_count(fdc, 0);
		fdc_set_terminal_count(fdc, 1);
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC TC\n");
		break;
	case 5:	/* RESET */
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC RESET\n");
		break;
	default:
		fprintf(stderr, "FDC bogus %02X->%02X\n", addr, val);
	}
}

static uint8_t fdc_read(uint8_t addr)
{
	uint8_t val = 0x78;
	switch(addr) {
	case 0:	/* Status*/
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC Read Status: ");
		val = fdc_read_ctrl(fdc);
		break;
	case 1:	/* Data */
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC Read Data: ");
		val = fdc_read_data(fdc);
		break;
	case 4:	/* TC */
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC TC: ");
		break;
	case 5:	/* RESET */
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC RESET: ");
		break;
	default:
		if (trace & TRACE_FDC)
			fprintf(stderr, "FDC bogus read %02X: ", addr);
	}
	fprintf(stderr, "%02X\n", val);
	return val;
}

static void diag_write(uint8_t val)
{
	uint8_t x[12];
	unsigned int i;
	if (leds == 0)
		return;

	memcpy(x, "\n[--------]\n", 12);
	for (i = 0; i < 8; i++)
		if (val & (1 << i))
			x[i + 2] = '@';
	write(1, x, 12);
}

/* The RTC is on this port but the other bits also do magic
	7: RTC DOUT
	6: RTC SCLK
	5: RTC \WE
	4: RTC \CE
	3: SD CS2
	2: SD CS1
	1: Flash select (not used)
	0: SCL (I2C) */

static void sysio_write(uint8_t val)
{
	static uint8_t sysio = 0xFF;
	uint8_t delta = val ^ sysio;
	if (sdcard && (delta & 4)) {
		if (trace & TRACE_SPI)
			fprintf(stderr, "[SPI CS %sed]\n",
				(val & 4) ? "rais" : "lower");
		if (val & 4)
			sd_spi_raise_cs(sdcard);
		else
			sd_spi_lower_cs(sdcard);
	}
	if (pspi && (delta & 8)) {
		if (trace & TRACE_SPI)
			fprintf(stderr, "[SPI2 CS %sed]\n",
				(val & 8) ? "rais" : "lower");
		piratespi_cs(pspi, val & 8);
	}
	pspi_cs = val & 8;
	if ((val & 0x0C) == 0x00)
		fprintf(stderr, "[Error SPI contention.]\n");
	sysio = val;
	/* We don't have anything on the second emulated SPI nor on the
	   I2C */
}

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

static uint8_t io_read_2014(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if (z180_iospace(io, addr))
		return z180_read(io, addr);
	if ((addr & 0xFF) == 0xBA) {
		return 0xCC;
	}
	if (zxkey && (addr & 0xFC) == 0xFC)
		return zxkey_scan(zxkey, addr);

	addr &= 0xFF;
	if (addr >= 0x80 && addr < 0xC0 && acia)
		return acia_read(acia, addr & 1);
	if (addr >= 0xA0 && addr <= 0xA7 && uart)
		return uart16x50_read(uart, addr & 7);
	if (addr >= 0x48 && addr < 0x50 && cpuboard != CPUBOARD_DYNO)
		return fdc_read(addr & 7);
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x27 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read(rtc);
	if ((addr == 0x98 || addr == 0x99) && vdp)
		return tms9918a_read(vdp, addr & 1);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_2014(uint16_t addr, uint8_t val, uint8_t known)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);

	if (z180_iospace(io, addr)) {
		z180_write(io, addr, val);
		known = 1;
	}
	if ((addr & 0xFF) == 0xBA) {
		/* Quart */
		return;
	}
	addr &= 0xFF;
	if (addr >= 0x80 && addr < 0xC0 && acia)
		acia_write(acia, addr & 1, val);
	else if (addr >= 0xA0 && addr <= 0xA7 && uart)
		uart16x50_write(uart, addr & 7, val);
	else if (addr >= 0x48 && addr < 0x50 && cpuboard != CPUBOARD_DYNO)
		fdc_write(addr & 7, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x27 && ide == 2)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr == 0x0C) {
		if (rtc)
			rtc_write(rtc, val);
		sysio_write(val);
	} else if (banked && addr >= 0x78 && addr < 0x7C) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(stderr, "Bank %d set to %d\n", addr & 3, val);
	} else if (banked && addr >= 0x7C && addr <=0x7F) {
		if (trace & TRACE_512)
			fprintf(stderr, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0x0D)
		diag_write(val);
	else if ((addr == 0x98 || addr == 0x99) && vdp)
		tms9918a_write(vdp, addr & 1, val);
	else if (addr == 0xFD) {
		trace &= 0xFF00;
		trace |= val;
		printf("trace set to %04X\n", trace);
	} else if (addr == 0xFE) {
		trace &= 0xFF;
		trace |= val << 8;
		printf("trace set to %d\n", trace);
	} else if (!known && (trace & TRACE_UNK))
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

/* BQ4845  - TODO split out into a driver file */

static uint8_t makebcd(unsigned n)
{
	uint8_t r;
	r = n % 10;
	r |= (n / 10) << 4;
	return r;
}

static void bqrtc_write(uint16_t addr, uint8_t val)
{
}

static uint8_t bqrtc_read(uint16_t addr)
{
	time_t t;
	struct tm *tm;

	time(&t);
	tm = gmtime(&t);

	switch(addr & 0x0F) {
	case 0:
		return makebcd(tm->tm_sec);
	case 1:
		return 0x00;
	case 2:
		return makebcd(tm->tm_min);
	case 3:
		return 0x00;
	case 4:
		/* FIXME: we assume 24hr mode */
		return makebcd(tm->tm_hour);
		/* 12hr is 0-11 and set top bit for PM */
	case 5:
		return 0;
	case 6:
		return makebcd(tm->tm_mday);
	case 7:
		return 0;
	case 8:
		return makebcd(tm->tm_wday + 1);
	case 9:
		return makebcd(tm->tm_mon + 1);
	case 10:
		return makebcd(tm->tm_year % 100);
	case 11:
		return 0x00;
	case 12:
		return 0x00;
	case 13:
		return 0x01;
	case 14:
		return 0x02;
	case 15:
		return 0x00;
	}
	return 0xFF;
}

static void fdc_write_dyno(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_FDC)
		fprintf(stderr, "fdc: W %02X <- %02X\n", addr, val);
	switch(addr & 3) {
	case 0:	/* MSR */
		break;
	case 1:	/* Data */
		fdc_write_data(fdc, val);
		break;
	case 2:	/* DOR */
		fdc_show_dor(val);
		fdc_write_dor(fdc, val);
		break;
	case 3:	/* DCR */
		fdc_show_dcr(val);
		fdc_write_drr(fdc, val & 3);	/* TODO: review */
		break;
	default:
		fprintf(stderr, "FDC bogus %02X->%02X\n", addr, val);
	}
}

static uint8_t fdc_read_dyno(uint8_t addr)
{
	uint8_t val = 0x78;
	switch(addr & 3) {
	case 0:	/* Status*/
		val = fdc_read_ctrl(fdc);
		break;
	case 1:	/* Data */
		val = fdc_read_data(fdc);
		break;
	case 2:	/* TC */
		fdc_set_terminal_count(fdc, 0);
		fdc_set_terminal_count(fdc, 1);
		break;
	case 3:
		break;
	}
	if (trace & TRACE_FDC)
		fprintf(stderr, "fdc: R %02X -> %02X\n", addr, val);
	return val;
}

static uint8_t io_read_dyno(uint16_t addr)
{
	uint8_t addr8 = addr & 0xFF;
	if (addr8 >= 0x4C && addr8 <= 0x4F && ppide)
		return ppide_read(ppide, addr & 3);
	if (addr8 >= 0x50 && addr8 < 0x5F)		/* CHECK */
		return bqrtc_read(addr);
	if (addr8 >= 0x84 && addr8 <= 0x87)
		return fdc_read_dyno(addr);
	return io_read_2014(addr);
}

static void io_write_dyno(uint16_t addr, uint8_t val, uint8_t known)
{
	uint8_t addr8 = addr & 0xFF;
	if (addr8 >= 0x4C && addr8 <= 0x4F&& ppide)
		ppide_write(ppide, addr & 3, val);
	else if (addr8 >= 0x50 && addr8 < 0x5F)		/* CHECK */
		bqrtc_write(addr, val);
	else if (addr8 >= 0x84 && addr8 <= 0x87)
		fdc_write_dyno(addr, val);
	else
		io_write_2014(addr, val, 0);
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z180:
		io_write_2014(addr, val, 0);
		break;
	case CPUBOARD_DYNO:
		io_write_dyno(addr, val, 0);
		break;
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

uint8_t io_read(int unused, uint16_t addr)
{
	switch (cpuboard) {
	case CPUBOARD_Z180:
		return io_read_2014(addr);
	case CPUBOARD_DYNO:
		return io_read_dyno(addr);
		break;
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

static void poll_irq_event(void)
{
	if (acia && acia_irq_pending(acia))
		z180_interrupt(io, 0, 0xFF, 1);
	if (uart && uart16x50_irq_pending(uart))
		z180_interrupt(io, 0, 0xFF, 1);
	else if (vdp && tms9918a_irq_pending(vdp))
		z180_interrupt(io, 0, 0xFF, 1);
	else
		z180_interrupt(io, 0, 0, 0);
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
	live_irq = 0;
	poll_irq_event();
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

static void usage(void)
{
	fprintf(stderr, "rcbus-z180: [-a] [-b] [-f] [-i idepath] [-P buspirate] [-R] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "rcbus-z180.rom";
	char *sdpath = NULL;
	char *idepath = NULL;
	char *patha = NULL, *pathb = NULL;
	char *piratepath = NULL;
	int input = 0;

	uint8_t *p = ramrom;
	while (p < ramrom + sizeof(ramrom))
		*p++= rand();

	while ((opt = getopt(argc, argv, "1acd:fF:i:I:lm:r:sP:RS:Twzb")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'S':
			sdpath = optarg;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
			break;
		case 'I':
			ide = 2;
			idepath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'l':
			leds = 1;
			break;
		case 'm':
			if (strcmp(optarg, "sc126") == 0) {
				rtc = rtc_create();
				banked = 0;
				input = 0;
				break;
			}
			if (strcmp(optarg, "sc130") == 0 || strcmp(optarg, "sc131") == 0 || strcmp(optarg, "sc111") ==0) {
				banked = 0;
				input = 0;
				break;
			}
			if (strcmp(optarg, "riz180") == 0) {
				/* A DIP Z180 running more slowly and with
				   only 128K RAM and 256K flash accessible */
				tstate_steps = 294;
				banked = 0;
				input = 0;
				ram_base = 0x40000;
				mem_map = 1;
				/* No SPI select line */
				sdpath = NULL;
				break;
			}
			if (strcmp(optarg, "dyno") == 0) {
				/* RCZ180 like but different PPIDE location
				   and different RTC */
				banked = 0;
				input = 0;
				cpuboard = CPUBOARD_DYNO;
				break;
			}
			fprintf(stderr, "rcbus-z180: unknown machine type '%s'.\n", optarg);
			exit(1);
		case 'f':
			fast = 1;
			break;
		case 'P':
			piratepath = optarg;
			break;
		case 'R':
			rtc = rtc_create();
			break;
		case 'b':
			banked = 1;
			break;
		case 'w':
			wiznet = 1;
			break;
		case 'a':
			input = 1;
			break;
		case '1':
			input = 2;
			break;
		case 'F':
			if (pathb) {
				fprintf(stderr, "rcbus-z180: too many floppy disks specified.\n");
				exit(1);
			}
			if (patha)
				pathb = optarg;
			else
				patha = optarg;
			break;
		case 'z':
			zxkey = zxkey_create(2);
			break;
		case 'T':
			has_tms = 1;
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
	if (read(fd, ramrom, ram_base) != ram_base) {
		fprintf(stderr, "rcbus-z180: ROM image should be %dK.\n",
			ram_base >> 10);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide == 1 ) {
		ide0 = ide_allocate("cf");
		if (ide0) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			}
			if (ide_attach(ide0, 0, ide_fd) == 0) {
				ide = 1;
				ide_reset_begin(ide0);
			}
		} else
			ide = 0;
	}

	/* FIXME: merge IDE handling once cf is a driver */
	if (ide == 2) {
		ppide = ppide_create("ppide");
		int ide_fd = open(idepath, O_RDWR);
		if (ide_fd == -1) {
			perror(idepath);
			ide = 0;
		} else
			ppide_attach(ppide, 0, ide_fd);
		if (trace & TRACE_PPIDE)
			ppide_trace(ppide, 1);
	}
	if (sdpath) {
		sdcard = sd_create("sd0");
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
		if (trace & TRACE_SD)
			sd_trace(sdcard, 1);
	}

	io = z180_create(&cpu_z180);
	z180_trace(io, trace & TRACE_CPU_IO);
	if (tstate_steps == 294)
		z180_set_clock(io, 6144000);
	else
		z180_set_clock(io, 18432000);

	switch(input) {
		case 0:
			z180_ser_attach(io, 0, &console);
			z180_ser_attach(io, 1, &console_wo);
			break;
		case 1:
			acia = acia_create();
			acia_trace(acia, trace & TRACE_ACIA);
			acia_attach(acia, &console);
			break;
		case 2:
			uart = uart16x50_create();
			uart16x50_trace(uart, trace & TRACE_UART);
			uart16x50_attach(uart, &console);
			break;
	}

	if (rtc)
		rtc_trace(rtc, trace & TRACE_RTC);

	if (has_tms) {
		vdp = tms9918a_create();
		tms9918a_trace(vdp, !!(trace & TRACE_TMS9918A));
		vdprend = tms9918a_renderer_create(vdp);
	}
	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

	fdc = fdc_new();

	lib765_register_error_function(fdc_log);

	if (patha) {
		drive_a = fd_newdsk();
		fd_settype(drive_a, FD_35);
		fd_setheads(drive_a, 2);
		fd_setcyls(drive_a, 80);
		fdd_setfilename(drive_a, patha);
	} else
		drive_a = fd_new();

	if (pathb) {
		drive_b = fd_newdsk();
		fd_settype(drive_a, FD_35);
		fd_setheads(drive_a, 2);
		fd_setcyls(drive_a, 80);
		fdd_setfilename(drive_a, pathb);
	} else
		drive_b = fd_new();

	fdc_reset(fdc);
	fdc_setisr(fdc, NULL);

	fdc_setdrive(fdc, 0, drive_a);
	fdc_setdrive(fdc, 1, drive_b);

	if (piratepath)
		pspi = piratespi_create(piratepath);

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

	Z180RESET(&cpu_z180);
	cpu_z180.ioRead = io_read;
	cpu_z180.ioWrite = io_write;
	cpu_z180.memRead = mem_read;
	cpu_z180.memWrite = mem_write;
	cpu_z180.trace = rcbus_trace;

	/* We don't have a GPIO control pin on the SC126, but we do have
	   devices that need to be wired to \RESET so emulate that with
	   the ALT pin. */
	if (pspi) {
		piratespi_alt(pspi, 0);
		piratespi_alt(pspi, 1);
	}

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int states = 0;
		unsigned int i, j;
		/* We have to run the DMA engine and Z180 in step per
		   instruction otherwise we will mess up on stalling DMA */

		/* Do an emulated 20ms of work (368640 clocks) */
		for (i = 0; i < 50; i++) {
			for (j = 0; j < 10; j++) {
				while (states < tstate_steps) {
					unsigned int used;
					used = z180_dma(io);
					if (used == 0)
						used = Z180Execute(&cpu_z180);
					states += used;
				}
				z180_event(io, states);
				states -= tstate_steps;
			}
			fdc_tick(fdc);
			/* We want to run UI events regularly it seems */
			ui_event();
		}

		/* 50Hz which is near enough */
		if (vdp) {
			tms9918a_rasterize(vdp);
			tms9918a_render(vdprend);
		}
		if (wiznet)
			w5100_process(wiz);
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending Z180 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
			if (!live_irq)
				poll_irq_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z180.IFF1|cpu_z180.IFF2))
				int_recalc = 0;
		}
	}
	fd_eject(drive_a);
	fd_eject(drive_b);
	fdc_destroy(&fdc);
	fd_destroy(&drive_a);
	fd_destroy(&drive_b);
	if (pspi)
		piratespi_free(pspi);
	exit(0);
}
