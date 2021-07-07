/*
 *	Platform features
 *
 *	Z180 at 7.372MHz
 *	512K/512K flat memory card
 *	RTC at 0x0C
 *
 *	Add support for using real CF card
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

#include "system.h"
#include "libz180/z180.h"
#include "lib765/include/765.h"
#include "z180_io.h"

#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "sdcard.h"
#include "tms9918a.h"
#include "tms9918a_render.h"
#include "w5100.h"
#include "z80dis.h"
#include "zxkey.h"

static uint8_t ramrom[1024 * 1024];	/* Low 512K is ROM */

#define CPUBOARD_Z180		0

static uint8_t cpuboard = CPUBOARD_Z180;

static uint8_t have_pio = 0;
static uint8_t fast = 0;
static uint8_t int_recalc = 0;
static uint8_t wiznet = 0;
static uint8_t has_tms;
static struct ppide *ppide;
static struct sdcard *sdcard;
static FDC_PTR fdc;
static FDRV_PTR drive_a, drive_b;
static struct tms9918a *vdp;
static struct tms9918a_renderer *vdprend;
struct zxkey *zxkey;
static struct z180_io *io;

static uint16_t tstate_steps = 369;	/* RC2014 speed */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

static Z180Context cpu_z180;

static nic_w5100_t *wiz;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_UNK	0x000004
#define TRACE_RTC	0x000008
#define TRACE_CPU	0x000010
#define TRACE_CPU_IO	0x000020
#define TRACE_IRQ	0x000040
#define TRACE_SD	0x008080
#define TRACE_PPIDE	0x000100
#define TRACE_TMS9918A  0x000200
#define TRACE_FDC	0x000400
#define TRACE_IDE	0x000800
#define TRACE_SPI	0x001000

static int trace = 0;

static void reti_event(void);

static uint8_t do_mem_read0(uint16_t addr, int quiet)
{
	uint32_t pa = z180_mmu_translate(io, addr);
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X[%06X] -> %02X\n", addr, pa, ramrom[pa]);
	return ramrom[pa];
}

static void mem_write0(uint16_t addr, uint8_t val)
{
	uint32_t pa = z180_mmu_translate(io, addr);
	if (pa < 0x80000) {
		if (trace & TRACE_MEM)
			fprintf(stderr, "W %04X[%06X] *ROM*\n",
				addr, pa);
		return;
	}
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %04X[%06X] <- %02X\n", addr, pa, val);
	ramrom[pa] = val;
}

uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r;

	switch (cpuboard) {
	case CPUBOARD_Z180:
		r = do_mem_read0(addr, 0);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
	if (cpu_z180.M1) {
		/* DD FD CB see the Z80 interrupt manual */
		if (r == 0xDD || r == 0xFD || r == 0xCB) {
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

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z180:
		mem_write0(addr, val);
		break;
	default:
		fputs("invalid cpu type.\n", stderr);
		exit(1);
	}
}

static unsigned int nbytes;

uint8_t z80dis_byte(uint16_t addr)
{
	uint8_t r = do_mem_read0(addr, 1);
	fprintf(stderr, "%02X ", r);
	nbytes++;
	return r;
}

uint8_t z80dis_byte_quiet(uint16_t addr)
{
	return do_mem_read0(addr, 1);
}

static void rc2014_trace(unsigned unused)
{
	static uint32_t lastpc = -1;
	char buf[256];

	if ((trace & TRACE_CPU) == 0)
		return;
	nbytes = 0;
	/* Spot XXXR repeating instructions and squash the trace */
	if (cpu_z180.M1PC == lastpc && z80dis_byte_quiet(lastpc) == 0xED &&
		(z80dis_byte_quiet(lastpc + 1) & 0xF4) == 0xB0) {
		return;
	}
	lastpc = cpu_z180.M1PC;
	fprintf(stderr, "%04X: ", lastpc);
	z80_disasm(buf, lastpc);
	while(nbytes++ < 6)
		fprintf(stderr, "   ");
	fprintf(stderr, "%-16s ", buf);
	fprintf(stderr, "[ %02X:%02X %04X %04X %04X %04X %04X %04X ]\n",
		cpu_z180.R1.br.A, cpu_z180.R1.br.F,
		cpu_z180.R1.wr.BC, cpu_z180.R1.wr.DE, cpu_z180.R1.wr.HL,
		cpu_z180.R1.wr.IX, cpu_z180.R1.wr.IY, cpu_z180.R1.wr.SP);
}

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

void recalc_interrupts(void)
{
	int_recalc = 1;
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	uint8_t r =  ide_read8(ide0, addr);
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide read %d = %02X\n", addr, r);
	return r;
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IDE)
		fprintf(stderr, "ide write %d = %02X\n", addr, val);
	ide_write8(ide0, addr, val);
}

struct rtc *rtc;

/* Software SPI test: one device for now */

#include "bitrev.h"

uint8_t z180_csio_write(struct z180_io *io, uint8_t bits)
{
	uint8_t r;

	if (sdcard == NULL)
		return 0xFF;

	r = bitrev[sd_spi_in(sdcard, bitrev[bits])];
	if (trace & TRACE_SPI)
		fprintf(stderr,	"[SPI %02X:%02X]\n", bits, r);
	return r;
}

static void fdc_log(int debuglevel, char *fmt, va_list ap)
{
	if ((trace & TRACE_FDC) || debuglevel == 0)
		vfprintf(stderr, "fdc: ", ap);
}

static void fdc_write(uint8_t addr, uint8_t val)
{
	switch(addr) {
	case 1:	/* Data */
		fprintf(stderr, "FDC Data: %02X\n", val);
		fdc_write_data(fdc, val);
		break;
	case 2:	/* DOR */
		fprintf(stderr, "FDC DOR %02X [", val);
		if (val & 0x80)
			fprintf(stderr, "SPECIAL ");
		else
			fprintf(stderr, "AT/EISA ");
		if (val & 0x20)
			fprintf(stderr, "MOEN2 ");
		if (val & 0x10)
			fprintf(stderr, "MOEN1 ");
		if (val & 0x08)
			fprintf(stderr, "DMA ");
		if (!(val & 0x04))
			fprintf(stderr, "SRST ");
		if (!(val & 0x02))
			fprintf(stderr, "DSEN ");
		if (val & 0x01)
			fprintf(stderr, "DSEL1");
		else
			fprintf(stderr, "DSEL0");
		fprintf(stderr, "]\n");
		fdc_write_dor(fdc, val);
#if 0		
		if ((val & 0x21) == 0x21)
			fdc_set_motor(fdc, 2);
		else if ((val & 0x11) == 0x10)
			fdc_set_motor(fdc, 1);
		else
			fdc_set_motor(fdc, 0);
#endif			
		break;
	case 3:	/* DCR */
		fprintf(stderr, "FDC DCR %02X [", val);
		if (!(val & 4))
			fprintf(stderr, "WCOMP");
		switch(val & 3) {
		case 0:
			fprintf(stderr, "500K MFM RPM");
			break;
		case 1:
			fprintf(stderr, "250K MFM");
			break;
		case 2:
			fprintf(stderr, "250K MFM RPM");
			break;
		case 3:
			fprintf(stderr, "INVALID");
		}
		fprintf(stderr, "]\n");
		fdc_write_drr(fdc, val & 3);	/* TODO: review */
		break;
	case 4:	/* TC */
		fdc_set_terminal_count(fdc, 0);
		fdc_set_terminal_count(fdc, 1);
		fprintf(stderr, "FDC TC\n");
		break;
	case 5:	/* RESET */
		fprintf(stderr, "FDC RESET\n");
		break;
	default:
		fprintf(stderr, "FDC bogus %02X->%02X\n", addr, val);
	}
}

static uint8_t fdc_read(uint8_t addr)
{
	uint8_t val = 0x78;
	switch(addr) {
	case 0:	/* Status*/
		fprintf(stderr, "FDC Read Status: ");
		val = fdc_read_ctrl(fdc);
		break;
	case 1:	/* Data */
		fprintf(stderr, "FDC Read Data: ");
		val = fdc_read_data(fdc);
		break;
	case 4:	/* TC */
		fprintf(stderr, "FDC TC: ");
		break;
	case 5:	/* RESET */
		fprintf(stderr, "FDC RESET: ");
		break;
	default:
		fprintf(stderr, "FDC bogus read %02X: ", addr);
	}
	fprintf(stderr, "%02X\n", val);
	return val;
}


static uint8_t io_read_2014(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if (z180_iospace(io, addr))
		return z180_read(io, addr);
	if ((addr & 0xFF) == 0xBA) {
		return 0xCC;
	}
	if (zxkey && (addr & 0xFC) == 0xFC)
		return zxkey_scan(zxkey, addr);

	addr &= 0xFF;
	if (addr >= 0x48 && addr < 0x50) 
		return fdc_read(addr & 7);
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x27 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read(rtc);
	if ((addr == 0x98 || addr == 0x99) && vdp)
		return tms9918a_read(vdp, addr & 1);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void io_write_2014(uint16_t addr, uint8_t val, uint8_t known)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);

	if (z180_iospace(io, addr)) {
		z180_write(io, addr, val);
		known = 1;
	}
	if ((addr & 0xFF) == 0xBA) {
		/* Quart */
		return;
	}
	addr &= 0xFF;
	if (addr >= 0x48 && addr < 0x50)
		fdc_write(addr & 7, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x27 && ide == 2)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr == 0x0C && rtc)
		rtc_write(rtc, val);
	else if ((addr == 0x98 || addr == 0x99) && vdp)
		tms9918a_write(vdp, addr & 1, val);
	else if (addr == 0xFD) {
		trace &= 0xFF00;
		trace |= val;
		printf("trace set to %04X\n", trace);
	} else if (addr == 0xFE) {
		trace &= 0xFF;
		trace |= val << 8;
		printf("trace set to %d\n", trace);
	} else if (!known && (trace & TRACE_UNK))
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	switch (cpuboard) {
	case CPUBOARD_Z180:
		io_write_2014(addr, val, 0);
		break;
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

uint8_t io_read(int unused, uint16_t addr)
{
	switch (cpuboard) {
	case CPUBOARD_Z180:
		return io_read_2014(addr);
	default:
		fprintf(stderr, "bad cpuboard\n");
		exit(1);
	}
}

static void poll_irq_event(void)
{
	if (vdp && tms9918a_irq_pending(vdp))
		Z180INT(&cpu_z180, 0xFF);
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
	live_irq = 0;
	poll_irq_event();
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

static void usage(void)
{
	fprintf(stderr, "rc2014-z180: [-f] [-i idepath] [-R] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "rc2014-z180.rom";
	char *sdpath = NULL;
	char *idepath = NULL;
	char *patha = NULL, *pathb = NULL;

	uint8_t *p = ramrom;
	while (p < ramrom + sizeof(ramrom))
		*p++= rand();

	while ((opt = getopt(argc, argv, "cd:fF:i:I:r:sRS:Twz")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'S':
			sdpath = optarg;
			have_pio = 1;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
			break;
		case 'I':
			ide = 2;
			idepath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'R':
			rtc = rtc_create();
			break;
		case 'w':
			wiznet = 1;
			break;
		case 'F':
			if (pathb) {
				fprintf(stderr, "rc2014: too many floppy disks specified.\n");
				exit(1);
			}
			if (patha)
				pathb = optarg;
			else
				patha = optarg;
			break;
		case 'z':
			zxkey = zxkey_create();
			break;
		case 'T':
			has_tms = 1;
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
	if (read(fd, ramrom, 524288) != 524288) {
		fprintf(stderr, "rc2014: banked rom image should be 512K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide == 1 ) {
		ide0 = ide_allocate("cf");
		if (ide0) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			}
			if (ide_attach(ide0, 0, ide_fd) == 0) {
				ide = 1;
				ide_reset_begin(ide0);
			}
		} else
			ide = 0;
	}

	/* FIXME: merge IDE handling once cf is a driver */
	if (ide == 2) {
		ppide = ppide_create("ppide");
		int ide_fd = open(idepath, O_RDWR);
		if (ide_fd == -1) {
			perror(idepath);
			ide = 0;
		} else
			ppide_attach(ppide, 0, ide_fd);
		if (trace & TRACE_PPIDE)
			ppide_trace(ppide, 1);
	}
	if (sdpath) {
		sdcard = sd_create("sd0");
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
		if (trace & TRACE_SD)
			sd_trace(sdcard, 1);
	}

	io = z180_create();
	z180_set_input(io, 0, 1);
	z180_trace(io, trace & TRACE_CPU_IO);

	if (rtc)
		rtc_trace(rtc, trace & TRACE_RTC);

	if (has_tms) {
		vdp = tms9918a_create();
		tms9918a_trace(vdp, !!(trace & TRACE_TMS9918A));
		vdprend = tms9918a_renderer_create(vdp);
	}
	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

	fdc = fdc_new();

	lib765_register_error_function(fdc_log);

	if (patha) {
		drive_a = fd_newdsk();
		fd_settype(drive_a, FD_35);
		fd_setheads(drive_a, 2);
		fd_setcyls(drive_a, 80);
		fdd_setfilename(drive_a, patha);
	} else
		drive_a = fd_new();

	if (pathb) {
		drive_b = fd_newdsk();
		fd_settype(drive_a, FD_35);
		fd_setheads(drive_a, 2);
		fd_setcyls(drive_a, 80);
		fdd_setfilename(drive_a, pathb);
	} else
		drive_b = fd_new();

	fdc_reset(fdc);
	fdc_setisr(fdc, NULL);

	fdc_setdrive(fdc, 0, drive_a);
	fdc_setdrive(fdc, 1, drive_b);

	/* 2.5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 2500000L;

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

	Z180RESET(&cpu_z180);
	cpu_z180.ioRead = io_read;
	cpu_z180.ioWrite = io_write;
	cpu_z180.memRead = mem_read;
	cpu_z180.memWrite = mem_write;
	cpu_z180.trace = rc2014_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 50 times then poll the
	   slow stuff and nap for 2.5ms to get 50Hz on the TMS99xx */
	while (!emulator_done) {
		int i;
		/* 36400 T states for base RC2014 - varies for others */
		for (i = 0; i < 50; i++) {
			int j;
			for (j = 0; j < 10; j++)
				Z180ExecuteTStates(&cpu_z180, (tstate_steps + 5)/10);
			fdc_tick(fdc);
			/* We want to run UI events regularly it seems */
			ui_event();
			z180_event(io);
		}

		/* 50Hz which is near enough */
		if (vdp) {
			tms9918a_rasterize(vdp);
			tms9918a_render(vdprend);
		}
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending Z180 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
			if (!live_irq)
				poll_irq_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z180.IFF1|cpu_z180.IFF2))
				int_recalc = 0;
		}
	}
	fd_eject(drive_a);
	fd_eject(drive_b);
	fdc_destroy(&fdc);
	fd_destroy(&drive_a);
	fd_destroy(&drive_b);
	exit(0);
}
