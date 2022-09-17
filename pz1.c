/*
 *	Platform features
 *
 *	PZ1 6502 (should be 65C02 when we sort the emulation of C02 out)
 *	I/O via modern co-processor
 *
 *	This is a very early initial emulation. The following should be
 *	correct
 *
 *	- bank registers
 *	- serial 0
 *	- idle serial 1
 *	- virtual disk via I/O processor
 *	- 50Hz timer
 *
 *	The following are not yet right
 *`	- cpu timer
 *	- 60Hz timer (it runs at 50)
 *	- timer interrupts (faked at 50Hz irrespective of settings)
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

static uint8_t ramrom[1024 * 1024];	/* 1MB RAM */
static uint8_t io[256];			/* I/O shadow */

static uint8_t iopage = 0xFE;
static uint8_t hd_fd;

static uint16_t tstate_steps = 100;	/* 2MHz */
static uint8_t fast;

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

	if (select(2, &i, NULL, NULL, &tv) == -1) {
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

/* We do this in the 6502 loop instead. Provide a dummy for the device models */
void recalc_interrupts(void)
{
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
	return c;
}

static void disk_write(uint8_t c)
{
	io[0x64] = 0;
	if (write(hd_fd, &c, 1) != 1)
		io[0x64] = 1;
}

static void disk_seek(void)
{
	off_t pos = (io[0x61] + (io[0x62] << 8)) << 9;
	if (lseek(hd_fd, pos, SEEK_SET) < 0)
		io[0x64] = 1;
	else
		io[0x64] = 0;
}
	
/* FExx is the I/O range 

	00-03	Bank registers
	10	Serial flags
	11	Serial in
	12	Serial out
	18-1A	Same for serial 2
	40	50Hz Counter
	41	60Hz Counter
	48-4B	CPU counter
	60-64	Filesystem interface
	80-85	Timer
 */

uint8_t mmio_read_6502(uint8_t addr)
{
	uint8_t r;
	/* The I/O space actually acts like a 256 byte memory block that is
	   shared. Thus reading/writing random crap behaves like memory but
	   is unbanked. So unlike normal I/O, we effectively update the
	   relevant shared RAM address on the I/O and then return it */
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	switch(addr) {
	case 0x10:
		r = check_chario();
		io[addr] = (r ^ 2) << 6;
		break;
	case 0x11:
		if (check_chario() & 1)
			io[addr] = next_char();
		break;
	case 0x63:	/* FS data */
		io[addr] = disk_read();
		break;
	}
	/* Timers are updated on the fly as they tick */
	return io[addr];
}

void mmio_write_6502(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	/* So it reads back as expected by default */
	io[addr] = val;
	switch(addr) {
	case 0x12:
		write(1, &val, 1);
		break;
	case 0x60:
		if (val == 0)
			io[0x64] = 0;
		if (val == 1)
			disk_seek();	/* Seeks 0x61, 0x62 <<8 512  byte blocks */
		break;			
	case 0x63:
		disk_write(val);
		break;
	case 0x64:
		break;
	case 0x80:
	case 0x81:
		break;
	case 0x82:
		int_clear(IRQ_TIMER);
		break;
	case 0x83:
	case 0x84:
	case 0x85:
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
}

/* Support emulating 32K/32K at some point */
uint8_t do_6502_read(uint16_t addr)
{
	unsigned int bank = (addr & 0xC000) >> 14;
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X[%02X] = %02X\n", addr, (unsigned int) io[bank], (unsigned int) ramrom[(io[bank] << 14) + (addr & 0x3FFF)]);
	addr &= 0x3FFF;
	return ramrom[(io[bank] << 14) + addr];
}

uint8_t read6502(uint16_t addr)
{
	uint8_t r;

	if (addr >> 8 == iopage)
		return mmio_read_6502(addr);

	r = do_6502_read(addr);
	return r;
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
	if (io[bank] >= 32) {
		addr &= 0x3FFF;
		ramrom[(io[bank] << 14) + addr] = val;
	}
	/* ROM writes go nowhere */
	else if (trace & TRACE_MEM)
		fprintf(stderr, "[Discarded: ROM]\n");
}

static void poll_irq_event(void)
{
}

static void irqnotify(void)
{
	if (live_irq)
		irq6502();
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

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ramrom + 1024, 64512) != 64512) {
		fprintf(stderr, "pz1: OS image should be 64512 bytes.\n");
		exit(EXIT_FAILURE);
	}
	/* For some reason it ends up in both */
	memcpy(ramrom + 512 * 1024, ramrom, 65536);
	close(fd);

	io[0] = 32;
	io[1] = 33;
	io[2] = 34;
	io[3] = 35;

	hd_fd = open(diskpath, O_RDWR);
	if (hd_fd == -1) {
		perror(diskpath);
		exit(1);
	}

	/* 20ms - it's a balance between nice behaviour and simulation
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

	if (trace & TRACE_CPU)
		log_6502 = 1;

	init6502();
	reset6502();
	hookexternal(irqnotify);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 2000000 t-states per second */
	/* We run 100 cycles per I/O check, do that 100 times then poll the
	   slow stuff and nap for 5ms. */
	while (!done) {
		int i;
		for (i = 0; i < 400; i++) {
			/* FIXME: should check return and keep adjusting */
			exec6502(tstate_steps);
			/* TODO; timers and other ints */
		}
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		io[0x40]++;
		io[0x41]++;	/* FIXME: should be 60Hz */
		/* FIXME: should be timer rate dependent */
		int_set(IRQ_TIMER);
		poll_irq_event();
	}
	exit(0);
}
