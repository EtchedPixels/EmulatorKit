/*
 *	Arnewsh Inc SBC08K
 *
 *	68008 @ 8 or 10MHz
 *	68230 PIT
 *	68681 DUART
 *	Up to 128K RAM in U5-U8, ROM in U4
 *
 *	Console on DUART port 0
 *
 *	TODO:
 *	-	Emulate PIT better
 *	-	Option for 74S138 mode
 *	-	8/10MHz CPU select
 *	-	RAM expansion emulation
 *
 *
 *	Memory map (74S138) : not currently emulated
 *	00000-01FFF RAM		U8
 *	02000-03FFF RAM/ROM	U7
 *	04000-05FFF RAM/ROM	U6
 *	06000-07FFF RAM		U5
 *	08000-09FFF ROM		U4
 *	0A000-0BFFF Unused
 *	0C000-0DFFF DUART
 *	0E000-0FFFF PIT
 *	10000-FFFFF Free
 *
 *	Memory map (PAL)
 *	00000-07FFF RAM	U8
 *	08000-0FFFF RAM	U7
 *	10000-17FFF RAM	U6
 *	18000-1FFFF RAM	U5
 *	20000-DFFFF Free
 *	E0000-EFFFF Tutor monitor (boot ROM)
 *	F0000-FEFFF Unused
 *	FF000-FF7FF PIT
 *	FF800-FFFFF DUART
 *
 *	As is typical the ROM appears low for the first 8 read cycles then the counter
 *	maps it high only.
 */

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
#include "68230.h"

static uint8_t ram[1048576];	/* 20bit addres bus so just allocate for all of it */
/* 68681 */
static struct duart *duart;
/* 68230 */
static struct m68230 *pit;
/* IDE on the 230 */
static struct ide_controller *ide;

static uint8_t rcount;		/* Counter for the first 8 fetches */

static int trace = 0;

#define TRACE_MEM	1
#define TRACE_CPU	2
#define TRACE_DUART	4
#define TRACE_PIT	8

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

static uint8_t ide_datav, ide_ctrl;

/* Emulate a 68230 with a CF adapter wired data to port A control to port B
   0-2: address bits 3: /CS 4: /CS 5: W 6: R 7: reset */
void m68230_write_port(struct m68230 *pit, unsigned port, uint8_t val)
{
	if (port == 0)
		ide_datav = val;
	if (port == 1) {
		uint8_t delta = ide_ctrl ^ val;
		unsigned port = val & 7;
		ide_ctrl = val;
		if (!(val & 0x80)) {	/* RESET */
			ide_reset_begin(ide);
			return;
		}
		/* No CS selected */
		if ((val & 0x18) == 0x18)
			return;
		if (val & 0x08)		/* CS for alt reg low, CS main high*/
			port += 2;
		/* Rising write edge */
		if (delta & val & 0x20) {
			if (trace & TRACE_DUART)
				fprintf(stderr, "\\WR %d <- %02X\n", port, ide_datav);
			ide_write8(ide, port, ide_datav);
		}
		/* Read edge going low */
		if ((delta & 0x40) && !(val & 0x40)) {
			ide_datav = ide_read8(ide, port);
			if (trace & TRACE_DUART)
				fprintf(stderr, "\\RD %d -> %02X\n", port, ide_datav);
		}
	}
}

uint8_t m68230_read_port(struct m68230 *pit, unsigned port)
{
	if (port == 1)
		return ide_ctrl;
	if (trace & TRACE_DUART)
		fprintf(stderr, "R ide %02X\n", ide_datav);
	return ide_datav;
}

static unsigned int irq_pending;

void recalc_interrupts(void)
{
	int i;

	/* Duart on IPL1 */
	if (duart_irq_pending(duart))
		irq_pending |= (1 << 5);
	else
		irq_pending &= ~(1 << 5);

	if (m68230_port_irq_pending(pit))
		irq_pending |= (1 << 2);
	else
		irq_pending &= ~(1 << 2);

	if (m68230_timer_irq_pending(pit))
		irq_pending |= (1 << 7);
	else
		irq_pending &= ~(1 << 7);

	/* TODO : emulate an abort button causing 7 */
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

int cpu_irq_ack(int level)
{
	if (!(irq_pending & (1 << level)))
		return M68K_INT_ACK_SPURIOUS;
	if (level == 2)
		return m68230_timer_vector(pit);
	if (level == 5)
		return duart_vector(duart);
	if (level == 2)
		return m68230_port_vector(pit);
	return M68K_INT_ACK_SPURIOUS;
}

void duart_signal_change(struct duart *d, uint8_t opr)
{
}

/* Read data from RAM, ROM, or a device */
unsigned int do_cpu_read_byte(unsigned int address)
{
	address &= 0xFFFFF;
	if (rcount < 8) {
		rcount++;
		return ram[(address & 0x3FFFF) + 0xE0000];
	}
	if (address < 131072)
		return ram[address];
	/* ROM */
	if (address >= 0xE0000 && address <= 0xEFFFF)
		return ram[address];
	if (address >= 0xFF800)
		return duart_read(duart, address >> 1);
	if (address >= 0xFF000)
		return m68230_read(pit, address >> 1);
	return 0xFF;
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
	if (address < 0xFF000)
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
	address &= 0xFFFFF;

	if (trace & TRACE_MEM)
		fprintf(stderr, "WB %06X <- %02X\n", address, value);

	if (address < 131072)	/* 128K modelled */
		ram[address] = value;
	else if (address >= 0xFF800 && address <= 0xFFFFF)
		duart_write(duart, address >> 1, value);
	else if (address >= 0xFF000 && address < 0xFF800)
		m68230_write(pit, address >> 1, value);
}

void cpu_write_word(unsigned int address, unsigned int value)
{
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
	ide_reset_begin(ide);
	duart_reset(duart);
	duart_set_input(duart, 1);
	m68230_reset(pit);
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
	fprintf(stderr, "sbc08k [-r rompath] [-f] [-d debug].\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd;
	int cputype = M68K_CPU_TYPE_68000;
	int fast = 0;
	int opt;
	const char *romname = "Tutor131.bin";
	const char *diskname = NULL;

	while((opt = getopt(argc, argv, "i:r:d:f")) != -1) {
		switch(opt) {
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
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
	/* copying the image is fine as this is read only space */
	switch(read(fd, ram + 0xE0000, 0x10000)) {
	case 0x10000:
		break;
	case 0x01000:
		memcpy(ram + 0xE2000, ram + 0xE0000, 0x2000);
	case 0x02000:
		memcpy(ram + 0xE2000, ram + 0xE0000, 0x2000);
	case 0x04000:
		memcpy(ram + 0xE4000, ram + 0xE0000, 0x4000);
		/* Fall through */
	case 0x8000:
		memcpy(ram + 0xE8000, ram + 0xE0000, 0x8000);
		break;
	default:
		fprintf(stderr, "%s: unsupported size.\n", romname);
		exit(1);
	}
	close(fd);

	if (diskname) {
		fd = open(diskname, O_RDWR);
		if (fd == -1) {
			perror(diskname);
			exit(1);
		}
	}
	ide = ide_allocate("hd0");
	if (ide == NULL)
		exit(1);
	if (diskname && ide_attach(ide, 0, fd))
		exit(1);

	duart = duart_create();
	if (trace & TRACE_DUART)
		duart_trace(duart, 1);

	pit = m68230_create();
	if (trace & TRACE_PIT)
		m68230_trace(pit, 1);

	m68k_init();
	m68k_set_cpu_type(cputype);
	m68k_pulse_reset();

	/* Init devices */
	device_init();

	while (1) {
		/* A 10MHz 68008 should do 1000 cycles per 1/10000th of a
		   second. We do a blind 0.01 second sleep so we are actually
		   emulating a bit under 10Mhz - which will do fine for
		   testing this stuff */
		m68k_execute(600);	/* We don't have an 008 emulation so approx the timing */
		duart_tick(duart);
		m68230_tick(pit, 1000);
		if (!fast)
			take_a_nap();
	}
}
