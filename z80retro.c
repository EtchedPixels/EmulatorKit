/*
 *	https://oshwlab.com/peterw8102/simple-z80
 *
 *	Platform features
 *
 *	Z80 at 14.7MHz
 *	Zilog SIO/2 at 0x80-0x83 at 1/2 clock
 *	Memory banking Zeta style 16K page at 0x60-63
 *	Config port at 64
 *	RTC on I2C
 *	SD on bitbang SPI
 *
 *	I2C use: 0x64, 0x65 (address bit 0 is clock)
 *	SPI use: 0x64 for chip selects
 *		0x68, 0x69 SPI data and (addr bit 0) SPI clock
 *
 *	Limitations:
 *	+ Only the first SDCard is supported
 *	+ Only emulates the main CPU card, not VDU or PIO cards
 *	+ Doesn't simulate programming the Flash
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

#include "i2c_bitbang.h"
#include "i2c_ds1307.h"
#include "sdcard.h"
#include "system.h"
#include "libz80/z80.h"
#include "z80dis.h"
#include "event.h"
#include "serialdevice.h"
#include "ttycon.h"
#include "z80sio.h"

static uint8_t ramrom[256 * 16384];

static unsigned int bankreg[4];
static uint8_t genio_data;		/* Generic I/O bits */

static uint8_t fast = 0;
static uint8_t int_recalc = 0;

static struct sdcard *sdcard;
static struct i2c_bus *i2cbus;		/* I2C bus */
static struct ds1307 *rtcdev;		/* DS1307 RTC */
static struct z80_sio *sio;

static uint16_t tstate_steps = 730;	/* 14.4MHz speed */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

#define IRQ_SIO		1
#define IRQ_SIOB	2
#define IRQ_CTC		3	/* 3 4 5 6 */
/* TOOD: PIO */

/* Value of the 3 configuration DIP switches */
static uint8_t dip_switch;

static Z80Context cpu_z80;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_ROM	0x000004
#define TRACE_UNK	0x000008
#define TRACE_CPU	0x000010
#define TRACE_BANK	0x000020
#define TRACE_SIO	0x000040
#define TRACE_CTC	0x000080
#define TRACE_IRQ	0x000100
#define TRACE_SPI	0x000200
#define TRACE_SD	0x000400
#define TRACE_I2C	0x000800
#define TRACE_RTC	0x001000

static int trace = 0;

static void reti_event(void);

/* TODO : > 3F don't exist */
static uint8_t *map_addr(uint16_t addr, unsigned is_write)
{
	unsigned int bank = (addr & 0xC000) >> 14;
	if (!(genio_data & 1)) {
		if (is_write)
			return NULL;
		return ramrom + addr;
	}
	if (bankreg[bank] < 0x20 && is_write)
		return NULL;
	return ramrom + 16384 * bankreg[bank] + (addr & 0x3FFF);
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

/* Model the chains between the CTC devices */
/* TODO: DS1307 can provide a clock to the CTC */
static void ctc_pulse(int i)
{
}
#if 0
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
#endif

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

static uint8_t bitcnt;
static uint8_t txbits, rxbits;
static uint8_t genio_txbit;
static uint8_t spi_addr;
static uint8_t spi_data;
static uint8_t i2c_clk;

static void spi_clock_high(void)
{
	txbits <<= 1;
	txbits |= spi_data;
	bitcnt++;
	if (bitcnt == 8) {
		rxbits = sd_spi_in(sdcard, txbits);
		if (trace & TRACE_SPI)
			fprintf(stderr, "spi %02X | %02X\n", rxbits, txbits);
		bitcnt = 0;
	}
}

static void spi_clock_low(void)
{
	genio_txbit = (rxbits & 0x80) ? 1 : 0;
	rxbits <<= 1;
	rxbits |= 1;
}

static void genio_write(uint16_t addr, uint8_t val)
{
	uint8_t delta = genio_data ^ val;
	genio_data = val;
	if (delta & 4) {
		if (genio_data & 4)
			sd_spi_lower_cs(sdcard);
		else
			sd_spi_raise_cs(sdcard);
	}
	/* I2C clock or data change. Provide new driven bits to bus */
	if ((delta & 2) || (addr & 1) != i2c_clk) {
		unsigned n = 0;
		i2c_clk = addr  & 1;
		/* The i2c bus interface and genio bits are not the same */
		if (val & 2)
			n |= I2C_DATA;
		if (i2c_clk)
			n |= I2C_CLK;
		i2c_write(i2cbus, n);
	}
}

static uint8_t genio_read(uint16_t addr)
{
	unsigned n = (i2c_read(i2cbus) & I2C_DATA) ? 2 : 0;
	return dip_switch | 0x08 | n | (genio_data & 0x04) | genio_txbit;
}

static void spi_write(uint16_t addr, uint8_t val)
{
	uint8_t delta = spi_addr ^ addr;
	spi_addr = addr & 1;
	spi_data = val & 1;
	if (delta & 1) {
		if (spi_addr)
			spi_clock_high();
		else
			spi_clock_low();
	}
}

static uint8_t sio_port[4] = {
	SIOA_D,
	SIOB_D,
	SIOA_C,
	SIOB_C
};

uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	addr &= 0xFF;

	if (addr >= 0x80 && addr <= 0x87)
		return sio_read(sio, sio_port[addr & 3]);
	if (addr >= 0x40 && addr <= 0x43)
		return ctc_read(addr & 3);
	if (addr >= 0x64 && addr <= 0x65)
		return genio_read(addr);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;	/* FF is what my actual board floats at */
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	addr &= 0xFF;
	if (addr >= 0x80 && addr <= 0x87)
		sio_write(sio, sio_port[addr & 3], val);
	else if (addr >= 0x60 && addr <= 0x63) {
		bankreg[addr & 3] = val;
		if (trace & TRACE_BANK)
			fprintf(stderr, "Bank %d set to %02X [%02X %02X %02X %02X]\n", addr & 3, val,
				bankreg[0], bankreg[1], bankreg[2], bankreg[3]);
	}
	else if (addr >= 0x40 && addr <= 0x43)
		ctc_write(addr & 3, val);
	else if (addr >= 0x64 && addr <= 0x67)
		genio_write(addr, val);
	else if (addr >= 0x68 && addr <= 0x6B)
		spi_write(addr, val);
	else if (addr == 0xFD) {
		trace &= 0xFF00;
		trace |= val;
		fprintf(stderr, "trace set to %04X\n", trace);
	} else if (addr == 0xFE) {
		trace &= 0xFF;
		trace |= val << 8;
		printf("trace set to %d\n", trace);
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

static void poll_irq_event(void)
{
	if (!live_irq) {
		int v = sio_check_im2(sio);
		if (v >= 0) {
			Z80INT(&cpu_z80, v);
			live_irq = IRQ_SIO;
		} else  {
			ctc_check_im2();
		}
		/* TODO: PIO */
	}
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
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
	fprintf(stderr,
		"z80retro: [-b cpath] [-c config] [-r rompath] [-S sdpath] [-N nvpath] [-f] [-d debug]\n"
			"   config:  State of DIP switches (0-7)\n"
			"   rompath: 512K binary file\n"
			"   sdpath:  Path to file containing SDCard data\n"
			"   debug:   Comma separated list of: MEM,IO,INFO,UNK,CPU,BANK,SIO,CTC,IRQ,SPI,SD,I2C,RTC\n"
			"   nvpath:  file to store DS1307+ RTC non-volatile memory\n"
			);
	exit(EXIT_FAILURE);
	/*
		"   cpath:   Path to directory holding files to serve over SIO port B\n"
		"If -b is specified then the emulator runs a file server function allowing the\n"
		"Z80 to download files directly from the host computer."
	*/
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "z80retro.rom";
	char *nvpath = "z80retrom.nvram";
	char *sdpath = NULL;

	while ((opt = getopt(argc, argv, "d:fr:S:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'S':
			sdpath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'N':
			nvpath = optarg;
			break;
		case 'c':
			/* State of the 3 DIP switches used for configuration,
			 * shifted to the right place in  */
			dip_switch = (atoi(optarg) & 0x7) << 5;
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
	if (read(fd, &ramrom[0], 524288) != 524288) {
		fprintf(stderr, "z80retro: banked rom image should be 512K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	sdcard = sd_create("sd0");
	if (sdpath) {
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
	}
	if (trace & TRACE_SD)
		sd_trace(sdcard, 1);

	i2cbus = i2c_create();
	i2c_trace(i2cbus, (trace & TRACE_I2C) != 0);

	rtcdev = rtc_create(i2cbus);
	rtc_trace(rtcdev, (trace & TRACE_RTC) != 0);
	rtc_load(rtcdev, nvpath);

	ui_init();

	sio = sio_create();
	sio_reset(sio);
	if (trace & TRACE_SIO) {
		sio_trace(sio, 0, 1);
		sio_trace(sio, 1, 1);
	}
	sio_attach(sio, 0, &console);
	sio_attach(sio, 1, &console_wo);

	ctc_init();

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

	/* We run 7372000 t-states per second */
	/* We run 365 cycles per I/O check, do that 50 times then poll the
	   slow stuff and nap for 20ms to get 50Hz on the TMS99xx */
	while (!emulator_done) {
		if (cpu_z80.halted && ! cpu_z80.IFF1) {
			/* HALT with interrupts disabled, so nothing left
			   to do, so exit simulation. If NMI was supported,
			   this might have to change. */
			emulator_done = 1;
			break;
		}
		int i;
		/* 36400 T states for base RC2014 - varies for others */
		for (i = 0; i < 40; i++) {
			int j;
			for (j = 0; j < 100; j++) {
				Z80ExecuteTStates(&cpu_z80, (tstate_steps + 5)/ 10);
				sio_timer(sio);
			}
			ctc_tick(tstate_steps);
			/* We want to run UI events regularly it seems */
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
	if (nvpath!=NULL)
		rtc_save(rtcdev, nvpath);
	exit(0);
}
