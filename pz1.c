/*
 *	Platform features
 *
 *	PZ1 6502 (should be 65C02 when we sort the emulation of C02 out)
 *	I/O via modern co-processor
 *	512KiB RAM, no ROM, boot code inserted by I/O-processor
 *
 *	This is a simple emulation, enough to run Fuzix.
 *	The following is roughly correct:
 *	- bank registers
 *	- serial 0
 *	- idle serial 1
 *	- virtual disk via I/O processor
 *	- Interrupt timer
 *
 *	HW available in real PZ1 but not used in Fuzix:
 *	- top page memory "locked"
 *	- SID-sound
 *	- 50Hz counter
 *	- 60Hz counter
 *	- cpu cycle counter
 *	- display character/graphics/sprite interface
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
#include <sys/select.h>
#include "6502.h"
#include "ide.h"

static uint8_t ram[512 * 1024];		/* 512KiB RAM */
static uint8_t io[256];			/* I/O shadow */

static uint8_t iopage = 0xFE;
static uint8_t hd_fd;
static uint8_t fast;
static uint8_t trunning;

/* Who is pulling on the interrupt line */

static uint8_t live_irq;

#define IRQ_TIMER	1

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_IRQ	4
#define TRACE_UNK	8
#define TRACE_CPU	16

static int trace = 0;

/* IO-ports */
#define PORT_BANK_0           0x00
#define PORT_BANK_1           0x01
#define PORT_BANK_2           0x02
#define PORT_BANK_3           0x03
#define PORT_SERIAL_0_FLAGS   0x10
#define PORT_SERIAL_0_IN      0x11
#define PORT_SERIAL_0_OUT     0x12
#define PORT_SERIAL_1_FLAGS   0x18
#define PORT_SERIAL_1_IN      0x19
#define PORT_SERIAL_1_OUT     0x1A
#define PORT_FILE_CMD         0x60
#define PORT_FILE_PRM_0       0x61
#define PORT_FILE_PRM_1       0x62
#define PORT_FILE_DATA        0x63
#define PORT_FILE_STATUS      0x64
#define PORT_IRQ_TIMER_TARGET 0x80
#define PORT_IRQ_TIMER_COUNT  0x81
#define PORT_IRQ_TIMER_RESET  0x82
#define PORT_IRQ_TIMER_TRIG   0x83
#define PORT_IRQ_TIMER_PAUSE  0x84
#define PORT_IRQ_TIMER_CONT   0x85

#define SERIAL_FLAGS_OUT_FULL  128
#define SERIAL_FLAGS_IN_AVAIL   64
#define FILE_STATUS_OK           0
#define FILE_STATUS_NOK          1

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
		if (errno == EINTR)
			return 0;
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
	if (c == 0x0A)
		c = '\r';
	return c;
}

static void irqnotify(void)
{
	if (live_irq)
		irq6502();
}

static void int_set(int src)
{
	live_irq |= (1 << src);
}

static void int_clear(int src)
{
	live_irq &= ~(1 << src);
}

static uint8_t disk_read(void)
{
	uint8_t c;
	read(hd_fd, &c, 1);
	/* Never any problem reading from file */
	io[PORT_FILE_STATUS] = FILE_STATUS_OK;
	return c;
}

static void disk_write(uint8_t c)
{
	io[PORT_FILE_STATUS] = FILE_STATUS_OK;
	if (write(hd_fd, &c, 1) != 1)
		io[PORT_FILE_STATUS] = FILE_STATUS_NOK;
}

static void disk_seek(void)
{
	/* Seeks to sector (PORT_FILE_PRM_0 + (PORT_FILE_PRM_1 << 8))
	   using 512 byte sectors */
	off_t pos = (io[PORT_FILE_PRM_0] + (io[PORT_FILE_PRM_1] << 8)) << 9;
	if (lseek(hd_fd, pos, SEEK_SET) < 0)
		io[PORT_FILE_STATUS] = FILE_STATUS_NOK;
	else
		io[PORT_FILE_STATUS] = FILE_STATUS_OK;
}

/* All I/O-writes are mirrored in unbanked RAM. Some I/O-reads are returned
   from devices, most are returned from the mirror RAM.
   A very simple way to implement bank register read-back when implemented
   in real hardware. */
uint8_t mmio_read_6502(uint8_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);

	switch(addr) {
	case PORT_SERIAL_0_FLAGS:
		io[addr] = (check_chario() ^ 2) << 6;
		break;
	case PORT_SERIAL_0_IN:
		if (check_chario() & 1)
			io[addr] = next_char();
		else
			io[addr] = 0;
		break;
	case PORT_SERIAL_1_FLAGS:
		io[addr] = 0;
		break;
	case PORT_FILE_DATA:
		io[addr] = disk_read();
		break;
	}
	/* Counters/timers are updated on the fly as they tick */
	return io[addr];
}

void mmio_write_6502(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);

	switch(addr) {
	case PORT_SERIAL_0_OUT:
		write(1, &val, 1);
		break;
	case PORT_FILE_CMD:
		if (val == 0) /* SELECT */
			io[PORT_FILE_STATUS] = FILE_STATUS_OK;
		if (val == 1) /* SEEK */
			disk_seek();
		break;			
	case PORT_FILE_DATA:
		disk_write(val);
		break;
	case PORT_IRQ_TIMER_TARGET:
		int_clear(IRQ_TIMER);
		io[PORT_IRQ_TIMER_COUNT] = 0;
		trunning = 1;
		break;
	case PORT_IRQ_TIMER_COUNT:
		/* This is strictly read only! */
		return;
	case PORT_IRQ_TIMER_RESET:
		int_clear(IRQ_TIMER);
		io[PORT_IRQ_TIMER_COUNT] = 0;
		trunning = 1;
		break;
	case PORT_IRQ_TIMER_TRIG:
		int_set(IRQ_TIMER);
		break;
	case PORT_IRQ_TIMER_PAUSE:
		trunning = 0;
		break;
	case PORT_IRQ_TIMER_CONT:
		trunning = 1;
		int_clear(IRQ_TIMER);
		break;
	case 0xFF:
		printf("trace set to %d\n", val);
		trace = val;
		if (trace & TRACE_CPU)
			log_6502 = 1;
		else
			log_6502 = 0;
	default:
		if (trace & TRACE_UNK)
			fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
	}
	io[addr] = val;
}

uint8_t do_6502_read(uint16_t addr)
{
	unsigned int bank = (addr & 0xC000) >> 14;
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X[%02X] = %02X\n", addr, (unsigned int) io[bank], (unsigned int) ram[(io[bank] << 14) + (addr & 0x3FFF)]);
	addr &= 0x3FFF;
	return ram[(io[bank] << 14) + addr];
}

uint8_t read6502(uint16_t addr)
{
	if (addr >> 8 == iopage)
		return mmio_read_6502(addr);

	return do_6502_read(addr);
}

uint8_t read6502_debug(uint16_t addr)
{
	/* Avoid side effects for debug */
	if (addr >> 8 == iopage)
		return 0xFF;

	return do_6502_read(addr);
}


void write6502(uint16_t addr, uint8_t val)
{
	unsigned int bank = (addr & 0xC000) >> 14;

	if (addr >> 8 == iopage) {
		mmio_write_6502(addr, val);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X[%02X] = %02X\n", (unsigned int) addr, (unsigned int) io[bank], (unsigned int) val);
	if (io[bank] <= 31) {
		addr &= 0x3FFF;
		ram[(io[bank] << 14) + addr] = val;
	}
	/* high writes go nowhere */
	else if (trace & TRACE_MEM)
		fprintf(stderr, "[Discarded: W above 512KiB]\n");
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
	fprintf(stderr, "pz1: [-f] [-i diskpath] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	char *rompath = "pz1.rom";
	char *diskpath = "pz1.hd";
	int fd;

	while ((opt = getopt(argc, argv, "d:fi:r:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			diskpath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	/* Insert the boot code to RAM */
	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ram + 1024, 64512) != 64512) {
		fprintf(stderr, "pz1: OS image should be 64512 bytes.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	/* Init the bank registers */
	io[0] = 0;
	io[1] = 1;
	io[2] = 2;
	io[3] = 3;

	hd_fd = open(diskpath, O_RDWR);
	if (hd_fd == -1) {
		perror(diskpath);
		exit(1);
	}

	/* 1ms sleep will get close enough to 2MHz performance */
	tc.tv_sec = 0;
	tc.tv_nsec = 1000000L;

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

	if (trace & TRACE_CPU)
		log_6502 = 1;

	init6502();
	reset6502();
	hookexternal(irqnotify);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* Everything internally on the real system is built around a 900Hz
	   timer so we work the same way. */

	while (!done) {
		/* run 6502 @ 2MHz, do timer update @ 900Hz
		 2000000 / 900 = 2222 cycles */
		exec6502(2222);
		if (!fast)
			nanosleep(&tc, NULL);
		/* Configurable interrupt timer */
		if (trunning) {
			io[PORT_IRQ_TIMER_COUNT]++;
			if (io[PORT_IRQ_TIMER_TARGET] == io[PORT_IRQ_TIMER_COUNT]) {
				int_set(IRQ_TIMER);
				trunning = 0;
			}
		}
	}
	exit(0);
}
