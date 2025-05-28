/*
 *	Platform features
 *
 *	PZ1 6502 (should be 65C02 when we sort the emulation of C02 out)
 *	I/O via modern co-processor
 *	512KiB RAM, no ROM, no swap, boot code inserted by I/O-processor
 *
 *	This is a simple emulation, enough to run Fuzix.
 *	The following is roughly correct:
 *	- bank registers, banked memory access
 *	- top page memory unbanked
 *	- serial 0
 *	- idle serial 1
 *	- virtual disk via I/O processor
 *	- Interrupt timer
 *
 *	HW available in real PZ1 but not used in Fuzix:
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
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <lib65816/cpu.h>
#include <lib65816/cpuevent.h>

static uint8_t ram[512 * 1024];
#define BANK_SIZE 16384

static uint8_t bankreg[4];

static uint8_t diskprm0;
static uint8_t diskprm1;
static uint8_t diskprm2;
static uint8_t diskprm3;
static uint8_t diskstatus;

static uint8_t timertarget;
static uint8_t timercount;
static bool trunning;

static int hd_fd;

static bool fast;

#define IRQ_TIMER		1

#define TRACE_MEM		1
#define TRACE_IO		2
#define TRACE_IRQ		4
#define TRACE_UNK		8
#define TRACE_CPU		16

static int trace = 0;

/* Run 6502 @ 2MHz, do timer update @ 900Hz */
static uint16_t tstate_steps = 2000000 / 900;

static struct timespec tc;


/* IO-ports */
#define IO_PAGE			0xFE00
#define IO_BANK_0		0xFE00
#define IO_BANK_1		0xFE01
#define IO_BANK_2		0xFE02
#define IO_BANK_3		0xFE03
#define IO_SERIAL_0_FLAGS	0xFE10
#define IO_SERIAL_0_IN		0xFE11
#define IO_SERIAL_0_OUT		0xFE12
#define IO_SERIAL_1_FLAGS	0xFE18
#define IO_SERIAL_1_IN		0xFE19
#define IO_SERIAL_1_OUT		0xFE1A
#define IO_DISK_CMD		0xFE60
#define IO_DISK_PRM_0		0xFE61
#define IO_DISK_PRM_1		0xFE62
#define IO_DISK_PRM_2		0xFE63
#define IO_DISK_PRM_3		0xFE64
#define IO_DISK_DATA		0xFE65
#define IO_DISK_STATUS		0xFE66
#define IO_TIMER_TARGET		0xFE80
#define IO_TIMER_COUNT		0xFE81
#define IO_TIMER_RESET		0xFE82
#define IO_TIMER_TRIG		0xFE83
#define IO_TIMER_PAUSE		0xFE84
#define IO_TIMER_CONT		0xFE85

#define SERIAL_FLAGS_OUT_FULL	128
#define SERIAL_FLAGS_IN_AVAIL	64
#define DISK_STATUS_OK		0
#define DISK_STATUS_NOK		1
#define DISK_CMD_SELECT		0
#define DISK_CMD_SEEK		1

static unsigned int check_chario(void)
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

static uint8_t disk_read(void)
{
	uint8_t c;
	if (read(hd_fd, &c, 1) != 1)
		diskstatus = DISK_STATUS_NOK;
	else
		diskstatus = DISK_STATUS_OK;
	return c;
}

static void disk_write(uint8_t c)
{
	if (write(hd_fd, &c, 1) != 1)
		diskstatus = DISK_STATUS_NOK;
	else
		diskstatus = DISK_STATUS_OK;
}

/* seek to disk position using 24-bit LBA */
static void disk_seek(void)
{
	off_t pos = 512 * (diskprm0 + (diskprm1 << 8) + (diskprm2 << 16));
	if (lseek(hd_fd, pos, SEEK_SET) < 0)
		diskstatus = DISK_STATUS_NOK;
	else
		diskstatus = DISK_STATUS_OK;
}

static uint8_t io_read(uint16_t addr, uint8_t debug)
{
	uint8_t result;

	switch(addr) {
	case IO_SERIAL_0_FLAGS:
		result = (check_chario() ^ 2) << 6;
		break;
	case IO_SERIAL_0_IN:
		if (debug)
			result = 0xFF;
		else
			if (check_chario() & 1)
				result = next_char();
			else
				result = 0;
		break;
	case IO_SERIAL_1_FLAGS:
		result = 0;
		break;
	case IO_SERIAL_1_IN:
		if (debug)
			result = 0xFF;
		else
			result = 0;
		break;
	case IO_DISK_DATA:
		if (debug)
			result = 0xFF;
		else
			result = disk_read();
		break;
	case IO_DISK_STATUS:
		result = diskstatus;
		break;
	default:
		result = 0xFF;
	}

	if (trace & TRACE_IO)
		fprintf(stderr, "IOR %04X = %02X\n", addr, result);

	return(result);
}

static void io_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "IOW %04X = %02X\n", addr, val);

	switch(addr) {
	case IO_BANK_0:
		bankreg[0] = val;
		break;
	case IO_BANK_1:
		bankreg[1] = val;
		break;
	case IO_BANK_2:
		bankreg[2] = val;
		break;
	case IO_BANK_3:
		bankreg[3] = val;
		break;
	case IO_SERIAL_0_OUT:
		write(1, &val, 1);
		break;
	case IO_DISK_CMD:
		if (val == 0) /* SELECT */
			diskstatus = DISK_STATUS_OK;
		if (val == 1) /* SEEK */
			disk_seek();
		break;
	case IO_DISK_PRM_0:
		diskprm0 = val;
		break;
	case IO_DISK_PRM_1:
		diskprm1 = val;
		break;
	case IO_DISK_PRM_2:
		diskprm2 = val;
		break;
	case IO_DISK_PRM_3:
		diskprm3 = val;
		break;
	case IO_DISK_DATA:
		disk_write(val);
		break;
	case IO_TIMER_TARGET:
		CPU_clearIRQ(IRQ_TIMER);
		timercount = 0;
		trunning = true;
		break;
	case IO_TIMER_RESET:
		CPU_clearIRQ(IRQ_TIMER);
		timercount = 0;
		trunning = true;
		break;
	case IO_TIMER_TRIG:
		CPU_addIRQ(IRQ_TIMER);
		break;
	case IO_TIMER_PAUSE:
		trunning = false;
		break;
	case IO_TIMER_CONT:
		trunning = true;
		CPU_clearIRQ(IRQ_TIMER);
		break;
	case 0xFF:
		printf("trace set to %d\n", val);
		trace = val;
		if (trace & TRACE_CPU)
			CPU_setTrace(1);
		else
			CPU_setTrace(0);
		break;
	default:
		if (trace & TRACE_UNK)
			fprintf(stderr, "Unknown IOW %04X = %02X\n", addr, val);
	}
}

uint8_t read65c816(uint32_t addr, uint8_t debug)
{
	uint8_t val;

	addr &= 0xFFFF;		/* 16 bit input */
	if (addr < IO_PAGE) {
		/* 0x0000-0xFDFF, banked ram read */
		uint8_t bank = addr >> 14;
		uint8_t block = bankreg[bank];
		if (block < (sizeof(ram) / BANK_SIZE)) {
			val = ram[(block << 14) + (addr & (BANK_SIZE - 1))];
			if (trace & TRACE_MEM)
				fprintf(stderr, "R %04X[%02X] = %02X\n",
					(uint16_t) addr,
					(uint8_t) block,
					(uint8_t) val);
		}
		/* high reads are faulty */
		else {
			val = 0xFF;
			if (trace & TRACE_MEM) {
				fprintf(stderr, "Discarded: R above 512KiB %04X[%02X] = %02X\n",
					(uint16_t) addr,
					(uint8_t) block,
					(uint8_t) val);
			}
		}
	} else if ((addr & 0xFF00) == IO_PAGE) {
		/* 0xFE00-0xFEFF, IO read */
		val = io_read(addr, debug);
	} else {
		/* 0xFF00-0xFFFF, top page read */
		val = ram[addr];
		if (trace & TRACE_MEM)
			fprintf(stderr, "R %04X = %02X\n",
				(uint16_t) addr,
				(uint8_t) val);
	}
	return val;
}

void write65c816(uint32_t addr, uint8_t val)
{
	addr &= 0xFFFF;		/* 16 bit input */
	if (addr < IO_PAGE) {
		/* 0x0000-0xFDFF, banked ram write */
		uint8_t bank = addr >> 14;
		uint8_t block = bankreg[bank];
		if (block < (sizeof(ram) / BANK_SIZE)) {
			ram[(block << 14) + (addr & (BANK_SIZE - 1))] = val;
			if (trace & TRACE_MEM)
				fprintf(stderr, "W %04X[%02X] = %02X\n",
					(uint16_t) addr,
					(uint8_t) block,
					(uint8_t) val);
		}
		/* high writes go nowhere */
		else if (trace & TRACE_MEM)
			fprintf(stderr, "Discarded: W above 512KiB %04X[%02X]\n",
					(uint16_t) addr,
					(uint8_t) block);
	} else if ((addr & 0xFF00) == IO_PAGE) {
		/* 0xFE00-0xFEFF, IO write */
		io_write(addr, val);
	} else {
		/* 0xFF00-0xFFFF, top page write */
		ram[addr] = val;
		if (trace & TRACE_MEM)
			fprintf(stderr, "W %04X = %02X\n",
				(uint16_t) addr,
				(uint8_t) val);
	}
}

void system_process(void)
{
	/*
	 * This is the wrong way to do timing but it's easier for the moment.
	 * We should track how much real time has occurred and try to keep cycle
	 * matched with that. The scheme here works fine except when the host
	 * is loaded though
	 */
	if (fast == false)
		nanosleep(&tc, NULL);
	/* Configurable interrupt timer */
	if (trunning == true) {
		timercount++;
		if (timercount == timertarget) {
			CPU_addIRQ(IRQ_TIMER);
			trunning = false;
		}
	}
}

void wdm(void)
{
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
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
	int opt;
	char *rompath = "pz1.rom";
	char *diskpath = "filesys.img";
	int fd;

	fast = false;

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
			fast = true;
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
	if (read(fd, ram, 65536) != 65536) {
		fprintf(stderr, "pz1: OS image should be 65536 bytes.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	/* Init the bank registers */
	bankreg[0] = 0;
	bankreg[1] = 1;
	bankreg[2] = 2;
	bankreg[3] = 3;

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
		CPU_setTrace(1);

	CPUEvent_initialize();
	CPU_setUpdatePeriod(tstate_steps);
	CPU_reset();
	CPU_run();
	exit(0);
}
