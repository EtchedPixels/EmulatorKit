/*
 *	Microtanic 6808
 *	6808 CPU at 1MHz (can be various speeds)
 *	RAM at 0000-BBFF (paged)
 *	I/O at BC00-BFFF (tanbus)
 *	RAM or ROM at C000-F7FF
 *	ROM at F800-FFFF (unpaged)
 *	VIA and 6551A as Tanex (except 6551A @ 1.8432Mhz)
 *
 *	TANRAM
 *	RAM 2000-BBFF	Pageable RAM banks (64K card equivalents can handle
 *			upper space without ROM if set up right)
 *
 *	TANDOS A800-BBFF RAM/ROM/FDC
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "6800.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "6522.h"
#include "6551.h"
#include "asciikbd.h"
#include "wd17xx.h"
#include "ide.h"
#include "58174.h"

static uint8_t mem[65536];
static uint8_t paged[16][65536];/* Paged space by board slot */
static uint8_t dosrom[4096];	/* TANDOS ROM */
static uint8_t dosram[1024];	/* TANDOS RAM */
static uint8_t dosctrl;

static unsigned romsize;	/* System ROM size */
static unsigned basic;		/* BASIC (etc) ROM present at C000-E7FF */
static unsigned numpages;	/* Pages of high memory */
static unsigned tandos;		/* TANDOS card present */
static unsigned basic_top;	/* BASIC top of ROM space */

static struct m6800 cpu;
static struct via6522 *via1, *via2;
static struct m6551 *uart;
static struct wd17xx *fdc;
static struct ide_controller *ide;
static struct mm58174 *rtc;

static uint8_t fast;
volatile int emulator_done;

static uint8_t mem_be;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_IRQ	0x000004
#define TRACE_CPU	0x000008
#define TRACE_6551	0x000010
#define TRACE_RTC	0x000020
#define TRACE_FDC	0x000040

static int trace = 0;

/* Not relevant for 6800 */
void m6800_sci_change(struct m6800 *cpu)
{
}

void m6800_tx_byte(struct m6800 *cpu, uint8_t byte)
{
}

void m6800_port_output(struct m6800 *cpu, int port)
{
}

uint8_t m6800_port_input(struct m6800 *cpu, int port)
{
	return 0xFF;
}

void recalc_interrupts(void)
{
	if (via1 && via_irq_pending(via1))
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else if (via2 && via_irq_pending(via2))
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else if (m6551_irq_pending(uart))
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else if (tandos && (dosctrl & 0x01) && wd17xx_intrq(fdc))
		m6800_raise_interrupt(&cpu, IRQ_IRQ1);
	else
		m6800_clear_interrupt(&cpu, IRQ_IRQ1);
}

static uint8_t tandos_read(uint16_t addr)
{
	unsigned r;
	uint8_t res;
	switch(addr & 0x0F) {
	case 0x00:
		return wd17xx_status(fdc);
	case 0x01:
		return wd17xx_read_track(fdc);
	case 0x02:
		return wd17xx_read_sector(fdc);
	case 0x03:
		return wd17xx_read_data(fdc);
	case 0x04:
		r = wd17xx_status_noclear(fdc);
		res = dosctrl & 0x3E;
		if (r & 0x02)	/* DRQ */
			res |= 0x80;
		if (r & 0x20)	/* HEADLOAD */
			res |= 0x40;
		if (wd17xx_intrq(fdc))
			res |= 0x01;
		return res;
	case 0x05:
	case 0x06:
	case 0x07:
		/* Switches - TODO */
		return 0xFF;
	default:
		/* TMS9914 - model not fitted for now */
		return 0xFF;
	}
	/* Can't get here */
	return 0xFF;
}

static void tandos_write(uint16_t addr, uint8_t val)
{
	switch(addr & 0x0F) {
	case 0x00:
		wd17xx_command(fdc, val);
		break;
	case 0x01:
		wd17xx_write_track(fdc, val);
		break;
	case 0x02:
		wd17xx_write_sector(fdc, val);
		break;
	case 0x03:
		wd17xx_write_data(fdc, val);
	case 0x04:
		/* Control TODO */
		dosctrl = val;
		if (trace & TRACE_FDC)
			fprintf(stderr, "fdc: dosctrl set to %02X\n", dosctrl);
		wd17xx_set_density(fdc, (val & 0x20) ? DEN_SD : DEN_DD);
		wd17xx_set_drive(fdc, (val >> 2) & 0x03);
		/* Not clear how motor timing works ? */
		wd17xx_motor(fdc, 1);
		break;
	case 0x05:
	case 0x06:
	case 0x07:
		/* Switches - RO */
		break;
	default:
		/* TMS9914 */
		break;
	}
}

uint8_t do_read(uint16_t addr, unsigned debug)
{
	/* ROM modelled as unpaged but can work either way */
	if (basic && addr >= 0xC000 && addr < basic_top)
		return mem[addr];
	if (tandos) {
		if (addr >= 0xA800 && addr < 0xB800)
			return dosrom[(addr - 0xA800) ^ 0x0800];
		if (addr >= 0xB800 && addr <= 0xBC00)
			return dosram[addr - 0xB800];
	}
	if (addr >= 0x10000 - romsize)
		return mem[addr];
	if (addr >= 0xBC00 && addr <= 0xBFFF) {
		uint8_t r = 0xFF;
		if (debug)
			return 0xFF;
		if (tandos && (addr & 0xFFF0) == 0xBF90)
			r = tandos_read(addr);
		if ((addr & 0xFFF0) == 0xBFC0)
			r = via_read(via1, addr & 0x0F);
		if ((addr & 0xFFF0) == 0xBFD0)
			r = m6551_read(uart, addr & 0x03);
		if ((addr & 0xFFF0) == 0xBFE0)
			r = via_read(via2, addr & 0x0F);
		if ((addr & 0xFFF0) == 0xBC40)
			r = mm58174_read(rtc, addr & 0x0F);
		recalc_interrupts();
		return r;
	}
	/* Paged space */
	if ((mem_be >> 4) < numpages)
		return paged[mem_be >> 4][addr];
	return 0xFF;
}

uint8_t m6800_read(struct m6800 *cpu, uint16_t addr)
{
	uint8_t r = do_read(addr, 0);
	if (trace & TRACE_MEM)
		fprintf(stderr, "%04X -> %02X\n", addr, r);
	return r;
}

uint8_t m6800_debug_read(struct m6800 *cpu, uint16_t addr)
{
	return do_read(addr, 1);
}

void m6800_write(struct m6800 *cpu, uint16_t addr, uint8_t val)
{
	if (basic && addr >= 0xC000 && addr < basic_top)
		return;
	if (addr >= 0xE800) {
		if (addr >= 0xFFF0)
			mem_be = val;
		return;
	}
	if (tandos) {
		if (addr >= 0xA800 && addr < 0xB800)
			return;
		if (addr >= 0xB800 && addr < 0xBC00) {
			dosram[addr - 0xB800] = val;
			return;
		}
	}
	if (tandos && (addr & 0xFFF0) == 0xBF90) {
		tandos_write(addr, val);
		recalc_interrupts();
		return;
	}
	if ((addr & 0xFFF0) == 0xBFC0) {
		via_write(via1, addr & 0x0F, val);
		recalc_interrupts();
		return;
	}
	if ((addr & 0xFFF0) == 0xBFD0) {
		m6551_write(uart, addr & 0x03, val);
		recalc_interrupts();
		return;
	}
	if ((addr & 0xFFF0) == 0xBC40) {
		mm58174_write(rtc, addr & 0x0F, val);
		recalc_interrupts();
		return;
	}
	if ((addr & 0xFFF0) == 0xBFE0) {
		via_write(via2, addr & 0x0F, val);
		return;
	}
	if ((addr < 0xBC00 || addr >= 0xC000) && (mem_be & 0x0F) < numpages) {
		paged[mem_be & 0x0F][addr] = val;
		return;
	}
}

/*
 *	VIA glue
 *
 *	Emulate CF directly attached to the second VIA, (resistors are a good
 *	idea on port B in case of screwups with DDRB)
 *
 *	Emulation doesn't consider DDR and the like properly. TODO
 */

#define IDE_ADDR	0x07
#define	IDE_CS0		0x08
#define	IDE_CS1		0x10	/* Not emulated at this point */
#define IDE_R		0x20
#define IDE_W		0x40
#define IDE_RESET	0x80

void via_recalc_outputs(struct via6522 *via)
{
	static unsigned old_pa;
	unsigned pa, pb, delta;

	if (via == via2 && ide) {
		/* See what is cooking */
		pa = via_get_port_a(via2);
		pb = via_get_port_b(via2);

		delta = pa ^ old_pa;
		if (delta & IDE_RESET) {
			if (!(pa & IDE_RESET))
				ide_reset_begin(ide);
		}
		if (!(pa & IDE_RESET) || (pa & IDE_CS0))
			return;
		if (delta & pa & IDE_W) {
			/* Write rising edge */
			ide_write8(ide, pa & IDE_ADDR, pb);
		}
		if (delta & IDE_R) {
			if (!(pa & IDE_R))
				via_set_port_b(via2, ide_read8(ide, pa & IDE_ADDR));
		}
	}
}

void via_handshake_a(struct via6522 *via)
{
}

void via_handshake_b(struct via6522 *via)
{
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	emulator_done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static int romload(const char *path, uint8_t *mem, unsigned int maxsize)
{
	int fd;
	int size;
	fd = open(path, O_RDONLY);
	if (fd == -1) {
		perror(path);
		exit(1);
	}
	size = read(fd, mem, maxsize);
	close(fd);
	return size;
}

static void usage(void)
{
	fprintf(stderr, "microtanic6808: [-f] [-A disk] [-B disk] [-i ide] [-r monitor] [-b basic] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	static int tstates = 1000; /* 1MHz */
	int opt;
	char *rom_path = "microtanic.rom";
	char *drive_a = NULL;
	char *drive_b = NULL;
	char *basic_path = NULL;
	char *ide_path = NULL;
	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "b:d:fi:mp:r:A:B:F:")) != -1) {
		switch (opt) {
		case 'b':
			basic_path = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'i':
			ide_path = optarg;
			break;
		case 'r':
			rom_path = optarg;
			break;
		case 'p':
			numpages = atoi(optarg);
			if (numpages > 16) {
				fprintf(stderr, "microtan: maximum of 16 banks supported.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'A':
			drive_a = optarg;
			break;
		case 'B':
			drive_b = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	romsize = romload(rom_path, mem + 0xF000, 0x1000);
	if (romsize == 0x0800)
		/* Move it up */
		memcpy(mem + 0xF800, mem + 0xF000, 0x0800);
	else if (romsize == 0x0400)
		/* Move it up */
		memcpy(mem + 0xFC00, mem + 0xF000, 0x0400);
	else if (romsize != 0x1000) {
		fprintf(stderr, "microtanic6808: invalid ROM size '%s'.\n", rom_path);
		exit(EXIT_FAILURE);
	}
	if (basic_path) {
		basic_top = romload(basic_path, mem + 0xC000, 0x2800);
		if (basic_top < 0x1000) {
			fprintf(stderr, "microtanic6808: invalid BASIC ROM\n");
			exit(EXIT_FAILURE);
		}
		basic = 1;
		basic_top = (basic_top + 0x7FF) & 0xF800;
		basic_top += 0xC000;
	}

	/* TODO: the TANDOS boot disk if present should
	   tell us the disk config and memory sizes at least for
	   track count */
	if (drive_a || drive_b) {
		tandos = 1;
		/* TODO: make path settable */
		if (romload("tandos.rom", dosrom, 4096) != 4096) {
			fprintf(stderr, "microtanic6808: invalid TANDOS ROM\n");
			exit(EXIT_FAILURE);
		}
		fdc = wd17xx_create(1791);
		wd17xx_trace(fdc, trace & TRACE_FDC);
		if (drive_a) {
			wd17xx_attach(fdc, 0, drive_a, 1, 40, 10, 256);
			wd17xx_set_media_density(fdc, 0, DEN_SD);
		}
		if (drive_b) {
			wd17xx_attach(fdc, 1, drive_b, 1, 40, 10, 256);
			wd17xx_set_media_density(fdc, 1, DEN_SD);
		}
	}

	if (ide_path) {
		int ide_fd;
		ide = ide_allocate("via2");
		ide_fd = open(ide_path, O_RDWR);
		if (ide_fd == -1) {
			perror(ide_path);
			exit(1);
		}
		ide_attach(ide, 0, ide_fd);
		ide_reset_begin(ide);
	}

	via1 = via_create();
	via2 = via_create();
	uart = m6551_create();
	m6551_attach(uart, &console);
	m6551_trace(uart, trace & TRACE_6551);

	rtc = mm58174_create();
	mm58174_trace(rtc, trace & TRACE_RTC);

	/* 10ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 10000000L;

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
	if (trace & TRACE_CPU)
		cpu.debug = 1;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int i;
		for (i = 0; i < 10; i++) {
			while (cycles < tstates)
				cycles += m6800_execute(&cpu);
			cycles -= tstates;
			via_tick(via1, tstates);
			via_tick(via2, tstates);
		}
		mm58174_tick(rtc);
		m6551_timer(uart);
		recalc_interrupts();
		if (tandos)
			wd17xx_tick(fdc, 10);
		/* Do 10ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
