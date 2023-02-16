/*
 *	A tiny 68K systen
 *	68000 CPU
 *	16-32K ROM
 *	64 or 128K RAM
 *	ACIA with jumpers for baud rate setting
 *	6522 VIA for glue to the outside world
 *
 *	We model the system with 32K/128K and with
 *	the via wired to an SD card. We could model
 *	an i2c rtc or similar but we don't yet do so
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <m68k.h>
#include "acia.h"
#include "6522.h"
#include "sdcard.h"

struct acia *acia;
struct via6522 *via;
struct sdcard *sd;

/* 128K RAM */
static uint8_t ram[0x20000];
/* 32K ROM */
static uint8_t rom[0x8000];
static int trace = 0;
#define TRACE_MEM	1
#define TRACE_CPU	2
#define TRACE_UART	4
#define TRACE_VIA	8
#define TRACE_SD	16

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

/*
 *	VIA callbacks
 */

void via_recalc_outputs(struct via6522 *via)
{
	static unsigned opa;
	static unsigned bitcount;
	static uint8_t rxbyte, txbyte;

	unsigned delta;
	unsigned pa = via_get_port_a(via);
	delta = opa ^ pa;
	opa = pa;

	/* Ok what happened */
	if (delta & 4) {
		if (pa & 4)
			sd_spi_raise_cs(sd);
		else {
			sd_spi_lower_cs(sd);
			bitcount = 0;
		}
	}
	if (delta & 2) {
		if (pa & 2) {
			/* Clock goes high. Sample */
			rxbyte <<= 1;
			rxbyte |= pa & 1;
			bitcount++;
			if (bitcount == 8) {
				txbyte = sd_spi_in(sd, rxbyte);
				if (trace & TRACE_SD)
					fprintf(stderr, "sd: %02X -> %02X\n",
							rxbyte, txbyte);
				bitcount = 0;
			}
		} else {
			via_set_port_a(via, txbyte & 0x80);
			txbyte <<= 1;
		}
	}
}

void via_handshake_a(struct via6522 *via)
{
}

void via_handshake_b(struct via6522 *via)
{
}

/*
 *	CPU glue
 */

static unsigned int irq_pending;

void recalc_interrupts(void)
{
	if (acia_irq_pending(acia) || via_irq_pending(via))
		m68k_set_irq(M68K_IRQ_4);
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
	if (address < 0x10000)
		return rom[address & 0x7FFF];
	if (address < 0x30000)
		return ram[address & 0x1FFFF];
	if (address >= 0x40000)
		return 0xFF;
	if (address & 1)
		return acia_read(acia, (address >> 1) & 1);
	else
		return via_read(via, (address >> 1) & 0x0F);
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
	if (address < 0x10000 || address >= 0x40000)
		return;
	if (address < 0x30000) {
		ram[address & 0x1FFFF] = value;
		return;
	}
	if (address & 1)
		acia_write(acia, (address >> 1) & 1, value);
	else
		via_write(via, (address >> 1 ) & 0x0F, value);
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
		m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
		fprintf(stderr, ">%06X %s\n", pc, buf);
	}
}

static void device_init(void)
{
	irq_pending = 0;
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
	fprintf(stderr, "pico68: [-0][-1][-2][-e][-r rompath][-s sdpath][-d debug].\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd;
	int cputype = M68K_CPU_TYPE_68000;
	int fast = 0;
	int opt;
	const char *romname = "pico68.rom";
	const char *sdname = NULL;

	while((opt = getopt(argc, argv, "012efd:r:s:")) != -1) {
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
		case 'r':
			romname = optarg;
			break;
		case 's':
			sdname = optarg;
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

	fd = open(romname, O_RDONLY);
	if (fd == -1) {
		perror(romname);
		exit(1);
	}
	if (read(fd, rom, 0x8000) < 0x2000) {
		fprintf(stderr, "%s: too short.\n", romname);
		exit(1);
	}
	close(fd);

	acia = acia_create();
	acia_trace(acia,  trace & TRACE_UART);
	acia_set_input(acia, 1);

	via = via_create();
	via_trace(via, trace & TRACE_VIA);

	if (sdname) {
		fd = open(sdname, O_RDWR);
		if (fd == -1) {
			perror(sdname);
			exit(1);
		}
		sd = sd_create("sd0");
		sd_reset(sd);
		sd_attach(sd, fd);
		sd_trace(sd, trace & TRACE_SD);
	}

	m68k_init();
	m68k_set_cpu_type(cputype);
	m68k_pulse_reset();

	/* Init devices */
	device_init();

	while (1) {
		unsigned n = 0;
		while(n++ < 5000) {
			/* 8MHz 68000 */
			m68k_execute(800);
			acia_timer(acia);
			via_tick(via, 800);
			recalc_interrupts();
			if (!fast)
				take_a_nap();
		}
	}
}
