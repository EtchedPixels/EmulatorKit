/*
 *	Platform features
 *
 *	Keyboard via RS422
 *	Control port				@0x00
 *		0: set to disable ROM
 *		1: video switch
 *		2: strobe printer
 *		3: green led
 *		4: red led
 *		5: yellow led
 *		6-7: unused
 *
 *		(also drives 3 leds, an rf switch and printer strobe)
 *	AY-3-8910 (not emulated)		@0x40 (data) @0x41 (addr)
 *	- The AY-3-8910 I/O ports are used as follows
 *	0x0E: interrupt enables
 *		Match priority (7 is hcca rx, 6 send etc)
 *	0x0F: interrupt status
 *		0: interrupt pending
 *		1: A0 priority		}
 *		2: A1 priority		}	if 0 set 1-3 is the source
 *		3: A2 priority		}
 *		4: printer busy
 *		5: framing error	}	HCC
 *		6: overrun		}
 *		7: unused
 *
 *	High speed comms port (TR1863 + ctrl)	@0x80 (R/W)
 *	(fixed at 111K 8N1 RS422 to talk to the HCCA)
 *	Keyboard (serial 8251)			@0x90(data)/0x91(status)
 *	(6992 bauyd, RS422 +10v power supply!)
 *	TMS9918A (video)			@0xA0 (data) @0xA1 (ctrl)
 *	Printer port				@0xB0
 *	Four slots at C0, D0, E0 and F0		@C0-FF
 *
 *	Optional CF add on			@0xC0
 *
 *	Interrupts are vectored IM2
 *	priority high->low 0-7 is
 *	0: HCCA receive
 *	1: HCCA send
 *	2: Keyboard
 *	3: TMS9918A
 *	4-7: Option card 0-3
 *
 *	Keyboard interface
 *	80 xx		joystick 1	(101FURDL)
 *	81 xx		joystick 2	(101FURDL)
 *	90		multiple keys
 *	91		faulty RAM
 *	92		faulty ROM
 *	93		illegal ISR
 *	94		watdhdog
 *	95		power up
 *
 *	FDC appears in a slot where  xF is 0x10
 *	Registers relative to slot base are
 *
 *	x0	W	Command
 *	x0	R	Status ? (bit 1 is used and bit 7)
 *	x1	R/W	Track ?
 *	x2	R/W	Sector
 *	x3	R	Data
 *	xF	W	some kind of motor control ?
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
#include <sys/socket.h>
#include <netdb.h>

#include "system.h"
#include "libz80/z80.h"
#include "lib765/include/765.h"

#include "ide.h"
#include "tms9918a.h"
#include "tms9918a_render.h"
#include "z80dis.h"

static uint8_t ram[65536];
static uint8_t rom[8192];

static uint8_t ctrlport;

static uint8_t fast = 0;
static uint8_t int_recalc = 0;

static struct tms9918a *vdp;
static struct tms9918a_renderer *vdprend;

static uint16_t tstate_steps = 365/2;	/* Should be 3.58MHz */

static struct wd17xx *wdfdc;

static int hcci_fd = -1;		/* HSS interface socket */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;
#define HCC_RXINT	0x80
#define HCC_TXINT	0x40
#define KBD_INT		0x20
#define VDP_INT		0x10

static Z80Context cpu_z80;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_ROM	0x000004
#define TRACE_UNK	0x000008
#define TRACE_CPU	0x000010
#define TRACE_KBD	0x000020
#define TRACE_IRQ	0x000040
#define TRACE_TMS9918A  0x000080
#define TRACE_IDE	0x000100

static int trace = 0;

static void reti_event(void);
static void poll_irq_event(void);

uint8_t do_mem_read(uint16_t addr, int quiet)
{
	if (!(ctrlport & 1) && addr < 8192) {
		if (!quiet && (trace & TRACE_MEM))
			fprintf(stderr, "R %04x = %02X (ROM)\n", (unsigned int)addr, rom[addr]);
		return rom[addr];
	}
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X = %02X\n", addr, rom[addr]);
	return ram[addr];
}

void mem_write(int unused, uint16_t addr, uint8_t val)
{
	/* Writes always go to RAM even if the ROM is live. The standard
	   NABU ROM uses this property */
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04x = %02X\n", (unsigned int) addr, val);
	ram[addr] = val;
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
	unsigned int n = 2;

	FD_ZERO(&i);
	FD_SET(0, &i);
	FD_ZERO(&o);
	FD_SET(1, &o);

	if (hcci_fd != -1) {
		FD_SET(hcci_fd, &i);
		FD_SET(hcci_fd, &o);
		n = hcci_fd + 1;
		if (n < 2)
			n = 2;
	}
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(n, &i, &o, NULL, &tv) == -1) {
		if (errno == EINTR)
			return 0;
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	if (FD_ISSET(hcci_fd, &i))
		r |= 4;
	if (FD_ISSET(hcci_fd, &o))
		r |= 8;
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

/* Keyboard - seems to be an 8251 serial keyboard */

/* Send init sequence (not clear how init works yet) */
static unsigned kbd_wdog = 1;
static uint8_t kbd_last = 0xFF;	/* Code is never used */

void nabupc_queue_key(uint8_t c)
{
	kbd_last = c;
}

/* Need to emulate set up etc eventually bur for now.. pfft */
static void kbd_out(uint16_t addr, uint8_t val)
{
}

/* Bit 1 of the status is set if a key is presetn */
static uint8_t kbd_in(uint16_t addr)
{
	uint8_t k;
	if (addr == 0x91) {
		if (kbd_wdog || kbd_last != 0xFF)
			return 2;
		return 0;
	}
	if (kbd_wdog) {
		kbd_wdog = 0;
		if (trace & TRACE_KBD)
			fprintf(stderr, "kbd_in: [first] 0x94\n");
		return 0x94;	/* watchdog */
	}
	/* Data */
	k = kbd_last;
	kbd_last = 0xFF;
	if (trace & TRACE_KBD)
		fprintf(stderr, "kbd_in: D: %02X\n", kbd_last);
	return k;
}

static uint8_t sg_addr;
static uint8_t sg_data[16];

static unsigned priority_encoder(void)
{
	unsigned i;
	uint8_t r = live_irq & sg_data[14];
	for (i = 0; i <= 7; i++) {
		if (r & 0x80) {
			if (trace & TRACE_IRQ)
				fprintf(stderr, "prio irq %d\n", i);
			return i;
		}
		r <<= 1;
	}
	return 0;
}

static void sound_out(uint16_t addr, uint8_t val)
{
	if (addr & 1)
		sg_addr = val;
	else if (sg_addr < 16)
		sg_data[sg_addr] = val;
	if (sg_addr == 14)
		poll_irq_event();
}

static uint8_t sound_in(uint16_t addr)
{
	/* Interrupt decode is on the input port. Bit 0 is irq indicator
	   1-3 are the output of a priority encoder, and 4-6 are misc
	   things we don't bother with (printer busy etc) */
	if (sg_addr == 15) {
		poll_irq_event();
		if (live_irq && (trace & TRACE_IRQ))
			fprintf(stderr, "irq: live %02X mask %02X enc %d\n", live_irq,
				sg_data[14], priority_encoder());
		if (live_irq)
			return 1 | (priority_encoder() << 1);
		return 0;
	}
	if (sg_addr < 16)
		return sg_data[sg_addr];
	return 0xFF;
}

static uint8_t hcci_read(uint16_t addr)
{
	static uint8_t c;
	if (hcci_fd != -1)
		read(hcci_fd, &c, 1);
	return c;
}

static void hcci_write(uint16_t addr, uint8_t val)
{
	if (hcci_fd != -1)
		write(hcci_fd, &val, 1);
}

/*
 *	A very primitive WD17xx simulation
 */

struct wd17xx {
	int fd[4];
	int tracks[4];
	int spt[4];
	int sides[4];
	int secsize[4];
	int drive;
	uint8_t buf[1024];
	int pos;
	int wr;
	int rd;
	uint8_t track;
	uint8_t trackpos;
	uint8_t sector;
	uint8_t status;
	uint8_t side;
	unsigned stepdir;
	unsigned type1;	/* Last command was type 1: index pulses apply */
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
	off_t pos = fdc->trackpos * fdc->spt[fdc->drive];

	pos += fdc->sector - 1;

	if (fdc->sides[fdc->drive] == 2 && fdc->side)
		pos += fdc->spt[fdc->drive] / 2;
	pos *= fdc->secsize[fdc->drive];
	if (lseek(fdc->fd[fdc->drive], pos, SEEK_SET) < 0) {
		perror("lseek");
		exit(1);
	}
	fdc->trackpos = fdc->track;
}

uint8_t wd_read_data(struct wd17xx *fdc)
{
	unsigned ss = fdc->secsize[fdc->drive];
	if (fdc->rd == 0)
		return fdc->buf[0];
	if (fdc->pos >= ss)
		return fdc->buf[ss - 1];
	if (fdc->pos == ss - 1) {
		fdc->status &= ~(BUSY | DRQ);
		fdc->rd = 0;
	}
	return fdc->buf[fdc->pos++];
}

void wd_write_data(struct wd17xx *fdc, uint8_t v)
{
	unsigned ss = fdc->secsize[fdc->drive];
	if (fdc->wr == 0) {
		fdc->buf[0] = v;
		return;
	}
	if (fdc->pos >= ss)
		return;
	fdc->buf[fdc->pos++] = v;
	if (fdc->pos == ss) {
		wd_diskseek(fdc);
		if (write(fdc->fd[fdc->drive], fdc->buf, ss) != ss)
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

static void wd_step(struct wd17xx *fdc, int direction, unsigned t)
{
	if (direction == 0)
		return;
	fdc->stepdir = direction;
	if (direction == -1) {
		if (fdc->track)
			fdc->track--;
		if (fdc->trackpos)
			fdc->trackpos--;
		return;
	}
	if (fdc->track < 255)
		fdc->track++;
	if (fdc->trackpos == fdc->tracks[fdc->drive] - 1)
		return;
	fdc->trackpos ++;
}

void wd_command(struct wd17xx *fdc, uint8_t v)
{
	unsigned ss = fdc->secsize[fdc->drive];
	if (fdc->fd[fdc->drive] == -1) {
		fdc->status = NOTREADY;
		return;
	}
	fprintf(stderr, "fdc cmd: %02X\n", v);
	fdc->type1 = 0;
	switch (v & 0xF0) {
	case 0x00:	/* Restore */
		while (fdc->trackpos)
			wd_step(fdc, -1, 0);
		fdc->track = 0;
		fdc->status &= ~(BUSY | DRQ);
		fdc->type1 = 1;
		break;
	case 0x10:	/* Seek */
		while (fdc->track < fdc->buf[0])
			wd_step(fdc, 1, 1);
		while (fdc->track > fdc->buf[0])
			wd_step(fdc, -1, 1);
		fdc->status &= ~(BUSY | DRQ);
		fdc->type1 = 1;
		break;
	case 0x20:
	case 0x30:
		/* Step in the current direction */
		wd_step(fdc, fdc->stepdir, v & 0x10);
		fdc->status &= ~(BUSY | DRQ);
		fdc->type1 = 1;
		break;
	case 0x40:
	case 0x50:
		/* Step in */
		wd_step(fdc, 1, v & 0x10);
		fdc->status &= ~(BUSY | DRQ);
		fdc->type1 = 1;
		break;
	case 0x60:
	case 0x70:
		/* Step out */
		wd_step(fdc, -1, v & 0x10);
		fdc->status &= ~(BUSY | DRQ);
		fdc->type1 = 1;
		break;
	case 0x80:
		/* Read */
	case 0x90:
		/* Read multiple */
		wd_diskseek(fdc);
		fdc->rd = 1;
		if (read(fdc->fd[fdc->drive], fdc->buf, ss) != ss)
			fprintf(stderr, "wd: I/O error.\n");
		fdc->status |= BUSY | DRQ;
		fdc->pos = 0;
		break;
	case 0xA0:
		/* Write */
	case 0xB0:
		/* Write multiple */
		fdc->status |= BUSY | DRQ;
		fdc->pos = 0;
		fdc->wr = 1;
		break;
	case 0xC0:
		/* Read address */
	case 0xD0:
		/* Force interrupt */
		if (fdc->status & BUSY) {
			fdc->status &= ~BUSY;
			break;
		}
		fdc->status &= ~(BUSY | DRQ);
		fdc->type1 = 1;
		break;
	case 0xE0:
		/* Read track */
	case 0xF0:
		/* Write track */
		break;
	}
}

uint8_t wd_status(struct wd17xx *fdc)
{
	return fdc->status;
}

/* Hack for now */
void wd_timer(struct wd17xx *fdc)
{
	if (fdc->fd[fdc->drive] == -1)
		return;
	if (fdc->type1 == 1)
		fdc->status ^= DRQ;	 /* Index pulse */
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


int wd_attach(struct wd17xx *fdc, int dev, const char *path)
{
	if (fdc->fd[dev])
		close(fdc->fd[dev]);
	fdc->fd[dev] = open(path, O_RDWR);
	if (fdc->fd[dev] == -1)
		perror(path);
	/* Assume the standard Nabu format - which is weird */
	fdc->spt[dev] = 5;
	fdc->tracks[dev] = 40;
	fdc->sides[dev] = 1;
	fdc->secsize[dev] = 1024;
	return fdc->fd[dev];
}

static uint8_t fdc_read(uint16_t addr)
{
	addr &= 0x0F;
	switch(addr) {
	case 0:
		return wd_status(wdfdc);
	case 1:
		return wd_read_track(wdfdc);
	case 2:
		return wd_read_sector(wdfdc);
	case 3:
		return wd_read_data(wdfdc);
	case 0x0F:
		return 0x10;
	default:
		fprintf(stderr, "fdc read %x unknown.\n", addr);
	}
	return 0xFF;
}

static void fdc_write(uint16_t addr, uint8_t data)
{
	addr &= 0x0F;
	switch(addr) {
	case 0:
		wd_command(wdfdc, data);
		break;
	case 1:
		wd_write_track(wdfdc, data);
		break;
	case 2:
		wd_write_sector(wdfdc, data);
		break;
	case 3:
		wd_write_data(wdfdc, data);
		break;
	case 0x0F:	/* bit 1 motor ? */
		break;
	default:
		fprintf(stderr, "fdc write %x <- %02x unknown\n", addr, data);
	}
}

uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);

	addr &= 0xFF;

	if (addr == 0xCF)
		return 0x10;
	if ((addr >= 0xC0 && addr <= 0xC7) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0xC0 && addr <= 0xCF)
		return fdc_read(addr);
	if (addr == 0xA0 || addr == 0xA1)
		return tms9918a_read(vdp, addr & 1);
	if (addr == 0x90||addr == 0x91)
		return kbd_in(addr);
	if (addr >= 0x80 && addr <= 0x8F)
		return hcci_read(addr);
	/* FIXME: sound hack */
	if (addr == 0x40 || addr == 0x41)
		return sound_in(addr);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0x78;
}

void io_write(int unknown, uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);

	addr &= 0xFF;
	if (addr == 0)
		ctrlport = val;
	else if ((addr >= 0xC0 && addr <= 0xC7) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0xC0 && addr <= 0xCF)
		fdc_write(addr, val);
	else if (addr == 0xA0 || addr == 0xA1)
		tms9918a_write(vdp, addr & 1, val);
	else if (addr == 0x90 || addr == 0x91)
		kbd_out(addr, val);
	else if (addr >= 0x80 && addr <= 0x8F)
		hcci_write(addr, val);
	else if (addr == 0x40 || addr == 0x41)
		sound_out(addr, val);
	else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

static void poll_irq_event(void)
{
	static unsigned old_irq;
	unsigned n = check_chario();
	old_irq = live_irq;
	live_irq = 0;
	if (tms9918a_irq_pending(vdp))
		live_irq |= VDP_INT;
	if (kbd_wdog || (n & 1))
		live_irq |= KBD_INT;
	if (n & 4)	/* HCCI in */
		live_irq |= HCC_RXINT;
	if (n & 8)	/* HCCI out */
		live_irq |= HCC_TXINT;
	live_irq &= sg_data[14];	/* Interrupt mask */
	if (live_irq != old_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "INT %02X\n", live_irq);
	if (live_irq)
		Z80INT(&cpu_z80, 0xFF);
}

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
	live_irq = 0;
	poll_irq_event();
}

static void hcci_connect(const char *p)
{
	struct addrinfo *res;
	static struct addrinfo hints;
	int err;
	char *x = strrchr(p, ':');
	if (x == NULL)
		x = ":9995";
	else {
		*x++ = 0;
	}

	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	err = getaddrinfo(p, x, &hints, &res);
	if (err) {
		fprintf(stderr, "nabupc: unable to look up %s: %s.\n", p,
			gai_strerror(err));
		exit(1);
	}
	while(res) {
		hcci_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (hcci_fd != -1) {
			if (connect(hcci_fd, res->ai_addr, res->ai_addrlen) != -1) {
				fcntl(hcci_fd, F_SETFL, FNDELAY);
				return;
			}
			perror("connect");
			close(hcci_fd);
		}
		res = res->ai_next;
	}
	hcci_fd = -1;
	fprintf(stderr, "nabupc: unable to connect to '%s'.\n", p);
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
	fprintf(stderr, "nabupc: [-f] [-h server] [-A floppy] [-i idepath] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "nabupc.rom";
	char *idepath = NULL;
	char *hccipath = NULL;
	char *drive_a = NULL;
	uint8_t *p = ram;
	unsigned kdog = 0;

	while (p < ram + sizeof(ram))
		*p++= rand();

	while ((opt = getopt(argc, argv, "d:fh:i:r:A:")) != -1) {
		switch (opt) {
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'h':
			hccipath = optarg;
			break;
		case 'i':
			ide = 1;
			idepath = optarg;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 'A':
			drive_a = optarg;
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
	if (read(fd, rom, 8192) != 8192) {
		fprintf(stderr, "nabupc: short rom '%s'.\n", rompath);
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

	wdfdc = wd_init();
	if (drive_a)
		wd_attach(wdfdc, 0, drive_a);

	if (hccipath)
		hcci_connect(hccipath);

	ui_init();

	vdp = tms9918a_create();
	tms9918a_trace(vdp, !!(trace & TRACE_TMS9918A));
	vdprend = tms9918a_renderer_create(vdp);

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
		int i;
		/* 36400 T states for base rcbus - varies for others */
		for (i = 0; i < 50; i++) {
			int j;
			for (j = 0; j < 100; j++) {
				Z80ExecuteTStates(&cpu_z80, (tstate_steps + 5)/ 10);
			}
			/* We want to run UI events regularly it seems */
			ui_event();
		}

		/* 50Hz which is near enough */
		if (vdp) {
			tms9918a_rasterize(vdp);
			tms9918a_render(vdprend);
		}
		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		wd_timer(wdfdc);
		kdog++;
		if (kdog == 50) {
			kbd_wdog = 1;
			kdog = 0;
		}
		recalc_interrupts();
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
	return 0;
}
