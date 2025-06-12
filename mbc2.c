/*
 *	Platform features
 *
 *	Z80 at 8Mhz
 *	All I/O via what in the real world is an Atmel
 *	Optional interrupt for serial (not yet implemented)
 *	8MB emulated disk volumes
 *
 *	128K RAM (low 32K 3 banks)
 *
 *	We only emulate the Z80 side, the IOS side is faked directly by
 *	emulation
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
#include <sys/select.h>
#include "libz80/z80.h"
#include "z80dis.h"

static uint8_t ram[131072];

static uint8_t bank = 0;	/* 1 - 3 for logical banks 0-2 */
static int diskset;
static int int_on;

static uint8_t ios_disk;
static uint16_t ios_track;
static uint8_t ios_sector;
static uint8_t ios_error;
static uint8_t ios_sysflag = 2;	/* RTC */
static int ios_fd;
static uint8_t ios_cmd;
static int ios_dptr;
static int ios_data;
static uint8_t ios_buf[512];
static uint8_t ios_timer_expired;

static Z80Context cpu_z80;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_IOS	4
#define TRACE_UNK	8
#define TRACE_DISK	16
#define TRACE_BANK	32
#define TRACE_CPU	64

static int trace = 0;

/*
 *	The banks map as
 *
 *	Logical	Physical
 *	0	0
 *	High	1
 *	1	2
 *	2	3
 */
static uint8_t do_mem_read(uint16_t addr, unsigned quiet)
{
	uint8_t r;
	unsigned int va = addr;
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X <- ", addr);
	if (va < 0x8000)
		va += 0x8000 * bank;
	/* 8000-FFFF map 1:1 */
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "@%05X ", va);
	r = ram[va];
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "%02X\n", r);
	return r;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	return do_mem_read(addr, 0);
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	unsigned int va = addr;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X ", addr);
	if (va < 0x8000)
		va += 0x8000 * bank;
	if (trace & TRACE_MEM)
		fprintf(stderr, "@%05X -> %02X\n", va, val);
	/* 8000-FFFF map 1:1 */
	if (0 && addr == 0x1389)
		fprintf(stderr, "W %X B %d PC = %X\n", addr, bank, cpu_z80.M1PC);
	ram[va] = val;
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = do_mem_read(addr, 1);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return do_mem_read(addr, 1);
}

static void z80_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z80.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z80.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z80.R1.br.A, cpu_z80.R1.br.F,
		cpu_z80.R1.wr.BC, cpu_z80.R1.wr.DE, cpu_z80.R1.wr.HL,
		cpu_z80.R1.wr.IX, cpu_z80.R1.wr.IY, cpu_z80.R1.wr.SP);
}

static int check_chario(void)
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

static unsigned int next_char(void)
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

static uint8_t ios_rx_char(void)
{
	int r = check_chario();
	if (r & 1) {
		ios_sysflag &= ~8;
		return next_char();
	}
	ios_sysflag |= 8;
	return 0xFF;
}

static void ios_rtc_load(void)
{
	struct tm *tm;
	time_t t;

	time(&t);
	tm = gmtime(&t);

	if (tm == NULL) {
		fprintf(stderr, "mbc2: unable to get time.\n");
		exit(1);
	}
	ios_buf[0] = tm->tm_sec;
	ios_buf[1] = tm->tm_min;
	ios_buf[2] = tm->tm_hour;
	ios_buf[3] = tm->tm_mday;
	ios_buf[4] = tm->tm_mon;
	ios_buf[5] = tm->tm_year - 100;	/* 2000 based */
	ios_buf[6] = (uint8_t)-40;	/* Silly value for temperature */
}

static void ios_open(void)
{
	char buf[32];

	if (trace & TRACE_DISK)
		fprintf(stderr, "IOS: Open disk %d.\n", ios_disk);
	if (ios_disk > 99) {
		ios_error = 16;
		return;
	}
	snprintf(buf, 32, "DS%dN%02d.DSK", diskset, ios_disk);
	if (ios_fd)
		close(ios_fd);
	ios_fd = open(buf, O_RDWR);
	if (ios_fd == -1)
		ios_error = 3;
	if (trace & TRACE_DISK)
		fprintf(stderr, "IOS: Open %d result %d.\n", ios_disk, ios_fd);
}

static int ios_seek(void)
{
	off_t offset;

	if (trace & TRACE_DISK)
		fprintf(stderr, "IOS: Seek %d %d %d.\n", ios_fd, ios_track, ios_sector);

	if (ios_fd == -1 || ios_sector > 31 || ios_track > 511) {
		ios_error = 18;
		return -1;
	}
	offset = (ios_track * 32 + ios_sector) * 512;
	if (trace & TRACE_DISK)
		fprintf(stderr, "IOS: disk LBA %u\n",
			(unsigned)(offset >> 9));
	if (lseek(ios_fd, offset, SEEK_SET) == -1) {
		ios_error =  19;
		return -1;
	}
	return 0;
}

static void ios_read_sector(void)
{
	if(ios_seek())
		return;
	if (trace & TRACE_DISK)
		fprintf(stderr, "IOS: Read.\n");
	if (read(ios_fd, ios_buf, 512) != 512)
		ios_error = 19;
}

static void ios_write_sector(void)
{
	if(ios_seek())
		return;
	if (trace & TRACE_DISK)
		fprintf(stderr, "IOS: Write.\n");
	if (write(ios_fd, ios_buf, 512) != 512)
		ios_error = 19;
}

static void ios_op(uint8_t val)
{
	ios_cmd = val;
	ios_dptr = 0;
	/* Idle */
	if (val == 0xFF)
		return;
	ios_data = 1;
	if (trace & TRACE_IOS)
		fprintf(stderr, "IOS_cmd %02X\n", ios_cmd);
	if (val & 0x80) {
		if (val > 0x89) {
			ios_cmd = 0xFF;
			return;
		}
		switch(val) {
		case 0x80:
			ios_buf[0] = ios_rx_char();
			/* Should drop interrupt here */
			break;
		case 0x81:
		case 0x82:
			ios_buf[0] = 0xFF;
			break;
		case 0x83:
			ios_sysflag &= ~4;
			if (check_chario() & 1)
				ios_sysflag |= 4;
			ios_buf[0] = ios_sysflag;
			break;
		case 0x84:
			ios_data = 7;
			ios_rtc_load();
			break;
		case 0x85:
			ios_buf[0] = ios_error;
			if (trace & TRACE_IOS)
				fprintf(stderr, "ios_error was %02X\n", ios_error);
			ios_error = 0;
			break;
		case 0x86:
			ios_data = 512;
			ios_read_sector();
			break;
		case 0x87:
			/* FIXME?? */
			break;
		case 0x88:
			ios_buf[0] = check_chario() & 2 ? 1 : 0;
			break;
		case 0x89:
			ios_buf[0] = check_chario() & 1;
			ios_buf[0] |= ios_timer_expired ? 2 : 0;
			ios_timer_expired = 0;
			break;
		}
	} else {
		if (val == 2 || val > 0x0D) {
			fprintf(stderr, "Unemulated command %02X\n", val);
			ios_cmd = 0xFF;
			return;
		}
		if (val == 0x0A)
			ios_data = 2;
		if (val == 0x0C)
			ios_data = 512;
	}
}

static uint8_t ios_rx(void)
{
	if (ios_cmd & 0x80) {
		if (ios_cmd == 0xFF)
			return 0xFF;
		if (ios_dptr >= ios_data)
			return 0xFF;
		return ios_buf[ios_dptr++];
	}
	return 0xFF;
}

static void ios_tx(uint8_t val)
{
	if (ios_cmd & 0x80)
		return;
	if (ios_dptr >= ios_data)
		return;
	if (trace & TRACE_IOS)
		fprintf(stderr, "[T%02X]", val);
	ios_buf[ios_dptr++] = val;
	if (ios_dptr != ios_data)
		return;
	switch(ios_cmd) {
	case 0x01:
		putchar(ios_buf[0]);
		fflush(stdout);
		break;
	case 0x09:
		ios_disk = ios_buf[0];
		ios_open();
		break;
	case 0x0A:
		ios_track = ios_buf[0] + (((uint16_t)ios_buf[1]) << 8);
		if (trace & TRACE_DISK)
			fprintf(stderr, "Track now %d.\n", ios_track);
		break;
	case 0x0B:
		ios_sector = ios_buf[0];
		if (trace & TRACE_DISK)
			fprintf(stderr, "Sector now %d.\n", ios_sector);
		break;
	case 0x0C:
		ios_write_sector();
		break;
	case 0x0D:
		if (ios_buf[0] < 3) {
			bank = ios_buf[0];
			/* 0 1 2 map to 0 2 3 */
			if (trace & TRACE_BANK)
				fprintf(stderr, "Bank set to %d: physical ", bank);
			if (bank)
				bank++;
			if (trace & TRACE_BANK)
				fprintf(stderr, "%d.\n", bank);
		}
		break;
	}
}

static uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;
	if (addr == 0)
		return ios_rx();
	if (addr == 1)
		return ios_rx_char();
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr == 0)
		ios_tx(val);
	else if (addr == 1)
		ios_op(val);
	else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "mbc2: [-f] [-i] [-s diskset] [-d debug] [-b image] [-a addr]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	int fast = 0;
	char *image = "fuzix.bin";
	uint16_t addr = 0x0000;

	while ((opt = getopt(argc, argv, "d:s:ib:a:f")) != -1) {
		switch (opt) {
		case 's':
			diskset = atoi(optarg);
			if (diskset < 0 || diskset > 99)
				usage();
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'b':
			image = optarg;
			break;
		case 'i':
			int_on = 1;
			break;
		case 'f':
			fast = 1;
			break;
		case 'a':
			addr = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	fd = open(image, O_RDONLY);
	if (fd == -1) {
		perror(image);
		exit(EXIT_FAILURE);
	}
	l = read(fd, ram + addr, 65536 - addr);
	if (l < 1024) {
		fprintf(stderr, "mbc2: short image '%s'.\n", image);
		exit(EXIT_FAILURE);
	}
	close(fd);
	if (addr) {
		ram[0] = 0xC3;
		ram[1] = addr;
		ram[2] = addr >> 8;
	}
	printf("Loaded %d bytes at %04X.\n", l, addr);

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

	Z80RESET(&cpu_z80);
	cpu_z80.ioRead = io_read;
	cpu_z80.ioWrite = io_write;
	cpu_z80.memRead = mem_read;
	cpu_z80.memWrite = mem_write;
	cpu_z80.trace = z80_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		int l;
		for (l = 0; l < 10; l++) {
			int i;
			/* 40000 T states */
			for (i = 0; i < 100; i++) {
				Z80ExecuteTStates(&cpu_z80, 400);
			}
			if (int_on && (check_chario() & 1))
				Z80INT(&cpu_z80, 0xFF);
			/* Do 5ms of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
		}
		ios_timer_expired = 1;
		if (int_on)
			Z80INT(&cpu_z80, 0xFF);
	}
	exit(0);
}
