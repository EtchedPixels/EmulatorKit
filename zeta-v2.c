/*
 *	Zeta V2
 *	Z80 CPU
 *	CTC at 0x20-27 (0 chained into 1) 2 and 3 are wired to the
 *		UART and PPI). 0 is tc/g from the 921.6Khz clock
 *	FDC CCR at 28-2F
 *	FDC status at 30/32/34/36
 *	FDC data at 31/33/35/37
 *	FDC DOR at 38-3F (read triggers /DACK /TC)
 *	PPI at 60-67
 *	16x50 UART at 68-6F	@ 1.8432MHz
 *	RTC at 70-77 (DS1302)
 *	78-7B paging 		0-31 ROM 32-63 SRAM
 *	7C-7F paging enable
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
#include <sys/select.h>

#include "system.h"
#include "libz80/z80.h"
#include "z80dis.h"
#include "lib765/include/765.h"

#include "pprop.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "16x50.h"

static struct ppide *ppide;
static struct pprop *pprop;
static FDC_PTR fdc;
static FDRV_PTR drive_a, drive_b;
struct uart16x50 *uart;
struct rtc *rtc;

static uint8_t ramrom[1048576];
static uint8_t bankreg[4];
static uint8_t banken;

static uint8_t fast = 0;
static uint8_t int_recalc = 0;

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

#define IRQ_CTC		1	/* 1 2 3 4 */

static Z80Context cpu_z80;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_ROM	0x000004
#define TRACE_UNK	0x000008
#define TRACE_CPU	0x000010
#define TRACE_BANK	0x000020
#define TRACE_UART	0x000040
#define TRACE_CTC	0x000080
#define TRACE_IRQ	0x000100
#define TRACE_PPIDE	0x000200
#define TRACE_FDC	0x000400
#define TRACE_PROP	0x000800
#define TRACE_RTC	0x001000

static int trace = 0;

static void reti_event(void);

static uint8_t *map_addr(uint16_t addr, unsigned is_write)
{
	unsigned off = addr & 0x3FFF;
	unsigned bank = addr >> 14;
	if (banken == 0)
		return ramrom + addr;
	if (is_write && bankreg[bank] < 32)
		return NULL;
	return ramrom + (bankreg[bank] << 14) + off;
}

static uint8_t do_mem_read(uint16_t addr, int quiet)
{
	uint8_t *p = map_addr(addr, 0);
	uint8_t r = *p;
	if ((trace & TRACE_MEM) && !quiet)
		fprintf(stderr, "R %04x = %02X\n", addr, r) ;
	return r;
}

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	uint8_t *p = map_addr(addr, 1);
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04x = %02X\n",
			addr, val);
	if (p)
		*p = val;
	else if (trace & TRACE_MEM)	/* ROM writes go nowhere */
		fprintf(stderr, "[Discarded: ROM]\n");
}

uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r = do_mem_read(addr, 0);

	if (cpu_z80.M1) {
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

void recalc_interrupts(void)
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
	uint8_t irq;		/* Only valid for channel 0, so we know
				   if we must wait for a RETI before doing
				   a further interrupt */
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
	if (ctc_irqmask & (1 << ctcnum)) {
		ctc_irqmask &= ~(1 << ctcnum);
		if (trace & TRACE_IRQ)
			fprintf(stderr, "Acked interrupt from CTC %d.\n", ctcnum);
	}
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

static void ctc_pulse(unsigned channel);

/* Model the chains between the CTC devices */
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
				if (live_irq == IRQ_CTC + channel)
					live_irq = 0;
			}
		}
	} else {
		if (trace & TRACE_CTC)
			fprintf(stderr, "CTC %d vector loaded with %02X\n", channel, val);
		/* Only works on channel 0 */
		if (channel == 0)
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

static void ctc_pulse(unsigned channel)
{
	if (channel == 0)
		ctc_receive_pulse(1);
}


static void fdc_log(int debuglevel, char *fmt, va_list ap)
{
	if ((trace & TRACE_FDC) || debuglevel == 0)
		vfprintf(stderr, "fdc: ", ap);
}

/* Nothing to do */
void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
}

uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;

	switch(addr & 0xF8) {
	case 0x20:
		return ctc_read(addr & 3);
	case 0x30:
		if (addr & 1)
			return fdc_read_data(fdc);
		return fdc_read_ctrl(fdc);
	case 0x38:
		fdc_set_terminal_count(fdc, 0);
		fdc_set_terminal_count(fdc, 1);
		return 0x78;
	case 0x60:
		if (pprop)
			return pprop_read(pprop, addr & 3);
		return ppide_read(ppide, addr & 3);
	case 0x68:
		return uart16x50_read(uart, addr & 7);
	case 0x70:
		return rtc_read(rtc);
	}
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0x78;	/* 78 is what my actual board floats at */
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	switch(addr & 0xF8) {
	case 0x20:
		ctc_write(addr & 3, val);
		break;
	case 0x28:
		fdc_write_drr(fdc, val);
		break;
	case 0x30:
		if (addr & 1)
			fdc_write_data(fdc, val);
		else
			/* ?? */;
		break;
	case 0x38:
		fdc_write_dor(fdc, val);
		break;
	case 0x60:
		if (pprop)
			pprop_write(pprop, addr & 3, val);
		else
			ppide_write(ppide, addr & 3, val);
		break;
	case 0x68:
		uart16x50_write(uart, addr & 7, val);
		break;
	case 0x70:
		rtc_write(rtc, val);
		break;
	case 0x78:
		if (addr >= 0x7C) {
			banken = val & 1;
			break;
		} else {
			bankreg[addr & 3] = val & 0x3F;
			if (trace & TRACE_BANK)
				fprintf(stderr, "Bank %d set to %02X [%02X %02X %02X %02X]\n", addr & 3, val,
					bankreg[0], bankreg[1], bankreg[2], bankreg[3]);
		}
		break;
	case 0xF8:
		if (addr == 0xFD) {
			trace &= 0xFF00;
			trace |= val;
			fprintf(stderr, "trace set to %04X\n", trace);
			break;
		} else if (addr == 0xFE) {
			trace &= 0xFF;
			trace |= val << 8;
			fprintf(stderr, "trace set to %d\n", trace);
			break;
		}
	default:
		if (trace & TRACE_UNK)
			fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
	}
}

static void ctc_irq_check(void)
{
	static unsigned last_ui;
	unsigned ui = uart16x50_irq_pending(uart);
	if (ui && !last_ui)
		ctc_receive_pulse(2);
	last_ui = ui;
}

static void poll_irq_event(void)
{
	if (!live_irq)
		ctc_check_im2();
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
	switch(live_irq) {
	case IRQ_CTC:
	case IRQ_CTC + 1:
	case IRQ_CTC + 2:
	case IRQ_CTC + 3:
		ctc_reti(live_irq - IRQ_CTC);
		break;
	}
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
	fprintf(stderr, "zeta-v2: [-r rompath] [-I ide] [-A disk] [-B disk][-f] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "zeta-v2.rom";
	char *idepath = NULL;
	char *patha = NULL;
	char *pathb = NULL;
	char *ppath = NULL;

	while ((opt = getopt(argc, argv, "d:fr:I:A:B:P:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'I':
			idepath = optarg;
			break;
		case 'P':
			ppath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'A':
			patha = optarg;
			break;
		case 'B':
			pathb = optarg;
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
	if (read(fd, &ramrom[0], 524288) != 524288) {
		fprintf(stderr, "zeta-v2: banked rom image should be 512K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	rtc = rtc_create();
	rtc_trace(rtc, trace & TRACE_RTC);

	ppide = ppide_create("ppi0");
	if (idepath) {
		if (idepath) {
			fd = open(idepath, O_RDWR);
			if (fd == -1) {
				perror(idepath);
			} else
				ppide_attach(ppide, 0, fd);
			if (trace & TRACE_PPIDE)
				ppide_trace(ppide, 1);
		}
	}
	ppide_reset(ppide);
	if (ppath) {
		pprop = pprop_create(ppath);
		pprop_set_input(pprop, 0);
		pprop_trace(pprop, trace & TRACE_PROP);
	}


	ctc_init();
	uart = uart16x50_create();
	uart16x50_set_input(uart, 1);

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

	while (!emulator_done) {
		if (cpu_z80.halted && ! cpu_z80.IFF1) {
			/* HALT with interrupts disabled, so nothing left
			   to do, so exit simulation. If NMI was supported,
			   this might have to change. */
			emulator_done = 1;
			break;
		}
		int i;
		/* ROMWBW probes the CTC tightly so we have to emulate closely */
		/* We run at 16MHz, so 320,000 per 20ms */
		for (i = 0; i < 32000; i++) {
			Z80ExecuteTStates(&cpu_z80, 10);
			ctc_tick(10);
			uart16x50_event(uart);
			ctc_irq_check();
			/* Runs at 0.916MHz properly - FIXME */
			ctc_receive_pulse(0);
		}

		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending Z80 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
			if (!live_irq)
				poll_irq_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z80.IFF1|cpu_z80.IFF2))
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
