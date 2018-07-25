/*
 *	Platform features
 *
 *	Z80 at 7.372MHz
 *	Zilog SIO/2 at 0x80-0x83
 *	Motorola 6850 repeats all over 0x40-0x7F (not recommended)
 *	IDE at 0x10-0x17 no high or control access
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	First 512K ROM Second 512K RAM (0-31, 32-63)
 *	RTC at 0xC0
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
#include "libz80/z80.h"
#include "ide.h"

static uint8_t ramrom[1024 * 1024];	/* Covers the banked card */

static unsigned int bankreg[4];
static uint8_t bankenable;

static uint8_t bank512 = 0;
static uint8_t switchrom = 1;
static uint8_t cpuboard = 0;
static uint8_t ctc = 0;
static uint8_t port38 = 0;

static Z80Context cpu_z80;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_SIO	16
static int trace;

/* FIXME: emulate paging off correctly, also be nice to emulate with less
   memory fitted */
static uint8_t mem_read0(uint16_t addr)
{
    if (bankenable) {
        unsigned int bank = (addr & 0xC000) >> 14;
        if (trace & TRACE_MEM)
            fprintf(stderr, "R %04x[%02X] = %02X\n",
                addr, (unsigned int) bankreg[bank],
                (unsigned int)ramrom[(bankreg[bank] << 14) + (addr&0x3FFF)]);
        addr &= 0x3FFF;
        return ramrom[(bankreg[bank] << 14) + addr];
    }
    if (trace & TRACE_MEM)
        fprintf(stderr, "R %04X = %02X\n", addr, ramrom[addr]);
    return ramrom[addr];
}

static void mem_write0(uint16_t addr, uint8_t val)
{
    if (bankenable) {
        unsigned int bank = (addr & 0xC000) >> 14;
        if (trace & TRACE_MEM)
	    fprintf(stderr, "W %04x[%02X] = %02X\n",
	        (unsigned int) addr, (unsigned int) bankreg[bank],
	        (unsigned int)val);
        if (bankreg[bank] >= 32) {
            addr &= 0x3FFF;
            ramrom[(bankreg[bank] << 14) + addr] = val;
        }
    /* ROM writes go nowhere */
        else if (trace & TRACE_MEM)
            fprintf(stderr,"[Discarded: ROM]\n");
    } else {
        if (trace & TRACE_MEM)
            fprintf(stderr, "W: %04X = %02X\n", addr, val);
        if (addr > 32768 && !bank512)
            ramrom[addr] = val;
        else if (trace & TRACE_MEM)
                fprintf(stderr, "[Discarded: ROM]\n");
    }
}

static uint8_t mem_read108(uint16_t addr)
{
    uint32_t aphys;
    if (addr < 0x8000 && !(port38 & 0x01))
        aphys = addr;
    else if (port38 & 0x80)
        aphys = addr + 131072;
    else
        aphys = addr + 65536;
    if (trace & TRACE_MEM)
        fprintf(stderr, "R %04X = %02X\n", addr, ramrom[aphys]);
    return ramrom[aphys];
}

static void mem_write108(uint16_t addr, uint8_t val)
{
    uint32_t aphys;
    if (trace & TRACE_MEM)
        fprintf(stderr, "W: %04X = %02X\n", addr, val);
    if (addr < 0x8000 && !(port38 & 0x01)) {
        if (trace & TRACE_MEM)
            fprintf(stderr,"[Discarded: ROM]\n");
        return;
    }
    else if (port38 & 0x80)
        aphys = addr + 131072;
    else
        aphys = addr + 65536;
    ramrom[aphys] = val;
}

static uint8_t mem_read(int unused, uint16_t addr)
{
    switch(cpuboard) {
    case 0:
        return mem_read0(addr);
    case 1:
        return mem_read108(addr);
    default:
        fputs("invalid cpu type.\n", stderr);
        exit(1);
    }
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
    switch(cpuboard) {
    case 0:
        mem_write0(addr, val);
        break;
    case 1:
        mem_write108(addr, val);
        break;
    default:
        fputs("invalid cpu type.\n", stderr);
        exit(1);
    }
}

static int check_chario(void)
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
	perror("select");
	exit(1);
    }
    if (FD_ISSET(0, &i))
	r |= 1;
    if (FD_ISSET(1, &o))
	r |= 2;
    return r;
}

static unsigned int next_char(void)
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

static uint8_t acia_status = 2;
static uint8_t acia_config;
static uint8_t acia_char;
static uint8_t acia;

static void acia_irq_compute(void)
{
    if (acia_config & acia_status & 0x80)
        Z80INT(&cpu_z80, 0xFF);	/* FIXME probably last data or bus noise */
}

static void acia_receive(void)
{
    uint8_t old_status = acia_status;
    acia_status = old_status & 0x02;
    if (old_status & 1)
        acia_status |= 0x20;
    acia_char = next_char();
    acia_status |= 0x81;	/* IRQ, and rx data full */
}
        
static void acia_transmit(void)
{
    if (!(acia_status & 2))
        acia_status |= 0x82;	/* IRQ, and tx data empty */
}
        
static void acia_timer(void)
{
    int s = check_chario();
    if (s & 1)
        acia_receive();
    if (s & 2)
        acia_transmit();
    if (s)
        acia_irq_compute();
}
    
/* Very crude for initial testing ! */
static uint8_t acia_read(uint8_t addr)
{
    switch (addr) {
    case 0:
	/* bits 7: irq pending, 6 parity error, 5 rx over
	 * 4 framing error, 3 cts, 2 dcd, 1 tx empty, 0 rx full.
	 * Bits are set on char arrival and cleared on next not by
	 * user
	 */
	return acia_status;
    case 1:
        acia_status &= ~0x81;	/* No IRQ, rx empty */
	return acia_char;
    }
}

static void acia_write(uint16_t addr, uint8_t val)
{
    switch (addr) {
    case 0:
	/* bit 7 enables interrupts, buits 5-6 are tx control
	   bits 2-4 select the world size and 0-1 counter divider
	   except 11 in them means reset */
	acia_config = val;
	if ((acia_config & 3) == 3)
	    acia_status = 2;
        acia_irq_compute();
	return;
     case 1:
	write(1, &val, 1);
	/* Clear any existing int state and tx empty */
	acia_status &= ~0x82;
	break;
    }
}

struct z80_sio_chan {
    uint8_t wr[8];
    uint8_t rr[3];
    uint8_t data[3];
    uint8_t dptr;
    uint8_t irq;
    uint8_t rxint;
    uint8_t txint;
    uint8_t intbits;
#define INT_TX	1
#define INT_RX	2
#define INT_ERR	4
};

static int sio2;
static struct z80_sio_chan sio[2];

/*
 *	Interrupts. We don't handle IM2 yet.
 */

static void sio2_clear_int(struct z80_sio_chan *chan, uint8_t m)
{
    chan->intbits &= ~m;
    /* Check me - does it auto clear down or do you have to reti it ? */
    if (!(sio->intbits | sio[1].intbits)) {
        sio->rr[1] &= ~0x02;
        sio->irq = 0;
    }
}

static void sio2_raise_int(struct z80_sio_chan *chan, uint8_t m)
{
    uint8_t new = (chan->intbits ^ m) & m;
    chan->intbits |= m;
    if (new) {
        if (!sio->irq) {
            sio->irq = 1;
            sio->rr[1] |= 0x02;
            Z80INT(&cpu_z80, 0xFF);	/* FIXME: for IM2 this is complex */
        }
    }
}

/*
 *	The SIO replaces the last character in the FIFO on an
 *	overrun.
 */
static void sio2_queue(struct z80_sio_chan *chan, uint8_t c)
{
    /* Receive disabled */
    if (!(chan->wr[3] & 1))
        return;
    /* Overrun */
    if (chan->dptr == 2) {
        chan->data[2] = c;
        chan->rr[1] |= 0x20;	/* Overrun flagged */
        sio2_raise_int(chan, INT_ERR);
    } else {
        /* FIFO add */
        chan->data[chan->dptr++] = c;
        chan->rr[0] |= 1;
        if (chan->dptr == 1)
            sio2_raise_int(chan, INT_RX);
    }
    /* Need to deal with interrupt results */
}

static void sio2_channel_timer(struct z80_sio_chan *chan, uint8_t ab)
{
    if (ab == 0) {
        int c = check_chario();
        if (c & 1)
            sio2_queue(chan, next_char());
        if (c & 2) {
            if (!(chan->rr[0] & 0x04)) {
                chan->rr[0] |= 0x04;
                sio2_raise_int(chan, INT_TX);
            }
        }
    }
}

static void sio2_timer(void)
{
    sio2_channel_timer(sio, 0);
    sio2_channel_timer(sio + 1, 1);
}

static void sio2_channel_reset(struct z80_sio_chan *chan)
{
    chan->rr[0] = 0x2C;
    chan->rr[1] = 0x01;
    chan->rr[2] = 0;
    sio2_clear_int(chan, INT_RX|INT_TX|INT_ERR);
}

static void sio_reset(void)
{
    sio2_channel_reset(sio);
    sio2_channel_reset(sio + 1);
}

static uint8_t sio2_read(uint16_t addr)
{
    struct z80_sio_chan *chan =  (addr & 2) ? sio + 1 : sio;
    if (!(addr & 1)) {
        /* Control */
        uint8_t r = chan->wr[0] & 007;
        chan->wr[0] &= ~007;

        if (trace & TRACE_SIO)
            fprintf(stderr, "sio%c read reg %d\n",
                (addr & 2)?'b':'a', r);
        switch(r) {
            case 0:
            case 1:
                return chan->rr[r];
            case 2:
                if (chan != sio)
                    return chan->rr[2];
            case 3:
                /* What does the hw report ?? */
                return 0xFF;
        }
    } else {
        /* FIXME: irq handling */
        uint8_t c  = chan->data[0];
        chan->data[0] = chan->data[1];
        chan->data[1] = chan->data[2];
        if (chan->dptr)
            chan->dptr--;
        if (chan->dptr == 0)
            chan->rr[0] &= 0xFE;	/* Clear RX pending */
        sio2_clear_int(chan, INT_RX);
        chan->rr[0] &= 0x3F;
        chan->rr[1] &= 0x3F;
        if (trace & TRACE_SIO)
            fprintf(stderr, "sio%c read data %d\n",
                (addr & 2)?'b':'a', c);
        return c;
    }
    return 0xFF;
}

static void sio2_write(uint16_t addr, uint8_t val)
{
    struct z80_sio_chan *chan =  (addr & 2) ? sio + 1 : sio;
    if (!(addr & 1)) {
        if (trace & TRACE_SIO)
            fprintf(stderr, "sio%c write reg %d with %02X\n",
                (addr & 2)?'b':'a', chan->wr[0] & 7, val);
        switch(chan->wr[0] & 007) {
            case 0:
                chan->wr[0] = val;
                /* FIXME: CRC reset bits ? */
                switch(val & 070) {
                    case 000:	/* NULL */
                        break;
                    case 010:	/* Send Abort SDLC */
                        /* SDLC specific no-op for async */
                        break;
                    case 020:	/* Reset external/status interrupts */
                        sio2_clear_int(chan, INT_ERR);
                        chan->rr[1] &= 0xCF ; /* Clear status bits on rr0 */
                        break;
                    case 030:	/* Channel reset */
                        if (trace & TRACE_SIO)
                            fprintf(stderr, "[channel reset]\n");
                        sio2_channel_reset(chan);
                        break;
                    case 040:	/* Enable interrupt on next rx */
                        chan->rxint = 1;
                        break;
                    case 050:	/* Reset transmitter interrupt pending */
                        chan->txint = 0;
                        break;
                    case 060:	/* Reset the error latches */
                        chan->rr[1] &= 0x8F;
                        break;
                    case 070:	/* Return from interrupt (channel A) */
                        if (chan == sio) {
                            sio->irq = 0;
                            sio->rr[1] &= ~0x02;
                            sio2_clear_int(sio, INT_RX|INT_TX|INT_ERR);
                            sio2_clear_int(sio + 1, INT_RX|INT_TX|INT_ERR);
                        }
                        break;
                }
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                chan->wr[chan->wr[0] & 7] = val;
                chan->wr[0] &= ~007;
                break;
        }
        /* Control */
    } else {
        /* Strictly we should emulate this as two bytes, one going out and
           the visible queue - FIXME */
        /* FIXME: irq handling */
        chan->rr[0] &= ~(1 << 2);	/* Transmit buffer no longer empty */
        chan->txint = 1;
        /* Should check chan->wr[5] & 8 */
        sio2_clear_int(chan, INT_TX);
        if (trace & TRACE_SIO)
            fprintf(stderr, "sio%c write data %d\n",
                (addr & 2)?'b':'a', val);
        write(1, &val, 1);
    }
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

/* Real time clock state machine and related state.

   Give the host time and don't emulate time setting except for
   the 24/12 hour setting.
   
   Doesn't emulate burst mode
 */
static uint8_t rtcw;
static uint8_t rtcst;
static uint8_t rtcr;
static uint8_t rtccnt;
static uint8_t rtcstate;
static uint8_t rtcreg;
static uint8_t rtcram[32];
static uint8_t rtcwp = 0x80;
static uint8_t rtc24 = 1;
static struct tm *rtc_tm;

static int rtc;

static uint8_t rtc_read(uint16_t addr)
{
    if (rtcst & 0x30)
        return rtcr & 0x80;
    return 0x80;
}

static void rtcop(void)
{
    unsigned int v;
    /* The emulated task asked us to write a byte, and has now provided
       the data byte to go with it */
    if (rtcstate == 2) {
        if (!rtcwp) {
            /* We did a real write! */
            /* Not yet tackled burst mode */
            if (rtcreg != 0x3F && (rtcreg & 0x20))	/* NVRAM */
                rtcram[rtcreg&0x1F] = rtcw;
            else if (rtcreg == 2)
                rtc24 =  rtcw & 0x80;
            else if (rtcreg == 7)
                rtcwp = rtcw & 0x80;
        }
        /* For now don't emulate writes to the time */
        rtcstate = 0;
    }
    /* Check for broken requests */
    if (!(rtcw & 0x80)) {
        rtcstate = 0;
        rtcr = 0xFF;
        return;
    }
    /* A write request */
    if (rtcw & 0x01) {
        rtcstate = 2;
        rtcreg = (rtcw >> 1) & 0x3F;
        rtcr = 0xFF;
        return;
    }
    /* A read request */
    rtcstate = 1;
    if (rtcw & 0x40) {
        /* RAM */
        if (rtcw != 0xFE)
            rtcr = rtcram[(rtcw >> 1) & 0x1F];
        return;
    }
    /* Register read */
    switch((rtcw >> 1) & 0x1F) {
        case 0:
            rtcr = (rtc_tm->tm_sec % 10) +
                   ((rtc_tm->tm_sec / 10) << 4);
            break;
        case 1:
            rtcr = (rtc_tm->tm_min % 10) +
                   ((rtc_tm->tm_min / 10) << 4);
            break;
        case 2:
            v = rtc_tm->tm_hour;            
            if (!rtc24) {
                v %= 12;
                v++;
            }
            rtcr = (v % 10) + ((v / 10) << 4);
            if (!rtc24) {
                if (rtc_tm->tm_hour > 11)
                    rtcr |= 0x20;
                rtcr |= 0x80;
            }
            break;
        case 3:
            rtcr = (rtc_tm->tm_mday % 10) +
                   ((rtc_tm->tm_mday / 10) << 4);
            break;
        case 4:
            rtcr = ((rtc_tm->tm_mon + 1) % 10) +
                   (((rtc_tm->tm_mon + 1) / 10) << 4);
            break;
        case 5:
            rtcr = rtc_tm->tm_wday + 1;
            break;
        case 6:
            v = rtc_tm->tm_year % 100;
            rtcr = (v % 10) + ((v / 10) << 4);
            break;
        case 7:
            rtcr = rtcwp ? 0x80 : 0x00;
            break;
        case 8:
            rtcr = 0;
            break;
        default:
            rtcr = 0xFF;
            /* Check */
            break;
    }
}

static void rtc_write(uint16_t addr, uint8_t val)
{
    uint8_t changed = val ^ rtcst;
    /* Clock */
    if (changed & 040) {
        /* The rising edge samples, the falling edge clocks receive */
        if (!(val & 0x40))
            rtcr <<= 1;
        else {
            rtcw <<= 1;
            if ((val & 0x30) == 0x10)
                rtcw |= (val & 0x80) ? 1 : 0;
            else
                rtcw |= 1;
            rtccnt++;
            if (rtccnt == 8)
                rtcop();
        }
    }
    /* CE */
    if (changed & 0x10) {
        if (rtcst & 0x10) {
            rtccnt = 0;
            rtcstate = 0;
        } else {
            /* Latch imaginary registers on rising edge */
            time_t t= time(NULL);
            rtc_tm = localtime(&t);
        }
    }
    rtcst = val;
}

/*
 *	Emulate the switchable ROM card. We switch between the ROM and
 *	two banks of RAM (any two will do providing it's not the ones we
 *	pretended the bank mapping used for the top 32K). You can't mix the
 *	512K ROM/RAM with this card anyway.
 */
static void toggle_rom(void)
{
    if (bankreg[0] == 0) {
        if (trace & TRACE_ROM)
            fprintf(stderr, "[ROM out]\n");
        bankreg[0] = 34;
        bankreg[1] = 35;
    } else {
        if (trace & TRACE_ROM)
            fprintf(stderr, "[ROM in]\n");
        bankreg[0] = 0;
        bankreg[1] = 1;
    }
}

static uint8_t io_read(int unused, uint16_t addr)
{
    if (trace & TRACE_IO)
        fprintf(stderr, "read %02x\n", addr);
    addr &= 0xFF;
    if ((addr >= 0x80 && addr <= 0xBF) && acia)
	return acia_read(addr & 1);
    if ((addr >= 0x80 && addr <= 0x83) && sio2)
	return sio2_read(addr & 3);
    if ((addr >= 0x10 && addr <= 0x17) && ide)
	return my_ide_read(addr & 7);
    if (addr == 0xC0 && rtc)
	return rtc_read(addr);
    if (trace & TRACE_UNK)
        fprintf(stderr, "Unknown read from port %04X\n", addr);
    return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
    if (trace & TRACE_IO)
        fprintf(stderr, "write %02x <- %02x\n", addr, val);
    addr &= 0xFF;
    if ((addr >= 0x80 && addr <= 0xBF) && acia)
	acia_write(addr & 1, val);
    else if ((addr >= 0x80 && addr <= 0x83) && sio2)
	sio2_write(addr & 3, val);
    else if ((addr >= 0x10 && addr <= 0x17) && ide)
	my_ide_write(addr & 7, val);
    else if (bank512 && addr >= 0x78 && addr <= 0x7B)
	bankreg[addr & 3] = val;
    else if (bank512 && addr == 0x7C)
	bankenable = val & 1;
    else if (addr == 0xC0 && rtc)
	rtc_write(addr, val);
    else if (switchrom && addr == 0x38)
        toggle_rom();
    else if (cpuboard == 1 && addr == 0x38) {
        if (trace & TRACE_ROM)
            fprintf(stderr, "Bank set to %02X\n", val);
        port38 = val;
    } else if (trace & TRACE_UNK)
        fprintf(stderr, "Unknown write to port %04X of %02X\n",
            addr, val);
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
    fprintf(stderr, "rc2014: [-a] [-b] [-c] [-m mainboard] [-r bank] [-e rombank] [-s]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    static struct timespec tc;
    int opt;
    int fd;
    int rom = 1;
    int rombank = 0;
    char *rompath = "rc2014.rom";
    char *idepath;

    while((opt = getopt(argc, argv, "abc:e:i:m:r:s")) != -1) {
        switch(opt) {
            case 'a':
                acia = 1;
                break;
            case 'r':
                rompath = optarg;
                break;
            case 's':
                sio2 = 1;
                break;
            case 'e':
                rombank = atoi(optarg);
                break;
            case 'b':
                bank512 = 1;
                switchrom = 0;
                rom = 0;
                break;
            case 'i':
                ide = 1;
                idepath = optarg;
                break;
            case 'c':
                ctc = 1;	/* TODO */
                break;
            case 'm':
                /* Default Z80 board */
                if (strcmp(optarg, "z80") == 0)
                    cpuboard = 0;
                else if (strcmp(optarg, "sc108") == 0) {
                    switchrom = 0;
                    bank512 = 0;
                    cpuboard = 1;
                } else {
                    fputs("rc2014: supported cpu types z80, sc108.\n", stderr);
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                usage();
        }
    }
    if (optind < argc)
        usage();

    if (acia == 0 && sio2 == 0) {
        fprintf(stderr, "rc2014: no UART selected, defaulting to 68B50\n");
        acia = 1;
    }
    if (rom == 0 && bank512 == 0) {
        fprintf(stderr, "rc2014: no ROM\n");
        exit(EXIT_FAILURE);
    }

    if (rom) {
        fd = open(rompath, O_RDONLY);
        if (fd == -1) {
	    perror(rompath);
            exit(EXIT_FAILURE);
        }
        bankreg[0] = 0;
        bankreg[1] = 0;
        bankreg[2] = 32;
        bankreg[3] = 33;
        bankenable = 1;
        if (lseek(fd, 8192 * rombank, SEEK_SET) < 0) {
            perror("lseek");
            exit(1);
        }
        if (read(fd, ramrom, 65536) < 8192) {
            fprintf(stderr, "rc2014: short rom '%s'.\n", rompath);
            exit(EXIT_FAILURE);
        }
        close(fd);
    }

    if (bank512) {
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
    }

    if (ide) {
        ide0 = ide_allocate("cf");
        if (ide0) {
            fd = open(idepath, O_RDWR);
            if (fd == -1) {
                perror(idepath);
                ide = 0;
            }
            if (ide_attach(ide0, 0, fd) == 0) {
                ide = 1;
                ide_reset_begin(ide0);
            }
        } else
            ide = 0;
    }

    if (sio2)
        sio_reset();

    /* 50ms - it's a balance between nice behaviour and simulation
       smoothness */
    tc.tv_sec = 0;
    tc.tv_nsec = 50000000L;

    if (tcgetattr(0, &term) == 0) {
	saved_term = term;
	atexit(exit_cleanup);
	signal(SIGINT, cleanup);
	signal(SIGQUIT, cleanup);
	term.c_lflag &= ~(ICANON|ECHO);
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 0;
	tcsetattr(0, TCSADRAIN, &term);
    }

    Z80RESET(&cpu_z80);
    cpu_z80.ioRead = io_read;
    cpu_z80.ioWrite = io_write;
    cpu_z80.memRead = mem_read;
    cpu_z80.memWrite = mem_write;

    /* This is the wrong way to do it but it's easier for the moment. We
       should track how much real time has occurred and try to keep cycle
       matched with that. The scheme here works fine except when the host
       is loaded though */

    while (!done) {
        int i;
        /* 36400 T states */
        for (i = 0;i < 100; i++) {
            Z80ExecuteTStates(&cpu_z80, 364);
            if (acia)
	        acia_timer();
	    if (sio2)
                sio2_timer();
        }
	/* Do 5ms of I/O and delays */
	nanosleep(&tc, NULL);
    }
    exit(0);
}
