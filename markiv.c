/*
 *	Platform features
 *
 *	Z180 at 18.432MHz
 *	512K/512K flat memory card
 *	IDE CF 8bit on 0x40-47
 *	SD on CSIO control on 0x49
 *	RTC at 0x4A
 *
 *	TODO: RTC needs adjusting to correct bits
 *
 *	Add support for using real CF card
 *	Add support for PProp
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
#include "z180_io.h"

#include "ide.h"
#include "rtc_bitbang.h"
#include "sdcard.h"
#include "z80dis.h"

static uint8_t ramrom[1024 * 1024];	/* Low 512K is ROM */

static uint8_t fast = 0;
static uint8_t int_recalc = 0;
static struct sdcard *sdcard;
static struct z180_io *io;

static uint16_t tstate_steps = 737;	/* 18.432MHz */

/* IRQ source that is live in IM2 */
static uint8_t live_irq;

static Z180Context cpu_z180;

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_UNK	0x000004
#define TRACE_RTC	0x000008
#define TRACE_CPU	0x000010
#define TRACE_CPU_IO	0x000020
#define TRACE_IRQ	0x000040
#define TRACE_SD	0x000080
#define TRACE_IDE	0x000100
#define TRACE_SPI	0x000200
#define TRACE_PROP	0x000400

static int trace = 0;

static void reti_event(void);

static uint8_t do_mem_read(uint16_t addr, int quiet)
{
	uint32_t pa = z180_mmu_translate(io, addr);
	if (!quiet && (trace & TRACE_MEM))
		fprintf(stderr, "R %04X[%06X] -> %02X\n", addr, pa, ramrom[pa]);
	return ramrom[pa];
}

void mem_write(int unused, uint16_t addr, uint8_t val)
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

uint8_t z180_phys_read(int unused, uint32_t addr)
{
	return ramrom[addr & 0xFFFFF];
}

void z180_phys_write(int unused, uint32_t addr, uint8_t val)
{
	addr &= 0xFFFFF;
	if (addr >= 0x80000)
		ramrom[addr] = val;
}

uint8_t mem_read(int unused, uint16_t addr)
{
	static uint8_t rstate = 0;
	uint8_t r = do_mem_read(addr, 0);

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

static void markiv_trace(unsigned unused)
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

/* PropIO v2 */

/* TODO:
    - The rx/tx pointers don't appear to be separate nor the data buffer
      so use one buffer and pointer set. Copy out the LBA on PREP
    - Stat isn't used but how should it work
    - What should be in the CSD ?
    - Four byte error blocks
    - Command byte for console does what ?
    - Status command does what ?
*/
static uint8_t prop_csd[16];	/* What goes here ??? */
static uint8_t prop_rbuf[4];
static uint8_t prop_sbuf[512];
static uint8_t *prop_rptr;
static uint16_t prop_rlen;
static uint8_t *prop_tptr;
static uint16_t prop_tlen;
static uint8_t prop_st;
static uint8_t prop_err;
static int prop_fd;
static off_t prop_cardsize;

static uint32_t buftou32(void)
{
    uint32_t x;
    x = prop_rbuf[0];
    x |= ((uint8_t)prop_rbuf[1]) << 8;
    x |= ((uint8_t)prop_rbuf[2]) << 16;
    x |= ((uint8_t)prop_rbuf[3]) << 24;
    return x;
}

static void u32tobuf(uint32_t t)
{
    prop_rbuf[0] = t;
    prop_rbuf[1] = t >> 8;
    prop_rbuf[2] = t >> 16;
    prop_rbuf[3] = t >> 24;
    prop_st = 0;
    prop_err = 0;
    prop_rptr = prop_rbuf;
    prop_rlen = 4;
}

static void prop_init(void)
{
    char *sdcard_path = "ibble";
    prop_rptr = prop_rbuf;
    prop_tptr = prop_rbuf;
    prop_rlen = 4;
    prop_tlen = 4;
    prop_fd = open(sdcard_path, O_RDWR);
    if (prop_fd == -1) {
        perror(sdcard_path);
        return;
    }
    if ((prop_cardsize = lseek(prop_fd, 0L, SEEK_END)) == -1) {
        perror(sdcard_path);
        close(prop_fd);
        prop_fd = -1;
    }
}

static uint8_t prop_read(uint8_t addr)
{
//    uint8_t r, v;
    switch(addr) {
    case 0:		/* Console status */
#if 0
        v = check_chario();
        r = (v & 1) ? 0x20:0x00;
        if (v & 2)
            r |= 0x10;
        return r;
#else
	return 0;
#endif
    case 1:		/* Keyboard input */
    	return 0xFF;
#if 0    	
        return next_char();
#endif
    case 2:		/* Disk status */
        return prop_st;
    case 3:		/* Data transfer */
        if (prop_rlen) {
            if (trace & TRACE_PROP)
                fprintf(stderr, "prop: read byte %02X\n", *prop_rptr);
            prop_rlen--;
            return *prop_rptr++;
        }
        else {
            if (trace & TRACE_PROP)
                fprintf(stderr, "prop: read byte - empty.\n");
            return 0xFF; 
        }
    }
    return 0xFF;
}

static void prop_write(uint8_t addr, uint8_t val)
{
    off_t lba;

    switch(addr) {
        case 0:
            /* command port */
            break;
        case 1:
            /* write to screen */
            putchar(val);
            fflush(stdout);
            break;
        case 2:
            if (trace & TRACE_PROP)
                fprintf(stderr, "Command %02X\n", val);
            /* commands */
            switch(val) {
            case 0x00:	/* NOP */
                prop_err = 0;
                prop_st = 0;
                prop_tptr = prop_rbuf;
                prop_rptr = prop_rbuf;
                prop_tlen = 4;
                prop_rlen = 4;
                break;
            case 0x01:	/* STAT */
                prop_rptr = prop_rbuf;
                prop_rlen = 1;
                *prop_rbuf = prop_err;
                prop_err = 0;
                prop_st = 0;
                break;
            case 0x02:	/* TYPE */
                prop_err = 0;
                prop_rptr = prop_rbuf;
                prop_rlen = 1;
                prop_st = 0;
                if (prop_fd == -1) {
                    *prop_rbuf = 0;
                    prop_err = -9;
                    prop_st |= 0x40;
                } else
                    *prop_rbuf = 1;		/* MMC */
                break;
            case 0x03:	/* CAP */
                prop_err = 0;
                u32tobuf(prop_cardsize >> 9);
                break;
            case 0x04:	/* CSD */
                prop_err = 0;
                prop_rptr = prop_csd;
                prop_rlen = sizeof(prop_csd);
                prop_st = 0;
                break;
            case 0x10:	/* RESET */
                prop_st = 0;
                prop_err = 0;
                prop_tptr = prop_rbuf;
                prop_rptr = prop_rbuf;
                prop_tlen = 4;
                prop_rlen = 4;
                break;
            case 0x20:	/* INIT */
                if (prop_fd == -1) {
                    prop_st = 0x40;
                    prop_err = -9;
                    /* Error packet */
                } else {
                    prop_st = 0;
                    prop_err = 0;
                }
                break;
            case 0x30:	/* READ */
                lba = buftou32();
                lba <<= 9;
                prop_err = 0;
                prop_st = 0;
                if (lseek(prop_fd, lba, SEEK_SET) < 0 ||
                    read(prop_fd, prop_sbuf, 512) != 512) {
                    prop_err = -6;
                    /* Do error packet FIXME */
                } else {
                    prop_rptr = prop_sbuf;
                    prop_rlen = sizeof(prop_sbuf);
                }
                prop_tptr = prop_rbuf;
                prop_tlen = 4;
                break;
                break;
            case 0x40:	/* PREP */
                prop_st = 0;
                prop_err = 0;
                prop_tptr = prop_sbuf;
                prop_tlen = sizeof(prop_sbuf);
                prop_rlen = 0;
                break;
            case 0x50:	/* WRITE */
                lba = buftou32();
                lba <<= 9;
                prop_err = 0;
                prop_st = 0;
                if (lseek(prop_fd, lba, SEEK_SET) < 0 ||
                    write(prop_fd, prop_sbuf, 512) != 512) {
                    prop_err = -6;
                    /* FIXME: do error packet */
                }
                prop_tptr = prop_rbuf;
                prop_tlen = 4;
                break;
            case 0xF0:	/* VER */
                prop_rptr = prop_rbuf;
                prop_rlen = 4;
                /* Whatever.. use a version that's obvious fake */
                memcpy(prop_rbuf,"\x9F\x00\x0E\x03", 4);
                break;
            default:
                if (trace & TRACE_PROP)
                    fprintf(stderr, "Prop: unknown command %02X\n", val);
                break;
            }
            break;
        case 3:
            /* data write */
            if (prop_tlen) {
                if (trace & TRACE_PROP)
                    fprintf(stderr, "prop: queue byte %02X\n", val);
                *prop_tptr++ = val;
                prop_tlen--;
            } else if (trace & TRACE_PROP)
                fprintf(stderr, "prop: write byte over.\n");
            break;
    }
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
		fprintf(stderr,	"[SPI %02X:%02X]\n", bitrev[bits], bitrev[r]);
	return r;
}

/*
 *	The SD is on bits 2/4/5/6/7
 *	2: CS
 *	4: WP detect
 *	5: Card detect
 *	6: Int enable
 *	7: Int pending
 */
static void sd_write(uint8_t val)
{
	static uint8_t sysio = 0xFF;
	uint8_t delta = val ^ sysio;
	if (sdcard && (delta & (1 << 2))) {
		if (trace & TRACE_SPI)
			fprintf(stderr, "[SPI CS %sed]\n",
				(val & (1 << 2)) ? "lower" : "rais");
		if (val & (1 << 2))
			sd_spi_lower_cs(sdcard);
		else
			sd_spi_raise_cs(sdcard);
	}
	sysio = val;
}

static uint8_t sd_read(void)
{
	return 0x20;
}

static uint8_t xar_read(void)
{
	/* TODO */
	return 0xFF;
}

/* Properly the XAR also provides bits for extended ROM banks, notably it
   provides A19 (bit 0) to the 1MB ROM option */

static void xar_write(uint8_t v)
{
	static uint8_t xar = 0;
	uint8_t delta = xar ^ v;
	xar = v;

	if (delta & xar & 0x80)
		ide_reset_begin(ide0);
}

uint8_t io_read(int unused, uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if (z180_iospace(io, addr))
		return z180_read(io, addr);

	addr &= 0xFF;
	if ((addr >= 0x80 && addr <= 0x87) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr == 0x88)
		return xar_read();
	if (addr == 0x89)
		return sd_read();
	if (addr == 0x8A)
		return rtc_read(rtc);
	if (addr >= 0xA8 && addr <= 0xAB)
		return prop_read(addr & 3);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void io_write(int unused, uint16_t addr, uint8_t val)
{
	unsigned int known = 0;

	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);

	if (z180_iospace(io, addr)) {
		z180_write(io, addr, val);
		known = 1;
	}
	addr &= 0xFF;
	if ((addr >= 0x80 && addr <= 0x87) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr == 0x88)
		xar_write(val);
	else if (addr == 0x89)
		sd_write(val);
	else if (addr == 0x8A)
		rtc_write(rtc, val);
	else if (addr >= 0xA8 && addr <= 0xAB)
		prop_write(addr & 3, val);
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

static void reti_event(void)
{
	if (live_irq && (trace & TRACE_IRQ))
		fprintf(stderr, "RETI\n");
	live_irq = 0;
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
	fprintf(stderr, "markiv: [-f] [-i idepath] [-r rompath] [-S sdpath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "markiv.rom";
	char *sdpath = NULL;
	char *idepath = NULL;

	uint8_t *p = ramrom;
	while (p < ramrom + sizeof(ramrom))
		*p++= rand();

	while ((opt = getopt(argc, argv, "r:S:i:d:f")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'S':
			sdpath = optarg;
			break;
		case 'i':
			idepath = optarg;
			ide = 1;
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

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ramrom, 524288) != 524288) {
		fprintf(stderr, "markiv: banked rom image should be 512K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide) {
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

	prop_init();

	io = z180_create(&cpu_z180);
	z180_set_input(io, 0, 1);
	z180_trace(io, trace & TRACE_CPU_IO);

	rtc = rtc_create();
	rtc_trace(rtc, trace & TRACE_RTC);

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

	Z180RESET(&cpu_z180);
	cpu_z180.ioRead = io_read;
	cpu_z180.ioWrite = io_write;
	cpu_z180.memRead = mem_read;
	cpu_z180.memWrite = mem_write;
	cpu_z180.trace = markiv_trace;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		int states = 0;
		unsigned int i, j;
		/* We have to run the DMA engine and Z180 in step per
		   instruction otherwise we will mess up on stalling DMA */

		/* Do an emulated 20ms of work (368640 clocks) */
		for (i = 0; i < 50; i++) {
			for (j = 0; j < 10; j++) {
				while (states < tstate_steps) {
					unsigned int used;
					used = z180_dma(io);
					if (used == 0)
						used = Z180Execute(&cpu_z180);
					states += used;
				}
				z180_event(io, states);
				states -= tstate_steps;
			}
		}

		/* Do 20ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		if (int_recalc) {
			/* If there is no pending Z180 vector IRQ but we think
			   there now might be one we use the same logic as for
			   reti */
//			if (!live_irq)
//				poll_irq_event();
			/* Clear this after because reti_event may set the
			   flags to indicate there is more happening. We will
			   pick up the next state changes on the reti if so */
			if (!(cpu_z180.IFF1|cpu_z180.IFF2))
				int_recalc = 0;
		}
	}
	exit(0);
}
