#include <stdio.h>
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
#include <arpa/inet.h>
#include "ide.h"
#include "duart.h"

/* 16MB RAM except for the top 32K which is I/O */

static uint8_t ram[(16 << 20) - 32768];
/* IDE controller */
static struct ide_controller *ide;
/* 68681 */
static struct duart *duart;
static int rcbus;

static int trace = 0;

#define TRACE_MEM	1
#define TRACE_CPU	2
#define TRACE_DUART	4

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

static unsigned int irq_pending;

void recalc_interrupts(void)
{
	int i;

	/* Duart on IPL1 */
	if (duart_irq_pending(duart))
		irq_pending |= (1 << 2);
	else
		irq_pending &= ~(1 << 2);

	if (irq_pending) {
		for (i = 7; i >= 0; i--) {
			if (irq_pending & (1 << i)) {
				m68k_set_irq(i);
				return;
			}
		}
	} else
		m68k_set_irq(0);
}

/* Until we plug any RC2014 emulation into this */

static uint8_t rcbus_inb(uint8_t address)
{
	return 0xFF;
}

static void rcbus_outb(uint8_t address, uint8_t data)
{
}



int cpu_irq_ack(int level)
{
	if (!(irq_pending & (1 << level)))
		return M68K_INT_ACK_SPURIOUS;
	if (level == 2)
		return duart_vector(duart);
	return M68K_INT_ACK_SPURIOUS;
}


/* TODO v2 board added an RTC */

static unsigned int do_io_readb(unsigned int address)
{
	if (rcbus && address >= 0xFF8000 && address <= 0xFF8FFF)
		return rcbus_inb(((address - 0xFF8000) >> 1) & 0xFF);
	/* SPI is not modelled */
	if (address >= 0xFFD000 && address <= 0xFFDFFF)
		return 0xFF;
	/* ATA CF */
	/* FIXME: FFE010-01F sets CS1 */
	if (address >= 0xFFE000 && address <= 0xFFEFFF)
		return ide_read8(ide, (address & 31) >> 1);
	/* DUART */
	return duart_read(duart, address & 31);
}

static void do_io_writeb(unsigned int address, unsigned int value)
{
	if (address == 0xFFFFFF) {
		printf("<%c>", value);
		return;
	}
	if (rcbus && address >= 0xFF8000 && address <= 0xFF8FFF) {
		rcbus_outb(((address - 0xFF8000) >> 1) & 0xFF, value);
		return;
	}
	/* SPI is not modelled */
	if (address >= 0xFFD000 && address <= 0xFFDFFF)
		return;
	/* ATA CF */
	if (address >= 0xFFE000 && address <= 0xFFEFFF) {
		ide_write8(ide, (address & 31) >> 1, value);
		return;
	}
	/* DUART */
	duart_write(duart, address & 31, value);
}

/* Read data from RAM, ROM, or a device */
unsigned int do_cpu_read_byte(unsigned int address)
{
	address &= 0xFFFFFF;
	if (rcbus) {
		/* Musashi can't emulate this properly it seems */
		if (address >= 0x800000 && address <= 0xFF8000) {
			fprintf(stderr, "R: bus error at %d\n", address);
			return 0xFF;
		}
		if (address <= 0xFF8000)
			address &= 0x1FFFFF;
	}
	if (address < sizeof(ram))
		return ram[address];
	return do_io_readb(address);
}

unsigned int cpu_read_byte(unsigned int address)
{
	unsigned int v = do_cpu_read_byte(address);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RB %06X -> %02X\n", address, v);
	return v;
}


unsigned int do_cpu_read_word(unsigned int address)
{
	address &= 0xFFFFFF;

	if (rcbus) {
		if (address >= 0x800000 && address < 0xFF8000) {
			fprintf(stderr, "R: bus error at %d\n", address);
			return 0xFFFF;
		}
		/* RAM wraps four times */
		if (address <= 0xFF8000)
			address &= 0x1FFFFF;
	}
	if (address < sizeof(ram) - 1)
		return READ_WORD(ram, address);
	else if (address >= 0xFFE000 && address <= 0xFFEFFF)
		return ide_read16(ide, (address & 31) >> 1);
	return (do_cpu_read_byte(address) << 8) | do_cpu_read_byte(address + 1);
}

unsigned int cpu_read_word(unsigned int address)
{
	unsigned int v = do_cpu_read_word(address);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RW %06X -> %04X\n", address, v);
	return v;
}

unsigned int cpu_read_word_dasm(unsigned int address)
{
	if (address < 0xFF7FFF)
		return cpu_read_word(address);
	else
		return 0xFFFF;
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

	if (trace & TRACE_MEM)
		fprintf(stderr, "WB %06X <- %02X\n", address, value);

	if (rcbus) {
		if (address >= 0x800000 && address <= 0xFF8000) {
			fprintf(stderr, "W: bus error at %06X\n", address);
			return;
		}
		if (address <= 0xFF8000)
			address &= 0x1FFFFF;
	}
	if (address < sizeof(ram))
		ram[address] = value;
	else
		do_io_writeb(address, (value & 0xFF));
}

void cpu_write_word(unsigned int address, unsigned int value)
{
	address &= 0xFFFFFF;

	if (trace & TRACE_MEM)
		fprintf(stderr, "WW %06X <- %04X\n", address, value);

	if (rcbus) {
		if (address >= 0x800000 && address <= 0xFF8000) {
			fprintf(stderr, "W: bus error at %06X\n", address);
			return;
		}
		if (address <= 0xFF8000)
			address &= 0x1FFFFF;
	}
	if (address < sizeof(ram) - 1) {
		WRITE_WORD(ram, address, value);
	} else if (address >= 0xFFE000 && address <= 0xFFEFFF)
		ide_write16(ide, (address & 31) >> 1, value);
	else {
		/* Corner cases */
		cpu_write_byte(address, value >> 8);
		cpu_write_byte(address + 1, value & 0xFF);
	}
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
	duart_reset(duart);
	duart_set_input(duart, 1);
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
	fprintf(stderr, "tiny68k [-0][-1][-2][-e][-R][-r rompath][-i idepath][-d debug].\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd;
	int cputype = M68K_CPU_TYPE_68000;
	int fast = 0;
	int opt;
	const char *romname = "tiny68k.rom";
	const char *diskname = "tiny68k.ide";

	while((opt = getopt(argc, argv, "012eRfd:i:r:")) != -1) {
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
		case 'R':
			rcbus = 1;
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
	if (read(fd, ram, 0x8000) < 0x1000) {
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

	duart = duart_create();
	if (trace & TRACE_DUART)
		duart_trace(duart, 1);

	m68k_init();
	m68k_set_cpu_type(cputype);
	m68k_pulse_reset();

	/* Init devices */
	device_init();

	while (1) {
		/* A 10MHz 68000 should do 1000 cycles per 1/10000th of a
		   second. We do a blind 0.01 second sleep so we are actually
		   emulating a bit under 10Mhz - which will do fine for
		   testing this stuff */
		m68k_execute(1000);
		duart_tick(duart);
		if (!fast)
			take_a_nap();
	}
}
