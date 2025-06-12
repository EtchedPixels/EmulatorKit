/*
 *	Simple S100 setup with an 8080 CPU
 *
 *	IMSAI style serial
 *	Tarbell 8" floppy controller
 *	Modern IDE controller
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include "intel_8080_emulator.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "mits1.h"
#include "ide.h"
#include "wd17xx.h"
#include "tarbell_fdc.h"

static uint8_t rom[4096];
static uint8_t ram[4096];

static unsigned int fast;
static volatile int done;
static unsigned bootmode = 1;
static unsigned emulator_done;

struct mits1_uart *mu;
static struct ide_controller *ide;
static struct wd17xx *fdc;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	4
#define TRACE_SERIAL	8
#define TRACE_IRQ	16
#define TRACE_CPU	32
#define TRACE_FDC	64

static int trace = 0;

static unsigned live_irq;

#define IRQ_MU_OUT	1
#define IRQ_MU_IN	2

/* Simple setup to begin with */
static uint8_t *mem_map(uint16_t addr, bool wr)
{
	if (bootmode && !wr) {
		/* Read from actual ROM space turns off boot mode */
		if ((addr & 0xF000) == 0xF000)
			bootmode = 0;
		return rom + (addr & 0x07FF);
	}
	if (addr >= 0xF000) {
		if (wr)
			return NULL;
		return rom + (addr & 0x0FFF);
	}
	return ram + (addr & 0x0FFF);
}

uint8_t i8080_read(uint16_t addr)
{
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	r = *mem_map(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);
	return r;
}

uint8_t i8080_debug_read(uint16_t addr)
{
	return *mem_map(addr, 0);
}

void i8080_write(uint16_t addr, uint8_t val)
{
	uint8_t *p;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	p = mem_map(addr,  1);
	if (p)
		*p = val;
}

/* For now just hand back RST 7 */
uint8_t i8080_get_vector(void)
{
	return 0xFF;
}

/* TODO: vectors and vector support on 8080.c */
void recalc_interrupts(void)
{
	if (live_irq)
		i8080_set_int(INT_IRQ);
	else
		i8080_clear_int(INT_IRQ);
}

#if 0
static void int_set(int src)
{
	live_irq |= (1 << src);
	recalc_interrupts();
}

static void int_clear(int src)
{
	live_irq &= ~(1 << src);
	recalc_interrupts();
}
#endif

static void poll_irq_event(void)
{
	/* No interrupts for now */
}

uint8_t i8080_inport(uint8_t addr)
{
	uint8_t r;
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if (addr == 0x00 || addr == 0x01) {
		r = mits1_read(mu, addr & 1);
		poll_irq_event();
		return r;
	}
	if (ide && addr >= 0x30 && addr <= 0x37)
		return ide_read8(ide, addr);
	if (fdc && addr >= 0xF8 && addr <= 0xFC)
		return tbfdc_read(fdc, addr);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void i8080_outport(uint8_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if (addr == 0x00 || addr == 0x01)
		mits1_write(mu, addr & 1, val);
	else if (ide && addr >= 0x30 && addr <= 0x37)
		ide_write8(ide, addr, val);
	else if (fdc && addr >= 0xF8 && addr <= 0xFC)
		tbfdc_write(fdc, addr & 7, val);
	else if (addr == 0xFD)
		trace = val;
	else if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
	poll_irq_event();
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
	fprintf(stderr, "s100-8080: [-A disk] [-B disk] [-f] [-r path] [-d debug] [-i ide]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "s100-8080.rom";
	char *idepath = NULL;
	char *drive_a = NULL, *drive_b = NULL;

	while ((opt = getopt(argc, argv, "A:B:d:fi:r:")) != -1) {
		switch (opt) {
		case 'A':
			drive_a = optarg;
			break;
		case 'B':
			drive_b = optarg;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'i':
			idepath = optarg;
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
	l = read(fd, rom, 4096);
	if (l != 4096) {
		fprintf(stderr, "s100-8080: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (drive_a || drive_b) {
		fdc = tbfdc_create();
		if (trace & TRACE_FDC)
			wd17xx_trace(fdc, 1);
		if (drive_a)
			wd17xx_attach(fdc, 0, drive_a, 1, 80, 26, 128);
		if (drive_b)
			wd17xx_attach(fdc, 0, drive_b, 1, 80, 26, 128);
	}

	if (idepath) {
		ide = ide_allocate("cf0");
		fd = open(idepath, O_RDWR);
		if (fd == -1) {
			perror(idepath);
			exit(1);
		}
		ide_attach(ide, 0, fd);
	}


	/* 20ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;

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

	mu = mits1_create();
	if (trace & TRACE_SERIAL)
		mits1_trace(mu, 1);
	mits1_attach(mu, &console);

	i8080_reset();
	if (trace & TRACE_CPU) {
		i8080_log = stderr;
	}

	/* We run 1000000 t-states per second */
	while (!emulator_done) {
		int i;
		for (i = 0; i < 10; i++) {
			i8080_exec(2000);
			mits1_timer(mu);
			poll_irq_event();
			/* We want to run UI events regularly it seems */
//			ui_event();
		}
		/* Do 20ms of I/O and delays */
		if (fdc)
			wd17xx_tick(fdc, 20);
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
