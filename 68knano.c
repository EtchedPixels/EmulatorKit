/*
 *	Emulator for Matt Sarnoff's 68Knano.
 *
 *	We emulate at about 10MHz. The serial doesn't actually care
 *	about real baud rates.
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
#include "16x50.h"
#include "ds3234.h"
#include "ide.h"

/* IDE controller */
static struct ide_controller *ide;
/* Serial */
static struct uart16x50 *uart;
/* RTC */
static struct ds3234 *ds3234;

/* 1MB RAM */
static uint8_t ram[0x100000];
/* 64K ROM */
static uint8_t rom[0x10000];
static int trace = 0;

#define TRACE_MEM	1
#define TRACE_CPU	2
#define TRACE_UART	4
#define TRACE_RTC	8

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

static uint8_t msr_lines;

/* The output modem lines may have shifted */
void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	static uint8_t last_mcr;
	static uint8_t rxbyte, txbyte;
	static uint8_t bitcount;
	uint8_t delta = last_mcr ^ mcr;
	last_mcr = mcr;

	/* Work out what this did to the SPI */
	/* DTR is MOSI - we don't care about it directly */
	/* RTS is the LED - again we don't care until we add SD */
	/* OUT2 is nSS - but inverted */
	if (delta & MCR_OUT2)
		ds3234_spi_cs(ds3234, !(mcr & MCR_OUT2));
	/* OUT1 is the SPI clock */
	if (delta & MCR_OUT1) {
		if (mcr & MCR_OUT1) {
			/* Falling edge as sen by SPI. Values change */
			/* Load */
			msr_lines &= ~MSR_DCD;
			/* Inverted signal */
			if (!(txbyte & 0x80))
				msr_lines |= MSR_DCD;
			txbyte <<= 1;
			if (trace & TRACE_RTC)
				fprintf(stderr, "spi: clock fall - MSR %02x\n", msr_lines);
			uart16x50_signal_event(uart, msr_lines);
		} else {
			/* Rising edge: Values sample */
			rxbyte <<= 1;
			rxbyte |= !(mcr & MCR_DTR);
			bitcount++;
			if (trace & TRACE_RTC)
				fprintf(stderr, "spi: clock rise - MCR %02x\n", mcr);
			if (bitcount == 8) {
				txbyte = ds3234_spi_rxtx(ds3234, rxbyte);
				bitcount = 0;
			}
		}
	}
}

/* We model this as if the clock is always on. Should really be checking
   the DS3234 state etc */
static void sqw_toggle(void)
{
	msr_lines ^= MSR_DSR;
	uart16x50_signal_event(uart, msr_lines);
}

static unsigned int irq_pending;

void recalc_interrupts(void)
{
	/*  UART autovector 1 */
	if (uart16x50_irq_pending(uart))
		m68k_set_irq(M68K_IRQ_1);
	else
		m68k_set_irq(0);
}

int cpu_irq_ack(int level)
{
	return M68K_INT_ACK_AUTOVECTOR;
}

/* Read data from RAM, ROM, or a device */
unsigned int do_cpu_read_byte(unsigned int address, unsigned int trap)
{
	address &= 0xFFFFFF;
	uint16_t r;
	switch(address >> 20 ) {
	case 0x00:	/* ROM */
	case 0x02:
		return rom[address & 0xFFFF];
	case 0x08:	/* Open bus */
		return 0xFF;
	case 0x09:
		r = ide_read16(ide, (address & 0x0E) >> 1);
		if (!(address & 1))
			r >>= 8;
		return r;
	case 0x0A:
		return uart16x50_read(uart, (address & 0x1E) >> 1);
	case 0x0C:
	case 0x0E:
		return ram[address & 0xFFFFF];
	default:
		if (trap) {
			fprintf(stderr, "0x%06X address conflict.\n", address);
			exit(1);
		}
		return 0xFF;	/* For disassembly */
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
	/* Special case the ide as it matters */
	if ((address & 0xF00000) == 0x900000) {
		return ide_read16(ide, (address & 0x0E) >> 1);
	}
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
	address &= 0xFFFFFF;
	switch(address >> 20) {
	case 0x00:	/* ROM */
	case 0x02:
		if (trace & TRACE_MEM)
			fprintf(stderr,  "%06x: write to ROM.\n", address);
		return;
	case 0x08:	/* Open bus */
		return;
	case 0x09:
		if (!(address & 1))
			value >>= 8;
		ide_write16(ide, (address & 0x0E) >> 1, value);
		return;
	case 0x0A:
		uart16x50_write(uart, (address & 0x1E) >> 1, value);
		return;
	case 0x0C:
	case 0x0E:
		ram[address & 0xFFFFF] = value;
		return;
	default:
		fprintf(stderr, "0x%06X address conflict.\n", address);
		exit(1);
	}
}

void cpu_write_word(unsigned int address, unsigned int value)
{
	address &= 0xFFFFFF;

	if (trace & TRACE_MEM)
		fprintf(stderr, "WW %06X <- %04X\n", address, value);

	/* Special case the ide as it matters */
	if ((address & 0xF00000) == 0x900000) {
		ide_write16(ide, (address & 0x0E) >> 1, value);
		return;
	}
	cpu_write_byte(address, value >> 8);
	cpu_write_byte(address + 1, value & 0xFF);
}

void cpu_write_long(unsigned int address, unsigned int value)
{
	address &= 0xFFFFFF;

	cpu_write_word(address, value >> 16);
	cpu_write_word(address + 2, value & 0xFFFF);
}

void cpu_write_pd(unsigned int address, unsigned int value)
{
	address &= 0xFFFFFF;

	cpu_write_word(address + 2, value & 0xFFFF);
	cpu_write_word(address, value >> 16);
}

void cpu_instr_callback(void)
{
	if (trace & TRACE_CPU) {
		char buf[128];
		unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
		m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
		fprintf(stderr, ">%06X %s\n", pc, buf);
	}
}

static void device_init(void)
{
	irq_pending = 0;
	ide_reset_begin(ide);
	uart16x50_reset(uart);
	uart16x50_attach(uart, &console);
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
	fprintf(stderr, "68knano: [-0][-1][-2][-e][-r rompath][-i idepath][-d debug].\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd;
	int cputype = M68K_CPU_TYPE_68000;
	int fast = 0;
	int opt;
	const char *romname = "68knano.rom";
	const char *diskname = "68knano.ide";

	while((opt = getopt(argc, argv, "012efd:i:r:")) != -1) {
		switch(opt) {
		case '0':
			cputype = M68K_CPU_TYPE_68000;
			break;
		case '1':
			cputype = M68K_CPU_TYPE_68010;
			break;
		case '2':
			cputype = M68K_CPU_TYPE_68020;
			break;
		case 'e':
			cputype = M68K_CPU_TYPE_68EC020;
			break;
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
	if (read(fd, rom, 0x10000) < 0x10000) {
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

	uart = uart16x50_create();
	if (trace & TRACE_UART)
		uart16x50_trace(uart, 1);
	uart16x50_set_clock(uart, 12000000);

	ds3234 = ds3234_create();
	ds3234_trace(ds3234, trace & TRACE_RTC);

	m68k_init();
	m68k_set_cpu_type(cputype);
	m68k_pulse_reset();

	/* Init devices */
	device_init();

	while (1) {
		unsigned n = 0;
		while(n++ < 5000) {
			/* A 12MHz 68000 should do 1200 cycles per 0.1ms
			   We do a blind 0.01ns second sleep so we are actually
			   emulating a bit under 12Mhz - which will do fine for
			   testing this stuff */
			m68k_execute(1200);
			uart16x50_event(uart);
			recalc_interrupts();
			if (!fast)
				take_a_nap();
		}
		/* Toggle SQW at 1Hz (so two toggles a second) */
		sqw_toggle();
	}
}
