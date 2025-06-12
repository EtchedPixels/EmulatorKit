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
#include "p90ce201.h"

/* Very minimal p90mb emulation. Much left to do */
static uint8_t ram[0x80000];
static uint8_t rom[0x80000];

/* IDE controller */
static struct ide_controller *ide;

static int trace = 0;

#define TRACE_MEM	1
#define TRACE_CPU	2
#define TRACE_CF	4

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

int cpu_irq_ack(int level)
{
	return p90_autovector(level);
}

uint32_t addrmask;
uint32_t addrbits;

void p90_set_aux(uint8_t auxcon, uint8_t aux)
{
	addrmask = auxcon << 16;
	addrbits = aux << 16;
}

static uint32_t pal_modify(uint32_t address)
{
	uint32_t na;
	if (trace & TRACE_MEM)
		fprintf(stderr, "PAL IN %08X PINS ", address);
	/* Allow for GPIO v A16-A23 setup */
	na = address & ~addrmask;
	na |= addrbits & addrmask;
	if (trace & TRACE_MEM)
		fprintf(stderr, "%08X OUT ", na);
	na &= 0x81FFFFFF;
	/* This isn't what the PAL actually uses in logic - FIXME later */
	if (address < 0x01200000 && (na & (1 << 23)))
		na ^= 0x01800000;
	else
		na = address;
	/* Fixing it means shorting the order we do stuff as A23 internal
	   is not GPIO modified */
	if (trace & TRACE_MEM)
		fprintf(stderr, "%08X\n", na);
	return na;
}

static unsigned int do_io_readb(unsigned int address)
{
	if (address >= 0x1200000 && address <= 0x12FFFFF) {
		uint8_t r = ide_read8(ide, address & 15);
		if (trace & TRACE_CF)
			printf("cf read: %x -> %x\n", address & 15, r);
		return r;
	}
	if (address & 0x80000000)
		return p90_read(address);
	return 0xFF;
}

static void do_io_writeb(unsigned int address, unsigned int value)
{
	if (address >= 0x1200000 && address <= 0x12FFFFF) {
		if (trace & TRACE_CF)
			printf("cf write: %x <- %x\n", address & 15, value);
		ide_write8(ide, address & 15, value);
	}
	else if (address & 0x80000000) {
		p90_write(address, value);
		/* Ensure the interrupt status is correct. We have no other
		   IRQ sources */
		m68k_set_irq(p90_interrupts());
	}
}

/* Read data from RAM, ROM, or a device */
unsigned int do_cpu_read_byte(unsigned int address)
{
	address = pal_modify(address);
	if (address >= 0x1200000)
		return do_io_readb(address);
	if (address >= 0x1000000)
		return ram[address & 0x7FFFF];
	return rom[address & 0x7FFFF];
}

unsigned int cpu_read_byte(unsigned int address)
{
	unsigned int v = do_cpu_read_byte(address);
	m68k_modify_timeslice(2);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RB %08X -> %02X\n", address, v);
	return v;
}


unsigned int do_cpu_read_word(unsigned int address)
{
	return (do_cpu_read_byte(address) << 8) | do_cpu_read_byte(address + 1);
}

unsigned int cpu_read_word(unsigned int address)
{
	unsigned int v = do_cpu_read_word(address);
	if (trace & TRACE_MEM)
		fprintf(stderr, "RW %08X -> %04X\n", address, v);
	return v;
}

unsigned int cpu_read_word_dasm(unsigned int address)
{
	return cpu_read_word(address);
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
	if (trace & TRACE_MEM)
		fprintf(stderr, "WB %08X <- %02X\n", address, value);
	m68k_modify_timeslice(2);
	address = pal_modify(address);
	if (address >= 0x1200000)
		do_io_writeb(address, value);
	else if (address >= 0x1000000)
		ram[address & 0x7FFFF] = value;
	else if (trace & TRACE_MEM)
		fprintf(stderr, "***ROM\n");
}

void cpu_write_word(unsigned int address, unsigned int value)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "WW %08X <- %04X\n", address, value);
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
		m68k_disassemble(buf, pc, M68K_CPU_TYPE_68012);
		fprintf(stderr, ">%08X %s\n", pc, buf);
	}
}

static void device_init(void)
{
	ide_reset_begin(ide);
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
	/* 1ms */
	t.tv_nsec = 1000000;
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
	fprintf(stderr, "p90mb [-0][-1][-2][-e][-R][-r rompath][-i idepath][-d debug].\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd;
	/* Not quite right but will do for the moment */
	int cputype = M68K_CPU_TYPE_68012;
	int fast = 0;
	int opt;
	const char *romname = "p90mb.rom";
	const char *diskname = "p90mb.ide";

	while((opt = getopt(argc, argv, "efd:i:r:")) != -1) {
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
	if (read(fd, rom, 0x80000) != 0x80000) {
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

	m68k_init();
	m68k_set_cpu_type(cputype);
	m68k_pulse_reset();

	/* Init devices */
	device_init();

	/* We run at 22Mhz but our performance is nearer that of an 8MHz
	   68000 part so we fudge it by running less cpu cycles than
	   we should to get armwavingly believable performance */
	while (1) {
		/* Per ms we do about 8000 68000 equivalent cycles */
		m68k_execute(8000);
		/* IRQ serial etc and timer stuff - true clock */
		p90_cycles(22000);
		m68k_set_irq(p90_interrupts());
		/* 1ms sleep */
		if (!fast)
			take_a_nap();
	}
}
