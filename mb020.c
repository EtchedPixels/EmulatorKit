/*
 *	Bill Shen's 68020 mainboard with RCbus
 *
 *	This is a fairly simple flat system with a mapping quirk
 *
 *	On boot
 *	0x00000000-0x03FFFFFF	RC2014 bus 8bit
 *	0x04000000-0x0BFFFFFF	Up to 32MB of 60ns EDO/FP SIMM, zero wait
 *	0xFFFFF000-0xFFFFFFFF	RC2014 I/O space 4 wait, only low 12 address lines
 *
 *	Swap register
 *	Poking FFFF8000 read or write swaps the 00000000-03FFFFFF range and
 *	04000000-07FFFFFF over. It can't be undone.
 *
 *	Interrupts
 *	- RC2014 autovectors for 100Hz timer and for 6850 and UART
 *
 *	The swap is used by the standard monitor
 *
 *	Clocked at 22MHz
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <m68k.h>
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"
#include "16x50.h"
#include "ide.h"
#include "rtc_bitbang.h"

/* CF adapter */
static struct ide_controller *ide;
/* Serial */
static struct acia *acia;
static struct uart16x50 *uart;
/* RTC */
static struct rtc *rtc;
/* Has the flip latch been set */
static unsigned flipped = 0;

/* 16MB RAM */
static uint8_t ram[0x1000000];
/* 8K ROM */
static uint8_t rom[0x2000];
static int trace = 0;

static unsigned irq_pending;
static unsigned timer;

#define TRACE_MEM	1
#define TRACE_CPU	2
#define TRACE_UART	4
#define TRACE_RTC	8
#define TRACE_IDE	16

uint8_t fc;

/* Read/write macros */
#define READ_BYTE(BASE, ADDR) (BASE)[ADDR]
#define READ_WORD(BASE, ADDR) (((BASE)[ADDR]<<8) | \
			(BASE)[(ADDR)+1])
#define READ_LONG(BASE, ADDR) (((BASE)[ADDR]<<24) | \
			((BASE)[(ADDR)+1]<<16) | \
			((BASE)[(ADDR)+2]<<8) | \
			(BASE)[(ADDR)+3])

#define WRITE_BYTE(BASE, ADDR, VAL) (BASE)[ADDR] = (VAL)&0xff
#define WRITE_WORD(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>8) & 0xff; \
			(BASE)[(ADDR)+1] = (VAL)&0xff
#define WRITE_LONG(BASE, ADDR, VAL) (BASE)[ADDR] = ((VAL)>>24) & 0xff; \
			(BASE)[(ADDR)+1] = ((VAL)>>16)&0xff; \
			(BASE)[(ADDR)+2] = ((VAL)>>8)&0xff; \
			(BASE)[(ADDR)+3] = (VAL)&0xff

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

	if (select(2, &i, &o, NULL, &tv) == -1) {
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
	return c;
}

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t msr)
{
}

void recalc_interrupts(void)
{
	/*  UART autovector 1 */
	unsigned p = 0;
	if (timer)
		p |= 1;
	if (acia_irq_pending(acia))
		p |= 2;
	if (uart16x50_irq_pending(uart))
		p |= 2;
	m68k_set_irq(p);
}

int cpu_irq_ack(int level)
{
	if (level == 1)
		timer = 0;
	return M68K_INT_ACK_AUTOVECTOR;
}

/* Read data from RAM, ROM, or a device */
unsigned int do_cpu_read_byte(unsigned int address, unsigned int trap)
{
	if (!flipped) {
		/* Model the standard 8K ROM config */
		if (address < 0x04000000)
			return rom[address & 0x1FFF];
		if (address < 0x0C000000)
			return ram[address & (sizeof(ram) - 1)];
	} else {
		if (address < 0x04000000)
			return ram[address & (sizeof(ram) -1)];
		if (address < 0x08000000)
			return rom[address & 0x1FFF];
		if (address < 0x0C000000)
			return ram[address & (sizeof(ram) - 1)];
	}
	if (address == 0xFFFF8000)
		flipped = 1;
	if ((address & 0xFFFFF000) == 0xFFFFF000) {
		address &= 0xFF;
		if (address == 0x0C)
			return rtc_read(rtc);
		if (address >= 0x10 && address <= 0x17) {
			uint8_t r = ide_read8(ide, address & 7);
			if (trace & TRACE_IDE)
				fprintf(stderr, "ide: R %02X -> %02X\n",
					address, r);
			return r;
		}
		if (address >= 0x80 && address <= 0x87)
			return acia_read(acia, address & 1);
		if (address >= 0xC0 && address <= 0xC7)
			return uart16x50_read(uart, address & 7);
	}
	return 0xFF;
}

unsigned int cpu_read_byte(unsigned int address)
{
	unsigned int v = do_cpu_read_byte(address, 1);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RB %06X -> %02X\n", address, v);
	return v;
}

unsigned int do_cpu_read_word(unsigned int address, unsigned int trap)
{
	return (do_cpu_read_byte(address, trap) << 8) | do_cpu_read_byte(address + 1, trap);
}

unsigned int cpu_read_word(unsigned int address)
{
	unsigned int v = do_cpu_read_word(address, 1);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RW %06X -> %04X\n", address, v);
	return v;
}

unsigned int cpu_read_word_dasm(unsigned int address)
{
	return do_cpu_read_word(address, 0);
}

unsigned int cpu_read_long(unsigned int address)
{
	return (cpu_read_word(address) << 16) | cpu_read_word(address + 2);
}

unsigned int cpu_read_long_dasm(unsigned int address)
{
	return (cpu_read_word_dasm(address) << 16) | cpu_read_word_dasm(address + 2);
}

void cpu_write_byte(unsigned int address, unsigned int value)
{
	if (!flipped) {
		/* Model the standard 8K ROM config */
		if (address < 0x04000000) {
			fprintf(stderr, "%x: write to ROM\n", address);
			return;
		}
		if (address < 0x0C000000) {
			ram[address & (sizeof(ram) - 1)] = value;
			return;
		}
	} else {
		if (address < 0x04000000) {
			ram[address & (sizeof(ram) - 1)] = value;
			return;
		}
		if (address < 0x08000000) {
			fprintf(stderr, "%x: write to ROM\n", address);
			return;
		}
		if (address < 0x0C000000) {
			ram[address & (sizeof(ram) - 1)] = value;
			return;
		}
	}
	if (address == 0xFFFF8000)
		flipped = 1;
	if ((address & 0xFFFFF000) == 0xFFFFF000) {
		address &= 0xFF;
		if (address == 0x0C) {
			rtc_write(rtc, value);
			return;
		}
		if (address >= 0x10 && address <= 0x17) {
			if (trace & TRACE_IDE)
				fprintf(stderr, "ide: W %02X = %02X\n",
					address, value);
			ide_write8(ide, address & 7, value);
			return;
		}
		if (address >= 0x80 && address <= 0x87) {
			acia_write(acia, address & 1, value);
			return;
		}
		if (address >= 0xC0 && address <= 0xC7) {
			uart16x50_write(uart, address & 7, value);
			return;
		}
	}
}

void cpu_write_word(unsigned int address, unsigned int value)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "WW %06X <- %04X\n", address, value);

	cpu_write_byte(address, value >> 8);
	cpu_write_byte(address + 1, value & 0xFF);
}

void cpu_write_long(unsigned int address, unsigned int value)
{
	cpu_write_word(address, value >> 16);
	cpu_write_word(address + 2, value & 0xFFFF);
}

void cpu_write_pd(unsigned int address, unsigned int value)
{
	cpu_write_word(address + 2, value & 0xFFFF);
	cpu_write_word(address, value >> 16);
}

void cpu_instr_callback(void)
{
	if (trace & TRACE_CPU) {
		char buf[128];
		unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
		m68k_disassemble(buf, pc, M68K_CPU_TYPE_68020);
		fprintf(stderr, ">%06X %s\n", pc, buf);
	}
}

static void device_init(void)
{
	irq_pending = 0;
	ide_reset_begin(ide);
	flipped = 0;
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, 0, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, 0, &saved_term);
}

static void take_a_nap(void)
{
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = 100000;
	if (nanosleep(&t, NULL))
		perror("nanosleep");
}

void cpu_pulse_reset(void)
{
	device_init();
}

void cpu_set_fc(int fc)
{
}

void usage(void)
{
	fprintf(stderr, "mb020: [-1] [-r rompath][-i idepath][-d debug].\n");
	exit(1);
}

#define IN_ACIA		1
#define IN_16X50	2

int main(int argc, char *argv[])
{
	int fd;
	int fast = 0;
	int opt;
	const char *romname = "mb020mon.rom";
	const char *diskname = "mb020.ide";
	unsigned input = IN_ACIA;

	while((opt = getopt(argc, argv, "2efd:i:r:1")) != -1) {
		switch(opt) {
		case 'f':
			fast = 1;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'i':
			diskname = optarg;
			break;
		case 'r':
			romname = optarg;
			break;
		case '1':
			input = IN_16X50;
			break;
		default:
			usage();
		}
	}

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, cleanup);
		signal(SIGTSTP, SIG_IGN);
		term.c_lflag &= ~ICANON;
		term.c_iflag &= ~(ICRNL | IGNCR);
		term.c_cc[VMIN] = 1;
		term.c_cc[VTIME] = 0;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VEOF] = 0;
		term.c_lflag &= ~(ECHO | ECHOE | ECHOK);
		tcsetattr(0, 0, &term);
	}

	if (optind < argc)
		usage();

	memset(ram, 0xA7, sizeof(ram));

	fd = open(romname, O_RDONLY);
	if (fd == -1) {
		perror(romname);
		exit(1);
	}
	if (read(fd, rom, 0x2000) < 0x2000) {
		fprintf(stderr, "%s: too short.\n", romname);
		exit(1);
	}
	close(fd);

	fd = open(diskname, O_RDWR);
	if (fd == -1) {
		perror(diskname);
		exit(1);
	}
	ide = ide_allocate("hd0");
	if (ide == NULL)
		exit(1);
	if (ide_attach(ide, 0, fd))
		exit(1);

	acia = acia_create();
	acia_trace(acia, trace & TRACE_UART);
	if (input == IN_ACIA)
		acia_attach(acia, &console);
	else
		acia_attach(acia, &console_wo);

	uart = uart16x50_create();
	uart16x50_trace(uart, trace & TRACE_UART);
	if (input == IN_16X50)
		uart16x50_attach(uart, &console);
	else
		uart16x50_attach(uart, &console_wo);

	rtc = rtc_create();
	rtc_trace(rtc, trace & TRACE_RTC);

	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68020);
	m68k_pulse_reset();

	/* Init devices */
	device_init();

	while (1) {
		unsigned n = 0;
		/* Do 1/100th of a second of work */
		while(n++ < 100) {
			/* 2200 clocks x 100 for the inner loop gives us
			   220000 clocks */
			m68k_execute(2200);
			acia_timer(acia);
			uart16x50_event(uart);
			recalc_interrupts();
			/* 0.1 ms sleep */
			if (!fast)
				take_a_nap();
		}
		timer = 1;
	}
}
