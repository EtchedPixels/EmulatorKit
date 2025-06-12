/*
 *	A very primitive platform to run Flex 6800 on for hacking around
 *	6800 CPU
 *	ACIA
 *	WD17xx floppy disk (double sided 5.25") and DC4 controls
 *	IDE at $8000
 *	Simple memory
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
#include "6800.h"
#include "ide.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"

static uint8_t ramrom[65536];
static uint8_t fast = 0;

/* The CPU runs at CLK/4 */
static uint16_t clockrate = 364 / 4;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_ACIA	16
#define TRACE_CPU	32
#define TRACE_IRQ	64

static int trace = 0;

struct m6800 cpu;
struct acia *acia;
struct wd17xx *wdfdc;

void recalc_interrupts(void)
{
	if (acia_irq_pending(acia))
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else
		m6800_clear_interrupt(&cpu, IRQ_IRQ1);
}

/* Not relevant for 6800 */

void m6800_sci_change(struct m6800 *cpu)
{
}

void m6800_tx_byte(struct m6800 *cpu, uint8_t byte)
{
}

/* I/O ports: nothing for now */

void m6800_port_output(struct m6800 *cpu, int port)
{
}

uint8_t m6800_port_input(struct m6800 *cpu, int port)
{
	return 0xFF;
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	return ide_read8(ide0, addr);
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	ide_write8(ide0, addr, val);
}

/*
 *	A very primitive WD17xx simulation
 */

struct wd17xx {
	int fd[4];
	int tracks[4];
	int spt[4];
	int sides[4];
	int drive;
	uint8_t buf[256];
	int pos;
	int wr;
	int rd;
	uint8_t track;
	uint8_t sector;
	uint8_t status;
	uint8_t side;
};

#define NOTREADY 	0x80
#define WPROT 		0x40
#define HEADLOAD 	0x20
#define CRCERR 		0x10
#define TRACK0 		0x08
#define INDEX 		0x04
#define DRQ 		0x02
#define BUSY		0x01

void wd_diskseek(struct wd17xx *fdc)
{
	off_t pos = fdc->track * fdc->spt[fdc->drive];
	if (fdc->track)
		pos += fdc->sector - 1;
	else {
		/* Track 0 has a sector 0 but not sector 2 */
		pos += fdc->sector;
		if (fdc->sector > 1)
			pos--;
	}
	/* Flex thinks in terms of double sector counts and knows nothing
	   about sides much like CP/M */
	if (fdc->sides[fdc->drive] == 2 && fdc->side)
		pos += fdc->spt[fdc->drive] / 2;
	pos *= 256;
	fflush(stdout);
	if (lseek(fdc->fd[fdc->drive], pos, SEEK_SET) < 0) {
		perror("lseek");
		exit(1);
	}
}

uint8_t wd_read_data(struct wd17xx *fdc)
{
	if (fdc->rd == 0)
		return fdc->buf[0];
	if (fdc->pos > 255)
		return fdc->buf[255];
	if (fdc->pos == 255) {
		fdc->status &= ~(BUSY | DRQ);
		fdc->rd = 0;
	}
	fflush(stdout);
	return fdc->buf[fdc->pos++];
}

void wd_write_data(struct wd17xx *fdc, uint8_t v)
{
	if (fdc->wr == 0) {
		fdc->buf[0] = v;
		return;
	}
	if (fdc->pos > 255)
		return;
	fdc->buf[fdc->pos++] = v;
	if (fdc->pos == 256) {
		wd_diskseek(fdc);
		if (write(fdc->fd[fdc->drive], fdc->buf, 256) != 256)
			fprintf(stderr, "wd: I/O error.\n");
		fdc->status &= ~(BUSY | DRQ);
		fdc->wr = 0;
	}
}

uint8_t wd_read_sector(struct wd17xx *fdc)
{
	return fdc->sector;
}

void wd_write_sector(struct wd17xx *fdc, uint8_t v)
{
	fdc->sector = v;
}

uint8_t wd_read_track(struct wd17xx *fdc)
{
	return fdc->sector;
}

void wd_write_track(struct wd17xx *fdc, uint8_t v)
{
	fdc->sector = v;
}

void wd_command(struct wd17xx *fdc, uint8_t v)
{
	if (fdc->fd[fdc->drive] == -1) {
		fdc->status = NOTREADY;
		return;
	}
	switch (v) {
	case 0x0B:
		fdc->track = 0;
		fdc->status &= ~(BUSY | DRQ);
		break;
	case 0x18:
	case 0x1B:
		fdc->track = fdc->buf[0];
		fdc->status &= ~(BUSY | DRQ);
		break;
	case 0x9C:
	case 0x8C:
		wd_diskseek(fdc);
		fdc->rd = 1;
		if (read(fdc->fd[fdc->drive], fdc->buf, 256) != 256)
			fprintf(stderr, "wd: I/O error.\n");
		fdc->status |= BUSY | DRQ;
		fdc->pos = 0;
		break;
	case 0xAC:
		fdc->status |= BUSY | DRQ;
		fdc->pos = 0;
		fdc->wr = 1;
		break;
	default:
		fprintf(stderr, "wd: unemulated command %02X\n", v);
		break;
	}
}

uint8_t wd_status(struct wd17xx *fdc)
{
	return fdc->status;
}

struct wd17xx *wd_init(void)
{
	struct wd17xx *fdc = malloc(sizeof(struct wd17xx));
	memset(fdc, 0, sizeof(*fdc));
	fdc->fd[0] = -1;
	fdc->fd[1] = -1;
	fdc->fd[2] = -1;
	fdc->fd[3] = -1;
	/* 35 track double sided */
	return fdc;
}

void wd_detach(struct wd17xx *fdc, int dev)
{
	if (fdc->fd[dev])
		close(fdc->fd[dev]);
}


#define ETRK	22
#define ESEC    23


int wd_attach(struct wd17xx *fdc, int dev, const char *path)
{
	uint8_t buf[32];

	if (fdc->fd[dev])
		close(fdc->fd[dev]);
	fdc->fd[dev] = open(path, O_RDWR);
	if (fdc->fd[dev] == -1)
		perror(path);
	/* See if it's a flex disk we can size */
	if (lseek(fdc->fd[dev], 0x210, SEEK_SET) < 0 ||
	    read(fdc->fd[dev], buf, 32) != 32) {
		wd_detach(fdc, dev);
		return -1;
	}
	if (buf[ESEC] >= 8 && buf[ESEC] <= 200 && buf[ETRK] >= 35
	    && buf[ETRK] <= 80) {
		fdc->spt[dev] = buf[ESEC];
		fdc->tracks[dev] = buf[ETRK] + 1;
		if (fdc->spt[dev] > 18)
			fdc->sides[dev] = 2;
		else
			fdc->sides[dev] = 1;
		printf("[Mounted volume %d: %d tracks, %d sectors per track %d sides].\n",
		       dev, fdc->tracks[dev], fdc->spt[dev], fdc->sides[dev]);
	}
	return fdc->fd[dev];
}

/*
 *	DC4 emulation
 */

uint8_t dc4_devstat(struct wd17xx *fdc)
{
	return 0;		/* Should be intrq... */
}

void dc4_devsel(struct wd17xx *fdc, uint8_t val)
{
	fdc->drive = val & 3;
	fdc->side = !!(val & 0x40);
	if (fdc->fd[fdc->drive])
		fdc->status = BUSY | DRQ;
	else
		fdc->status = NOTREADY;
}

/* I/O space */

static uint8_t m6800_do_ior(uint16_t addr)
{
	/* Console MP-S at 0x8004 */
	if (addr >= 0x8004 && addr <= 0x8007)
		return acia_read(acia, addr & 1);
	/* Disk at 0x8014-0x801B
	   8014: drive select
	   8018: fdc command
	   8019: fdc track
	   801A: fdc sector
	   801B: fd_data
	 */
	if (addr >= 0x8014 && addr <= 0x801B) {
		switch (addr) {
		case 0x8014:
			return dc4_devstat(wdfdc);
			break;
		case 0x8018:
			return wd_status(wdfdc);
		case 0x8019:
			return wd_read_track(wdfdc);
		case 0x801A:
			return wd_read_sector(wdfdc);
		case 0x801B:
			return wd_read_data(wdfdc);
		default:;
		}
	}
	if (ide && (addr >= 0x8080 && addr <= 0x8087)) {
		return my_ide_read(addr & 7);
	}
	return 0xFF;
}

static uint8_t m6800_ior(uint16_t addr)
{
	uint8_t r = m6800_do_ior(addr);
	if (trace & TRACE_IO)
		fprintf(stderr, "IR %04X = %02X\n", addr, r);
	return r;
}

static void m6800_iow(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "IW %04X = %02X\n", addr, val);
	/* Console MP-S at 0x8004 */
	if (addr >= 0x8004 && addr <= 0x8007)
		acia_write(acia, addr & 1, val);
	/* FDC */
	else if (addr >= 0x8014 && addr <= 0x801B) {
		switch (addr) {
		case 0x8014:
			dc4_devsel(wdfdc, val);
			break;
		case 0x8018:
			wd_command(wdfdc, val);
			break;
		case 0x8019:
			wd_write_track(wdfdc, val);
			break;
		case 0x801A:
			wd_write_sector(wdfdc, val);
			break;
		case 0x801B:
			wd_write_data(wdfdc, val);
			break;
		default:;
		}
	} else if (ide && addr >= 0x8080 && addr <= 0x8087) {
		/* IDE at 0x8080 for now */
		my_ide_write(addr & 7, val);
	}
}

uint8_t m6800_read_op(struct m6800 *cpu, uint16_t addr, int debug)
{
	if (addr >= 0x8000 && addr < 0xA000) {
		if (debug)
			return 0xFF;
		return m6800_ior(addr);
	}
	if (!debug && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

uint8_t m6800_debug_read(struct m6800 *cpu, uint16_t addr)
{
	return m6800_read_op(cpu, addr, 1);
}

uint8_t m6800_read(struct m6800 *cpu, uint16_t addr)
{
	return m6800_read_op(cpu, addr, 0);
}

void m6800_write(struct m6800 *cpu, uint16_t addr, uint8_t val)
{
	if (addr >= 0xE000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X = %02X (ROM)\n", addr,
				val);
	} else if (addr >= 0x8000 && addr < 0xA000)
		m6800_iow(addr, val);
	else {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W: %04X = %02X\n", addr, val);
		ramrom[addr] = val;
	}
}

static void poll_irq_event(void)
{
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
	fprintf(stderr,
		"flexbox: [-i idepath] [-f] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int rom = 1;
	char *rompath = "6800.rom";
	char *idepath;
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "d:fi:r:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
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

	if (rom) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		if (read(fd, ramrom, 65536) != 65536) {
			fprintf(stderr, "flexbox: short rom '%s'.\n",
				rompath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}
	ramrom[0xA105] = 0;
	ramrom[0xA106] = 1;

	if (ide) {
		ide0 = ide_allocate("cf");
		if (ide0) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			} else if (ide_attach(ide0, 0, ide_fd) == 0) {
				ide = 1;
				ide_reset_begin(ide0);
			}
		} else
			ide = 0;
	}

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

	m6800_reset(&cpu, CPU_6800, INTIO_NONE, 3);

	acia = acia_create();

	wdfdc = wd_init();
	wd_attach(wdfdc, 0, "Flex2_a.dsk");
	wd_attach(wdfdc, 1, "Flex2_b.dsk");
	if (trace & TRACE_ACIA)
		acia_trace(acia, 1);
	acia_attach(acia, &console);

	if (trace & TRACE_CPU)
		cpu.debug = 1;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i;
		for (i = 0; i < 100; i++) {
			while (cycles < clockrate)
				cycles += m6800_execute(&cpu);
			cycles -= clockrate;
		}
		/* Drive the internal serial */
		acia_timer(acia);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
