/*
 *	Ampro Littleboard
 *	4MHz Z80
 *	64K DRAM
 *	4-32K ROM (repeats through low space)
 *	DART
 *	CTC (provides some DART timings)
 *	Printer port
 *	WD1772 floppy
 *
 *	PLUS has an NCR5380
 *
 *	Emulation includes the third party MDISK board/mod that allows
 *	port 0x30 to control up to 1MB of RAM.
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
#include <sys/stat.h>
#include "libz80/z80.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "z80dis.h"
#include "z80sio.h"
#include "sasi.h"
#include "ncr5380.h"
#include "wd17xx.h"


static uint8_t ram[1048576];
static uint8_t rom[32768];

static uint8_t fast = 0;
static uint8_t idport = 0x07;
static uint8_t bcr = 0;
static uint16_t eprom_mask;

static Z80Context cpu_z80;
static uint8_t int_recalc = 0;

struct sasi_bus *sasi;
struct ncr5380 *ncr;
struct wd17xx *wd;
struct z80_sio *sio;
static uint32_t mdisk_latch;	/* Pre shifted for convenience */

static unsigned sector_base[4];

/* IRQ source that is live */
static uint8_t live_irq;

#define IRQ_SIO		1
#define IRQ_CTC		2

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_SIO	16
#define TRACE_CTC	32
#define TRACE_IRQ	64
#define TRACE_CPU	128
#define TRACE_SCSI	256
#define TRACE_FDC	512
#define TRACE_BANK	1024

static int trace = 0;

static void reti_event(void);

static uint8_t *mdisk(uint16_t addr)
{
	uint32_t pa = addr & 0x7FFF;
	pa |= mdisk_latch;
	return ram + pa;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate;
	uint8_t r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R");
	if (addr < 0x8000 && !(bcr & 0x40))
		r = rom[addr & eprom_mask];
	else if (addr >= 0x8000)
		r = ram[addr];
	else
		r = *mdisk(addr);

	if (trace & TRACE_MEM)
		fprintf(stderr, " %04X <- %02X\n", addr, r);

	/* Look for ED with M1, followed directly by 4D and if so trigger
	   the interrupt chain */
	if (cpu_z80.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r== 0xCB) {
			rstate = 2;
			return r;
		}
		/* Look for ED with M1, followed directly by 4D and if so trigger
		   the interrupt chain */
		if (r == 0xED && rstate == 0) {
			rstate = 1;
			return r;
		}
	}
	if (r == 0x4D && rstate == 1)
		reti_event();
	rstate = 0;
	return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
	if (addr < 0x8000 && !(bcr & 0x40)) {
/*		if (trace & TRACE_MEM) */
			fprintf(stderr, "W %04X : ROM\n", addr);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X -> %02X\n", addr, val);
	if (addr >= 0x8000)
		ram[addr] = val;
	else
		*mdisk(addr) = val;
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = mem_read(0, addr);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return mem_read(0, addr);
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

static void recalc_interrupts(void)
{
	int_recalc = 1;
}


/*
 *	Z80 CTC
 */

struct z80_ctc {
	uint16_t count;
	uint16_t reload;
	uint8_t vector;
	uint8_t ctrl;
#define CTC_IRQ		0x80
#define CTC_COUNTER	0x40
#define CTC_PRESCALER	0x20
#define CTC_RISING	0x10
#define CTC_PULSE	0x08
#define CTC_TCONST	0x04
#define CTC_RESET	0x02
#define CTC_CONTROL	0x01
};

#define CTC_STOPPED(c)	(((c)->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET))

struct z80_ctc ctc[4];
uint8_t ctc_irqmask;

static void ctc_reset(struct z80_ctc *c)
{
	c->vector = 0;
	c->ctrl = CTC_RESET;
}

static void ctc_init(void)
{
	ctc_reset(ctc);
	ctc_reset(ctc + 1);
	ctc_reset(ctc + 2);
	ctc_reset(ctc + 3);
}

static void ctc_interrupt(struct z80_ctc *c)
{
	int i = c - ctc;
	if (c->ctrl & CTC_IRQ) {
		if (!(ctc_irqmask & (1 << i))) {
			ctc_irqmask |= 1 << i;
			recalc_interrupts();
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d wants to interrupt.\n", i);
		}
	}
}

static void ctc_reti(int ctcnum)
{
	ctc_irqmask &= ~(1 << ctcnum);
	if (trace & TRACE_IRQ)
		fprintf(stderr, "Acked interrupt from CTC %d.\n", ctcnum);
}

/* After a RETI or when idle compute the status of the interrupt line and
   if we are head of the chain this time then raise our interrupt */

static int ctc_check_im2(void)
{
	if (ctc_irqmask) {
		int i;
		for (i = 0; i < 4; i++) {	/* FIXME: correct order ? */
			if (ctc_irqmask & (1 << i)) {
				uint8_t vector = ctc[0].vector & 0xF8;
				vector += 2 * i;
				if (trace & TRACE_IRQ)
					fprintf(stderr, "New live interrupt is from CTC %d vector %x.\n", i, vector);
				live_irq = IRQ_CTC + i;
				Z80INT(&cpu_z80, vector);
				return 1;
			}
		}
	}
	return 0;
}

/* Model the chains between the CTC devices */
static void ctc_receive_pulse(int i);

/* CTC 2 is chained into CTC3 */
static void ctc_pulse(int i)
{
	if (i == 2)
		ctc_receive_pulse(3);
}

/* We don't worry about edge directions just a logical pulse model */
static void ctc_receive_pulse(int i)
{
	struct z80_ctc *c = ctc + i;
	if (c->ctrl & CTC_COUNTER) {
		if (CTC_STOPPED(c))
			return;
		if (c->count >= 0x0100)
			c->count -= 0x100;	/* No scaling on pulses */
		if ((c->count & 0xFF00) == 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			c->count = c->reload << 8;
		}
	} else {
		if (c->ctrl & CTC_PULSE)
			c->ctrl &= ~CTC_PULSE;
	}
}

/* Model counters */
static void ctc_tick(unsigned int clocks)
{
	struct z80_ctc *c = ctc;
	int i;
	int n;
	int decby;

	for (i = 0; i < 4; i++, c++) {
		/* Waiting a value */
		if (CTC_STOPPED(c))
			continue;
		/* Pulse trigger mode */
		if (c->ctrl & CTC_COUNTER)
			continue;
		/* 256x downscaled */
		decby = clocks;
		/* 16x not 256x downscale - so increase by 16x */
		if (!(c->ctrl & CTC_PRESCALER))
			decby <<= 4;
		/* Now iterate over the events. We need to deal with wraps
		   because we might have something counters chained */
		n = c->count - decby;
		while (n < 0) {
			ctc_interrupt(c);
			ctc_pulse(i);
			if (c->reload == 0)
				n += 256 << 8;
			else
				n += c->reload << 8;
		}
		c->count = n;
	}
}

static void ctc_write(uint8_t channel, uint8_t val)
{
	struct z80_ctc *c = ctc + channel;
	if (c->ctrl & CTC_TCONST) {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d constant loaded with %02X\n", channel, val);
		c->reload = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == (CTC_TCONST|CTC_RESET)) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		c->ctrl &= ~CTC_TCONST|CTC_RESET;
	} else if (val & CTC_CONTROL) {
		/* We don't yet model the weirdness around edge wanted
		   toggling and clock starts */
		/* Check rule on resets */
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d control loaded with %02X\n", channel, val);
		c->ctrl = val;
		if ((c->ctrl & (CTC_TCONST|CTC_RESET)) == CTC_RESET) {
			c->count = (c->reload - 1) << 8;
			if (trace & TRACE_CTC)
				fprintf(stderr, "CTC %d constant reloaded with %02X\n", channel, val);
		}
		/* Undocumented */
		if (!(c->ctrl & CTC_IRQ) && (ctc_irqmask & (1 << channel))) {
			ctc_irqmask &= ~(1 << channel);
			if (ctc_irqmask == 0) {
				if (trace & TRACE_IRQ)
					fprintf(stderr, "CTC %d irq reset.\n", channel);
				recalc_interrupts();
			}
		}
	} else {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d vector loaded with %02X\n", channel, val);
		c->vector = val;
	}
}

static uint8_t ctc_read(uint8_t channel)
{
	uint8_t val = ctc[channel].count >> 8;
	if (trace & TRACE_CTC)
		fprintf(stderr, "CTC %d reads %02x\n", channel, val);
	return val;
}

static uint8_t wd1772_read(uint8_t addr)
{
	switch(addr & 0x07) {
	case 0x04:	/* read status */
		return wd17xx_status(wd);
	case 0x05:	/* read track */
		return wd17xx_read_track(wd);
	case 0x06:	/* read sector */
		return wd17xx_read_sector(wd);
	case 0x07:	/* read data */
		return wd17xx_read_data(wd);
	}
	return 0xFF;
}

static void wd1772_write(uint8_t addr, uint8_t val)
{
	switch(addr & 0x0F) {
	case 0x00:	/* write command */
		wd17xx_command(wd, val);
		return;
	case 0x01:	/* write track */
		wd17xx_write_track(wd, val);
		return;
	case 0x02:	/* write sector */
		wd17xx_write_sector(wd, val);
		return;
	case 0x03:	/* write data */
		wd17xx_write_data(wd, val);
		return;
	}
}

static void wd1772_update_bcr(uint8_t bcr)
{
	static uint8_t old_bcr;
	uint8_t delta = bcr ^ old_bcr;
	old_bcr = bcr;
	unsigned d = 0;

	if (delta & 0x0F) {
		/* Set drive */
		switch(bcr & 0x0F) {
		case 1:
			d = 1;
			break;
		case 2:
			d = 2;
			break;
		case 4:
			d = 3;
			break;
		case 8:
			d = 4;
			break;
		default:
			wd17xx_no_drive(wd);
			break;
		}
	}
	if (d) {
		d--;
		if (trace & TRACE_FDC)
			fprintf(stderr, "select drive %d base %d\n",
				d, sector_base[d]);
		wd17xx_set_drive(wd, d);
		wd17xx_set_sector0(wd, d, sector_base[d]);
	}
	if (delta & 0x10)
		wd17xx_set_side(wd, (bcr & 0x10) ? 1 : 0);
	/* 0x20 sets the density (not emulated)
	   0x80 sets the clock (not emulated) */
}

static uint8_t scsi_read(uint8_t addr)
{
	if (ncr) {
		if (trace & TRACE_SCSI)
			fprintf(stderr, "[%04X]", cpu_z80.PC);
		if (addr == 0x29)
			return idport;
		if (addr > 0x29)
			return 0xFF;
		if (addr <= 0x28)
			return ncr5380_read(ncr, addr);
	}
	return 0xFF;
}

static void scsi_write(uint8_t addr, uint8_t val)
{
	if (addr > 0x28 || ncr == NULL)
		return;
	if (trace & TRACE_SCSI)
		fprintf(stderr, "[%04X]", cpu_z80.PC);
	ncr5380_write(ncr, addr, val);
}

static void pp_out(uint8_t val)
{
}

static void pp_strobe(unsigned n)
{
}


/*
 *	There is a lot of partial decode NMOS here
 */
static uint8_t io_read(int unused, uint16_t addr)
{
	uint8_t r = 0xFF;
	addr &= 0xFF;
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr & 0xF0) == 0x20)
		return scsi_read(addr);
	else switch(addr & 0xC0) {
		case 0x40:
			return ctc_read((addr - 0x40) >> 4);
		case 0x80:
			recalc_interrupts();
			return sio_read(sio, addr >> 2);
		case 0xC0:
			return wd1772_read(addr);
	}
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return r;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
	addr &= 0xFF;
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr & 0xF8) == 0x00) {
		switch(addr & 3) {
		case 0x00:
			bcr = val;
			wd1772_update_bcr(val);
			break;
		case 0x01:
			pp_out(val);
			break;
		case 0x02:
			pp_strobe(1);
			break;
		case 0x03:
			pp_strobe(0);
		}
		return;
	}
	if ((addr & 0xF0) == 0x20) {
		scsi_write(addr, val);
		return;
	}
	if ((addr & 0xF0) == 0x30) {
		if (trace & TRACE_BANK)
			fprintf(stderr, "mdisk: bank set to %02X\n", val & 0x1F);
		mdisk_latch = (val & 0x1F) << 15;
		return;
	}
	if (addr == 0xF4) {	/* Secret trap door debug */
		trace = val;
		return;
	}
	switch (addr & 0xC0) {
	case 0x40:
		ctc_write((addr - 0x40) >> 4, val);
		return;
	case 0x80:
		sio_write(sio, (addr >> 2), val);
		recalc_interrupts();
		return;
	case 0xC0:
		wd1772_write(addr, val);
		return;
	}
	if (trace & TRACE_UNK)
		fprintf(stderr,
			"Unknown write to port %04X of %02X\n", addr, val);
}

static void reti_event(void)
{
	int r;
	switch(live_irq) {
	case IRQ_SIO:
		sio_reti(sio);
		break;
	case IRQ_CTC:
	case IRQ_CTC + 1:
	case IRQ_CTC + 2:
	case IRQ_CTC + 3:
		ctc_reti(live_irq - IRQ_CTC);
		break;
	}
	live_irq = 0;
	r = sio_check_im2(sio);
	if (r == -1)
		ctc_check_im2();
	else {
		live_irq = IRQ_SIO;
		Z80INT(&cpu_z80, r);
	}
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
	fprintf(stderr, "littleboard: [-f] [i idport] [-s path] [-r path] [-d debug] [-A|B|C|D disk]\n");
	exit(EXIT_FAILURE);
}

static void insert_floppy(int unit, const char *path)
{
	struct stat st;
	if (stat(path, &st) == -1) {
		perror(path);
		exit(1);
	}
	/* Guess the media from the size. We only
	   model double density. The Ampro disks number sectors
	   from 17 on DS media as an autodetect scheme */
	switch(st.st_size) {
	case 204800:
		/* 40 track ss/dd */
		wd17xx_attach(wd, unit, path, 1, 40, 10, 512);
		sector_base[unit] = 1;
		break;
	case 409600:
		/* 40 track ds/dd (could be ss 80 in theory ??) */
		wd17xx_attach(wd, unit, path, 2, 40, 10, 512);
		sector_base[unit] = 17;
		break;
	case 819200:
		/* 80 track ds/dd */
		wd17xx_attach(wd, unit, path, 2, 80, 10, 1024);
		sector_base[unit] = 17;
		break;
	default:
		fprintf(stderr, "littleboard: unknown media size.\n");
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	int l;
	char *rompath = "ampro.rom";
	char *diskpath = NULL;
	static char *fdpath[4] = { NULL, NULL, NULL, NULL };

	while ((opt = getopt(argc, argv, "d:fi:r:s:A:B:C:D:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 's':
			diskpath = optarg;
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
			fdpath[opt - 'A'] = optarg;
			break;
		case 'i':
			idport = atoi(optarg);
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
	l = read(fd, rom, 32768);
	switch(l) {
	case 0x1000:
	case 0x2000:
	case 0x4000:
	case 0x8000:
		break;
	default:
		fprintf(stderr, "littleboard: invalid EPROM size.\n");
		exit(1);
	}
	close(fd);
	eprom_mask = l - 1;

	sio = sio_create();
	sio_trace(sio, 0, !!(trace & TRACE_SIO));
	sio_trace(sio, 1, !!(trace & TRACE_SIO));
	sio_attach(sio, 0, &console);
	sio_attach(sio, 1, &console_wo);
	sio_reset(sio);
	ctc_init();

	wd = wd17xx_create(1772);
	/* Not clear what we do here - probably we need to add support
	   for proper disk metadata formats as the disks came with some
	   peculiar setings - like sectors numbering from 16 and 1K or 512
	   byte sector options */
	if (fdpath[0])
		insert_floppy(0, fdpath[0]);
	if (fdpath[1])
		insert_floppy(1, fdpath[1]);
	if (fdpath[2])
		insert_floppy(2, fdpath[2]);
	if (fdpath[3])
		insert_floppy(3, fdpath[3]);
	wd17xx_trace(wd, trace & TRACE_FDC);

	if (diskpath) {
		sasi = sasi_bus_create();
		sasi_disk_attach(sasi, 0, diskpath, 512);
		sasi_bus_reset(sasi);
		ncr = ncr5380_create(sasi);
		ncr5380_trace(ncr, trace & TRACE_SCSI);
	}

	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 50000000L;	/* 50ms */

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
		unsigned n;
		for (l = 0; l < 10; l++) {
			int i;
			/* 200000 T states */
			for (i = 0; i < 500; i++) {
				Z80ExecuteTStates(&cpu_z80, 400);
				sio_timer(sio);
				ctc_tick(400);
				for (n = 0; n < 200;n++) {
					ctc_receive_pulse(0);
					ctc_receive_pulse(1);
				}
			}
			if (ncr)
				ncr5380_activity(ncr);
			wd17xx_tick(wd, 5);
			/* Do 5ms of I/O and delays */
			if (!fast)
				nanosleep(&tc, NULL);
			if (int_recalc) {
				/* If there is no pending Z80 vector IRQ but we think
				   there now might be one we use the same logic as for
				   reti */
				if (!live_irq)
					reti_event();
				/* Clear this after because reti_event may set the
				   flags to indicate there is more happening. We will
				   pick up the next state changes on the reti if so */
				if (!(cpu_z80.IFF1|cpu_z80.IFF2))
					int_recalc = 0;
			}
		}
	}
	exit(0);
}
