/*
 *	SWTPC 6809
 *	MP-09 CPU card
 *	20bit DAT addressing
 *	ACIA at F004 (mirrored at E004 for other monitors)
 *	PT SS30-IDE
 *
 *	TODO: sort out Exxx v Fxxx for different monitor/board setups
 *	(Fxxx is more normal)
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
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "d6809.h"
#include "e6809.h"
#include "acia.h"
#include "ide.h"
#include "wd17xx.h"

static uint8_t rom[4096];
static uint8_t ram[1024 * 1024];
static uint8_t dat[16];

static unsigned fast;

static struct acia *acia;
static struct ide_controller *ide;
static struct wd17xx *fdc;

/* 1MHz */
static uint16_t clockrate =  100;

static uint8_t live_irq;

#define IRQ_ACIA	1


static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_CPU	16
#define TRACE_IRQ	32
#define TRACE_ACIA	64
#define TRACE_FDC	128

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

static void int_clear(unsigned int irq)
{
	live_irq &= ~(1 << irq);
}

static void int_set(unsigned int irq)
{
	live_irq |= 1 << irq;
}

void recalc_interrupts(void)
{
	if (acia_irq_pending(acia))
		int_set(IRQ_ACIA);
	else
		int_clear(IRQ_ACIA);
}

/* For now assume RAM only */
static uint8_t *dat_xlate(unsigned addr)
{
	return ram + dat[addr >> 12] + (addr & 0xFFF);
}

unsigned char do_e6809_read8(unsigned addr, unsigned debug)
{
        unsigned char r = 0xFF;
        /* Stop the debugger causing side effects in the I/O window */
        if (debug && addr >= 0xE000 && addr < 0xF800)
        	return r;

	/* TODO: set the I/O window up properly, mask and decode
	   to slot implementations of cards */
	if ((addr & 0xEFFE) == 0xE004)
		r = acia_read(acia, addr & 1);
	else if (ide && addr >= 0xE058 && addr <= 0xE05F)
		r = ide_read8(ide, addr & 7);
	else if (fdc && addr == 0xE018)
		r = wd17xx_status(fdc);
	else if (fdc && addr == 0xE019)
		r = wd17xx_read_track(fdc);
	else if (fdc && addr == 0xE01A)
		r = wd17xx_read_sector(fdc);
	else if (fdc && addr == 0xE01B)
		r = wd17xx_read_data(fdc);
	else if (fdc && addr == 0xE014) {
		/* DCS3 and later have DRQ/IRQ status here */
		/* TODO: verify which bit is intrq */
		r = wd17xx_intrq(fdc);
		r |= wd17xx_status_noclear(fdc) & 0x80;
	} else if (addr >= 0xF800)
		r = rom[addr & 0x7FF];
	else if (addr < 0xE000)
		r = *dat_xlate(addr);
	if (!debug && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X = %02X\n", addr, r);
	return r;
}

unsigned char e6809_read8(unsigned addr)
{
	return do_e6809_read8(addr, 0);
}

unsigned char e6809_read8_debug(unsigned addr)
{
	return do_e6809_read8(addr, 1);
}

void e6809_write8(unsigned addr, unsigned char val)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X = %02X\n", addr, val);
	if ((addr & 0xEFFE) == 0xE004)
		acia_write(acia, addr & 1, val);
	else if (addr >= 0xFFF0)
		dat[addr & 0x0F] = ~val;
	else if (ide && addr >= 0xE058 && addr <= 0xE05F)
		ide_write8(ide, addr & 7, val);
	else if (addr == 0xE018 && fdc)
		wd17xx_command(fdc, val);
	else if (addr == 0xE019 && fdc)
		wd17xx_write_track(fdc, val);
	else if (addr == 0xE01A && fdc)
		wd17xx_write_sector(fdc, val);
	else if (addr == 0xE01B && fdc)
		wd17xx_write_data(fdc, val);
	else if (addr == 0xE014 && fdc) {
		/* Drive register.. unclear what all bits are */
		wd17xx_set_drive(fdc, val & 1);
	}
	else if (addr >= 0xF800)
		fprintf(stderr, "***ROM WRITE %04X = %02X\n", addr, val);
	else if (addr < 0xE000)
		*dat_xlate(addr) = val;
}

static const char *make_flags(uint8_t cc)
{
	static char buf[9];
	char *p = "EFHINZVC";
	char *d = buf;

	while(*p) {
		if (cc & 0x80)
			*d++ = *p;
		else
			*d++ = '-';
		cc <<= 1;
		p++;
	}
	*d = 0;
	return buf;
}

/* Called each new instruction issue */
void e6809_instruction(unsigned pc)
{
	char buf[80];
	struct reg6809 *r = e6809_get_regs();
	if (trace & TRACE_CPU) {
		d6809_disassemble(buf, pc);
		fprintf(stderr, "%04X: %-16.16s | ", pc, buf);
		fprintf(stderr, "%s %02X:%02X %04X %04X %04X %04X\n",
			make_flags(r->cc),
			r->a, r->b, r->x, r->y, r->u, r->s);
	}
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
	fprintf(stderr, "swt6809: [-f] [-i idepath] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

struct diskgeom {
	const char *name;
	unsigned int size;
	unsigned int sides;
	unsigned int tracks;
	unsigned int spt;
	unsigned int secsize;
};

struct diskgeom disktypes[] = {
	{ "CP/M 77 track DSDD", 788480, 2, 77, 10, 512},
	{ "CP/M 77 track SSDD", 394240, 1, 77, 10, 512},
	{ NULL,}
};

static struct diskgeom *guess_format(const char *path)
{
	struct diskgeom *d = disktypes;
	struct stat s;
	off_t size;
	if (stat(path, &s) == -1) {
		perror(path);
		exit(1);
	}
	size = s.st_size;
	while(d->name) {
		if (d->size == size)
			return d;
		d++;
	}
	fprintf(stderr, "nascom: unknown disk format size %ld.\n", (long)size);
	exit(1);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "swt6809.rom";
	char *idepath = NULL;
	char *fdc_path[2] = { NULL, NULL };
	unsigned int cycles = 0;
	unsigned need_fdc = 0;
	unsigned i;

	while ((opt = getopt(argc, argv, "A:B:d:fi:r:")) != -1) {
		switch (opt) {
		case 'A':
			fdc_path[0] = optarg;
			need_fdc = 1;
			break;
		case 'B':
			fdc_path[1] = optarg;
			need_fdc = 1;
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
		case 'r':
			rompath = optarg;
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
	if (read(fd, rom, 2048) != 2048) {
		fprintf(stderr, "swt6809: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
		close(fd);
	}

	if (idepath ) {
		ide = ide_allocate("cf");
		if (ide) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = NULL;
			} else if (ide_attach(ide, 0, ide_fd) == 0) {
				ide_reset_begin(ide);
			}
		}
	}

	if (need_fdc) {
		fdc = wd17xx_create(1797);
		for (i = 0; i <2 ; i++) {
			if (fdc_path[i]) {
				struct diskgeom *d = guess_format(fdc_path[i]);
				printf("[Drive %c, %s.]\n", 'A' + i, d->name);
				wd17xx_attach(fdc, i, fdc_path[i], d->sides, d->tracks, d->spt, d->secsize);
			}
		}
		wd17xx_trace(fdc, trace & TRACE_FDC);
	}

	acia = acia_create();
	acia_set_input(acia, 1);
	acia_trace(acia, trace & TRACE_ACIA);

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

	e6809_reset(trace & TRACE_CPU);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		unsigned int i;
		for (i = 0; i < 100; i++) {
			while(cycles < clockrate)
				cycles += e6809_sstep(live_irq, 0);
			cycles -= clockrate;
			recalc_interrupts();
		}
		/* Drive the serial */
		acia_timer(acia);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	exit(0);
}
