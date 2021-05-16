/*
 *	Platform features
 *
 *	68HC11 CPU
 *	IDE at $FE10-$FE17 no high or control access (mirrored at $FE90-97)
 *	Simple memory 32K ROM / 32K RAM
 *	Memory banking Zeta style 16K page at $FE78-$FE7B (enable at $FE7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at $FE0C
 *	WizNET ethernet
 *
 *	Alternate MMU option using highmmu on 8085/MMU card
 *
 *	Not yet supported: linear memory via I/O ports
 *
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
#include "6800.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "w5100.h"
#include "sdcard.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */
static uint8_t monitor[12288];		/* Monitor ROM - usually Buffalo */
static uint8_t eerom[2048];		/* EEROM - not properly emulated yet */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t bankhigh = 0;
static uint8_t bankflat = 0;
static uint8_t mmureg = 0;
static uint8_t rtc;
static uint8_t fast = 0;
static uint8_t wiznet = 0;

static uint32_t flatahigh = 0;		/* Flat model high address bits */

static uint8_t protlow, prothi;

struct ppide *ppide;
struct rtc *rtcdev;
struct sdcard *sdcard;

/* The CPU E clock runs at CLK/4. The RC2014 standard clock is fine
   and makes serial rates easy */
static uint16_t clockrate =  364/4;

/* Who is pulling on the IRQ1 interrupt line */

static uint8_t live_irq;

#define IRQ_16550A	1

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
#define TRACE_SD	1024
#define TRACE_SPI	2048

static int trace = 0;

struct m6800 cpu;

static uint16_t lastch = 0xFFFF;

int check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;
	char c;


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
	if (FD_ISSET(0, &i)) {
		r |= 1;
		if (lastch == 0xFFFF) {
			if (read(0, &c, 1) == 1)
				lastch = c;
			if (c == ('V' & 31)) {
				cpu.debug ^= 1;
				fprintf(stderr, "CPUTRACE now %d\n", cpu.debug);
				lastch = 0xFFFF;
				r &= ~1;
			}
		}
	}
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

unsigned int next_char(void)
{
	char c = lastch;
	lastch = 0xFFFF;
	if (c == 0x0A)
		c = '\r';
	return c;
}

void recalc_interrupts(void)
{
	if (live_irq)
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else
		m6800_clear_interrupt(&cpu, IRQ_IRQ1);
}

/* Serial glue: a bit different as the serial port is on chip and emulated
   by the CPU emulator */

void m6800_sci_change(struct m6800 *cpu)
{
	static uint_fast8_t pscale[4] = {1, 3, 4, 13 };
	unsigned int baseclock = 7372800 / 4;	/* E clock */
	unsigned int prescale = pscale[(cpu->io.baud >> 4) & 0x03];
	unsigned int divider = 1 << (cpu->io.baud & 7);

	/* SCI changed status - could add debug here FIXME */
	if (!(trace & TRACE_UART))
		return;

	baseclock /= prescale;
	baseclock /= divider;
	baseclock /= 16;

	fprintf(stderr, "[UART  %d baud]\n", baseclock);
}

void m6800_tx_byte(struct m6800 *cpu, uint8_t byte)
{
	write(1, &byte, 1);
}

static void flatarecalc(struct m6800 *cpu)
{
	uint8_t bits = cpu->io.padr;
	/* PA3 has a pull down so if it is an input (eg at boot)
	   then it is low */
	fprintf(stderr, "pactl %02X padr %02X\n", cpu->io.pactl, cpu->io.padr);

	if (!(cpu->io.pactl & 0x08))
		bits &= 0xF7;
	/* We should check for OC/IC function and blow up messily
	   if set */
	flatahigh = 0;
	if (bits & 0x40)
		flatahigh += 0x10000;
	if (bits & 0x20)
		flatahigh += 0x20000;
	if (bits & 0x10)
		flatahigh += 0x40000;
	if (bits & 0x08)
		flatahigh += 0x80000;
}


/* I/O ports */

void m6800_port_output(struct m6800 *cpu, int port)
{
	/* Port A is the flat model A16-A20 */
	if (port == 1)
		flatarecalc(cpu);
	if (sdcard && port == 4) {
		if (cpu->io.pddr & 0x20)
			sd_spi_raise_cs(sdcard);
		else
			sd_spi_lower_cs(sdcard);
	}
}

uint8_t m6800_port_input(struct m6800 *cpu, int port)
{
	if (port == 5)
		return 0x00;
	return 0xFF;
}

void m68hc11_port_direction(struct m6800 *cpu, int port)
{
	flatarecalc(cpu);
}

static uint8_t spi_rxbyte;

/* Should fix this to model whether D bit 5 is assigned as GPIO */

void m68hc11_spi_begin(struct m6800 *cpu, uint8_t val)
{
	spi_rxbyte = 0xFF;
	if (sdcard) {
		if (trace & TRACE_SPI)
			fprintf(stderr, "SPI -> %02X\n", val);
		spi_rxbyte = sd_spi_in(sdcard, val);
		if (trace & TRACE_SPI)
			fprintf(stderr, "SPI <- %02X\n", spi_rxbyte);
	}
}

uint8_t m68hc11_spi_done(struct m6800 *cpu)
{
	return spi_rxbyte;
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

/* Real time clock state machine and related state.

   Give the host time and don't emulate time setting except for
   the 24/12 hour setting.

 */

uint8_t m6800_inport(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read(rtcdev);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void m6800_outport(uint8_t addr, uint8_t val)
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
	} else if (addr == 0xFC)
		protlow = val;
	else if (addr == 0xFB)
		prothi = val;
	else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

uint8_t m6800_read_op(struct m6800 *cpu, uint16_t addr, int debug)
{
	if (addr >> 8 == 0xFE) {
		if (debug)
			return 0xFF;
		return m6800_inport(addr & 0xFF);
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
		if (!debug && (trace & TRACE_MEM)) {
			fprintf(stderr, "R %04X[%02X] = %02X\n",
				(unsigned int)addr,
				(unsigned int)higha,
				(unsigned int)val);
		}
		return val;
	} else 	if (bankenable) {
		unsigned int bank = (addr & 0xC000) >> 14;
		if (!debug && (trace & TRACE_MEM))
			fprintf(stderr, "R %04x[%02X] = %02X\n", addr, (unsigned int) bankreg[bank], (unsigned int) ramrom[(bankreg[bank] << 14) + (addr & 0x3FFF)]);
		addr &= 0x3FFF;
		return ramrom[(bankreg[bank] << 14) + addr];
	} else if (bankflat) {
		if (!debug && (trace & TRACE_MEM))
			fprintf(stderr, "R [%02X]%04x = %02X\n", flatahigh >> 16, addr, (unsigned int) ramrom[flatahigh + addr]);
		return ramrom[flatahigh + addr];
	}
	if (!debug && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

uint8_t m6800_debug_read(struct m6800 *cpu, uint16_t addr)
{
	return m6800_read_op(cpu, addr, 1);
}

uint8_t m6800_read(struct m6800 *cpu, uint16_t addr)
{
	return m6800_read_op(cpu, addr, 0);
}


void m6800_write(struct m6800 *cpu, uint16_t addr, uint8_t val)
{
	if (addr >> 8 == 0xFE) {
		m6800_outport(addr & 0xFF, val);
		return;
	}
	if (protlow && addr >> 8 >= protlow && addr >> 8 < prothi) {
		fprintf(stderr, "MMU fault %04x %04x\n",
			cpu->pc, addr);
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
	} else if (bankflat) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W [%02X]%04x = %02X\n", flatahigh >> 16, (unsigned int) addr, (unsigned int) val);
		if (flatahigh >= 0x80000)
			ramrom[flatahigh + addr] = val;
		/* ROM writes go nowhere */
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	} else if (bankflat) {
	} else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X = %02X\n", addr, val);
		if (addr < 32768 && !bank512)
			ramrom[addr] = val;
		else if (trace & TRACE_MEM)
			fprintf(stderr, "[Discarded: ROM]\n");
	}
}

static void poll_irq_event(void)
{
	/* For now only internal IRQ */
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
	fprintf(stderr, "rc2014-68hc11: [-b] [-B] [-F] [-f] [-R] [-r rom] [-i idedisk] [-S sdcard] [-m monitor] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	char *rompath = "rc2014-68hc11.rom";
	char *monpath = NULL;
	char *idepath;
	char *sdpath = NULL;
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "1abBd:Ffi:I:r:RS:m:w")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'b':
			bank512 = 1;
			bankhigh = 0;
			bankflat = 0;
			rom = 0;
			break;
		case 'B':
			bankhigh = 1;
			bank512 = 0;
			bankflat = 0;
			rom = 0;
			break;
		case 'F':
			bankhigh = 0;
			bank512 = 0;
			bankflat = 1;
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
		case 'm':
			monpath = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (rom == 0 && bank512 == 0 && bankhigh == 0 && bankflat == 0) {
		fprintf(stderr, "rc2014-68hc11: no ROM\n");
		exit(EXIT_FAILURE);
	}

	if (rom) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom + 32768, 32768) != 32768) {
			fprintf(stderr, "rc2014-68hc11: short rom '%s'.\n", rompath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}
	if (monpath) {
		fd = open(monpath, O_RDONLY);
		if (fd == -1) {
			perror(monpath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, monitor, 12288) != 12288) {
			fprintf(stderr, "rc2014-68hc11: short monitor '%s'.\n", monpath);
			exit(EXIT_FAILURE);
		}
		close(fd);
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

	if (bank512 || bankhigh || bankflat) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom, 524288) != 524288) {
			fprintf(stderr, "rc2014-68hc11: banked rom image should be 512K.\n");
			exit(EXIT_FAILURE);
		}
		if (bank512 || bankhigh)
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

	/* 68HC11E variants */
	if (monpath) /* 68HC11E9 with EEROM and ROM */
		m68hc11e_reset(&cpu, 9, 0x03, monitor, eerom);
	else	/* 68HC11E0 for now */
		m68hc11e_reset(&cpu, 0, 0, NULL, NULL);

	if (trace & TRACE_CPU)
		cpu.debug = 1;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i;
		unsigned int j;
		/* 36400 T states for base RC2014 - varies for others */
		for (j = 0; j < 10; j++) {
			for (i = 0; i < 10; i++) {
				while(cycles < clockrate)
					cycles += m68hc11_execute(&cpu);
				cycles -= clockrate;
			}
			/* Drive the internal serial */
			i = check_chario();
			if (i & 1)
				m68hc11_rx_byte(&cpu, next_char());
			if (i & 2)
				m68hc11_tx_done(&cpu);
		}
		/* Wiznet timer */
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
