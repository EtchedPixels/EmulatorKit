/*
 *	Platform features
 *
 *	6809 CPU
 *	IDE at $FE10-$FE17 no high or control access (mirrored at $FE90-97)
 *	Simple memory 32K ROM / 32K RAM
 *	Memory banking Zeta style 16K page at $FE78-$FE7B (enable at $FE7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at $FE0C
 *	WizNET ethernet
 *	6840 at 0xFE60
 *	6821 at 0xFE68 TODO CHECK
 *
 *	Alternate MMU option using highmmu on 8085/MMU card at FEFF
 *
 *	TODO:
 *	Lots - this is just an initial hack for testing
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
#include "d6809.h"
#include "e6809.h"
#include "ide.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "16x50.h"
#include "6821.h"
#include "6840.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "sdcard.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t bankhigh = 0;
static uint8_t mmureg = 0;
static uint8_t rtc;
static uint8_t fast = 0;
static uint8_t wiznet = 0;

struct ppide *ppide;
struct rtc *rtcdev;
struct sdcard *sdcard;
struct uart16x50 *uart;
struct m6840 *ptm;
struct m6821 *pia;

/* The CPU runs at CLK/4 so for sane RS232 we run at the usual clock
   rate and get 115200 baud - which is pushing it admittedly! */
static uint16_t clockrate =  364/4;

/* Who is pulling on the IRQ1 interrupt line */

static uint8_t live_irq;

#define IRQ_16550A	1
#define IRQ_PTM		2
#define IRQ_PIA		4

static nic_w5100_t *wiz;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_PPIDE	16
#define TRACE_512	32
#define TRACE_RTC	64
#define TRACE_CPU	128
#define TRACE_IRQ	256
#define TRACE_UART	512
#define TRACE_PTM	1024
#define TRACE_PIA	2048
#define TRACE_SPI	4096
#define TRACE_SD	8192

static int trace = 0;

static void int_clear(unsigned int irq)
{
	live_irq &= ~(1 << irq);
}

static void int_set(unsigned int irq)
{
	live_irq |= 1 << irq;
}

void recalc_interrupts(void)
{
	if (uart16x50_irq_pending(uart))
		int_set(IRQ_16550A);
	else
		int_clear(IRQ_16550A);
	if (m6821_irq_pending(pia))
		int_set(IRQ_PIA);
	else
		int_clear(IRQ_PIA);
	if (m6840_irq_pending(ptm))
		int_set(IRQ_PTM);
	else
		int_clear(IRQ_PTM);
}

static uint8_t bitcnt;
static uint8_t txbits, rxbits;
static uint8_t pia_a, pia_b;
static uint8_t pia_a_in;

static void spi_clock_high(void)
{
	txbits <<= 1;
	txbits |= !!(pia_a & 2);
	bitcnt++;
	if (bitcnt == 8) {
		rxbits = sd_spi_in(sdcard, txbits);
		if (trace & TRACE_SPI)
			fprintf(stderr, "spi %02X | %02X\n", rxbits, txbits);
		bitcnt = 0;
	}
}

static void spi_clock_low(void)
{
	pia_a_in &= 0x7F;
	pia_a_in |= (rxbits & 0x80);
	rxbits <<= 1;
	rxbits |= 1;
}


uint8_t m6821_input(struct m6821 *pia, int port)
{
	if (port == 1)
		return 0xFF;
	return pia_a_in;
}

void m6821_output(struct m6821 *pia, int port, uint8_t data)
{
	if (port == 1)
		pia_b = data;
	else {
		uint8_t delta = data ^ pia_a;
		pia_a = data;
		if (delta & 4) {
			if (data & 4) {
				bitcnt = 0;
				sd_spi_raise_cs(sdcard);
			} else {
				sd_spi_lower_cs(sdcard);
			}
		}
		if (delta & 1) {
			if (data & 1)
				spi_clock_high();
			else
				spi_clock_low();
		}
	}
}

/* We don't use the strobe */
void m6821_strobe(struct m6821 *pia, int port)
{
}

/* We don't use the control signals */
void m6821_ctrl_change(struct m6821 *pia, uint8_t ctrl)
{
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	return ide_read8(ide0, addr);
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	ide_write8(ide0, addr, val);
}

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

/* Clock timer 3 off timer 2 */
void m6840_output_change(struct m6840 *m, uint8_t outputs)
{
	static int old = 0;
	if ((outputs ^ old) & 2) {
		/* timer 2 high to low -> clock timer 3 */
		if (!(outputs & 2))
			m6840_external_clock(ptm, 3);
	}
	old = outputs;
}

static uint8_t m6809_do_inport(uint8_t addr)
{
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr >= 0x60 && addr <= 0x67)
		return m6840_read(ptm, addr & 7);
	if (addr >= 0x68 && addr <= 0x6F)
		return m6821_read(pia, addr & 7);
	if (addr == 0x0C && rtc)
		return rtc_read(rtcdev);
	if (addr >= 0xC0 && addr <= 0xC7)
		return uart16x50_read(uart, addr & 7);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

uint8_t m6809_inport(uint8_t addr)
{
	uint8_t r;
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x = ", addr);
	r = m6809_do_inport(addr);
	if (trace & TRACE_IO)
		fprintf(stderr, "%02x\n", r);
	return r;
}

void m6809_outport(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if (addr == 0xFF && bankhigh) {
		mmureg = val;
		if (trace & TRACE_512)
			fprintf(stderr, "MMUreg set to %02X\n", val);
	}
	else if (addr == 0x80)
		fprintf(stderr, "[%02X] ", val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		my_ide_write(addr & 7, val);
	else if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr >= 0x60 && addr <= 0x67)
		m6840_write(ptm, addr & 7, val);
	else if (addr >= 0x68 && addr <= 0x6F)
		m6821_write(pia, addr & 7, val);
	else if (addr >= 0xC0 && addr <= 0xC7)
		uart16x50_write(uart, addr & 7, val);
	/* FIXME: real bank512 alias at 0x70-77 for 78-7F */
	else if (bank512 && addr >= 0x78 && addr <= 0x7B) {
		bankreg[addr & 3] = val & 0x3F;
		if (trace & TRACE_512)
			fprintf(stderr, "Bank %d set to %d\n", addr & 3, val);
	} else if (bank512 && addr >= 0x7C && addr <= 0x7F) {
		if (trace & TRACE_512)
			fprintf(stderr, "Banking %sabled.\n", (val & 1) ? "en" : "dis");
		bankenable = val & 1;
	} else if (addr == 0x0C && rtc)
		rtc_write(rtcdev, val);
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

unsigned char do_e6809_read8(unsigned addr, unsigned debug)
{
	if (addr >> 8 == 0xFE) {
		if (debug)
			return 0xFF;
		return m6809_inport(addr & 0xFF);
	}
	if (bankhigh) {
		uint8_t reg = mmureg;
		uint8_t val;
		uint32_t higha;
		if (addr < 0xE000)
			reg >>= 1;
		higha = (reg & 0x40) ? 1 : 0;
		higha |= (reg & 0x10) ? 2 : 0;
		higha |= (reg & 0x4) ? 4 : 0;
		higha |= (reg & 0x01) ? 8 : 0;	/* ROM/RAM */

		val = ramrom[(higha << 16) + addr];
		if (trace & TRACE_MEM) {
			fprintf(stderr, "R %04X[%02X] = %02X\n",
				(unsigned int)addr,
				(unsigned int)higha,
				(unsigned int)val);
		}
		return val;
	} else 	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(stderr, "R %04x[%02X] = %02X\n", addr, (unsigned int) bankreg[bank], (unsigned int) ramrom[(bankreg[bank] << 14) + (addr & 0x3FFF)]);
		addr &= 0x3FFF;
		return ramrom[(bankreg[bank] << 14) + addr];
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

unsigned char e6809_read8(unsigned addr)
{
	return do_e6809_read8(addr, 0);
}

unsigned char e6809_read8_debug(unsigned addr)
{
	return do_e6809_read8(addr, 1);
}

void e6809_write8(unsigned addr, unsigned char val)
{
	if (addr >> 8 == 0xFE) {
		m6809_outport(addr & 0xFF, val);
		return;
	}
	if (bankhigh) {
		uint8_t reg = mmureg;
		uint8_t higha;
		if (addr < 0xE000)
			reg >>= 1;
		higha = (reg & 0x40) ? 1 : 0;
		higha |= (reg & 0x10) ? 2 : 0;
		higha |= (reg & 0x4) ? 4 : 0;
		higha |= (reg & 0x01) ? 8 : 0;	/* ROM/RAM */

		if (trace & TRACE_MEM) {
			fprintf(stderr, "W %04X[%02X] = %02X\n",
				(unsigned int)addr,
				(unsigned int)higha,
				(unsigned int)val);
		}
		if (!(higha & 8)) {
			if (trace & TRACE_MEM)
				fprintf(stderr, "[Discard: ROM]\n");
			return;
		}
		ramrom[(higha << 16)+ addr] = val;
	} else if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (trace & TRACE_MEM)
			fprintf(stderr, "W %04x[%02X] = %02X\n", (unsigned int) addr, (unsigned int) bankreg[bank], (unsigned int) val);
		if (bankreg[bank] >= 32) {
			addr &= 0x3FFF;
			ramrom[(bankreg[bank] << 14) + addr] = val;
		}
		/* ROM writes go nowhere */
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X = %02X\n", addr, val);
		if (addr < 32768 && !bank512)
			ramrom[addr] = val;
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	}
}

static const char *make_flags(uint8_t cc)
{
	static char buf[9];
	char *p = "EFHINZVC";
	char *d = buf;

	while(*p) {
		if (cc & 0x80)
			*d++ = *p;
		else
			*d++ = '-';
		cc <<= 1;
		p++;
	}
	*d = 0;
	return buf;
}

/* Called each new instruction issue */
void e6809_instruction(unsigned pc)
{
	char buf[80];
	struct reg6809 *r = e6809_get_regs();
	if (trace & TRACE_CPU) {
		/*
		 * The PC reported by e6809 can have garbage in the upper
		 * bits of the value. Mask it off.
		 */
		unsigned dpc = pc & 0xFFFF;
		d6809_disassemble(buf, dpc);
		fprintf(stderr, "%04X: %-16.16s | ", dpc, buf);
		fprintf(stderr, "%s %02X:%02X %04X %04X %04X %04X\n",
			make_flags(r->cc),
			r->a, r->b, r->x, r->y, r->u, r->s);
	}
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "rcbus-6809: [-b] [-f] [-R] [-i idepath] [-I ppidepath] [-S sdcardpath] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	char *rompath = "rcbus-6809.rom";
	char *idepath = NULL;
	char *sdpath = NULL;
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "1abBd:fi:I:r:RS:w")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'b':
			bank512 = 1;
			bankhigh = 0;
			rom = 0;
			break;
		case 'B':
			bankhigh = 1;
			bank512 = 0;
			rom = 0;
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
		case 'f':
			fast = 1;
			break;
		case 'R':
			rtc = 1;
			break;
		case 'S':
			sdpath = optarg;
			break;
		case 'w':
			wiznet = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (rom == 0 && bank512 == 0 && bankhigh == 0) {
		fprintf(stderr, "rcbus-6809: no ROM\n");
		exit(EXIT_FAILURE);
	}

	if (rom) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom + 32768, 32768) != 32768) {
			fprintf(stderr, "rcbus: short rom '%s'.\n", rompath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}

	if (bank512 || bankhigh) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom, 524288) != 524288) {
			fprintf(stderr, "rcbus: banked rom image should be 512K.\n");
			exit(EXIT_FAILURE);
		}
		bankenable = 1;
		close(fd);
	}

	if (ide) {
		/* FIXME: clean up when classic cf becomes a driver */
		if (ide == 1) {
			ide0 = ide_allocate("cf");
			if (ide0) {
				int ide_fd = open(idepath, O_RDWR);
				if (ide_fd == -1) {
					perror(idepath);
					ide = 0;
				}
				else if (ide_attach(ide0, 0, ide_fd) == 0) {
					ide = 1;
					ide_reset_begin(ide0);
				}
			} else
				ide = 0;
		} else {
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
	}

	sdcard = sd_create("sd0");
	if (sdpath) {
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
	}
	if (trace & TRACE_SD)
		sd_trace(sdcard, 1);
	sd_blockmode(sdcard);

	uart = uart16x50_create();
	uart16x50_trace(uart, trace & TRACE_UART);
	uart16x50_attach(uart, &console);

	ptm = m6840_create();
	m6840_trace(ptm, trace & TRACE_PTM);

	pia = m6821_create();
	m6821_trace(pia, trace & TRACE_PIA);

	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

	if (rtc) {
		rtcdev = rtc_create();
		rtc_reset(rtcdev);
		if (trace & TRACE_RTC)
			rtc_trace(rtcdev, 1);
	}

	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;

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

	e6809_reset(trace & TRACE_CPU);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i, j;
		/* 36400 T states for base rcbus - varies for others */
		for (i = 0; i < 100; i++) {
			while(cycles < clockrate)
				cycles += e6809_sstep(live_irq, 0);
			m6840_tick(ptm, cycles);
			for (j = 0; j < cycles; j++)
				m6840_external_clock(ptm, 2);
			cycles -= clockrate;
			recalc_interrupts();
		}
		/* Drive the  serial */
		uart16x50_event(uart);
		/* Wiznet timer */
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
