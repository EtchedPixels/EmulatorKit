/*
 *	Platform features
 *	Z80A @ 8Mhz
 *	1MB ROM (max), 512K RAM
 *	16550A UART @1.8432Mhz at I/O 0x68
 *	DS1302 bitbanged RTC
 *	8255 for PPIDE etc
 *	Memory banking
 *	0x78-7B: RAM bank
 *	0x7C-7F: ROM bank (or set bit 7 to get RAM bank)
 *
 *	IRQ from serial only, or from ECB bus but not serial
 *	Optional PropIO v2 for I/O ports (keyboard/video/sd)
 *
 *	General stuff to tackle
 *	Interrupt jumper (ECB v 16x50)
 *	16x50 interrupts	(partly done)
 *	ECB and timer via UART interrupt hack (timer done)
 *	DS1302 burst mode for memory
 *	Do we care about DS1302 writes ?
 *	Whine/break on invalid PPIDE sequences to help debug code
 *	Memory jumpers (is it really 16/48 or 48/16 ?)
 *	Z80 CTC card (ECB)
 *	4UART needs connecting to something as does uart0 when in PropIO
 *	SCG would be fun but major work (does provide vblank though)
 *
 *	Fix usage!
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

#define HIRAM	63

static uint8_t ramrom[64][32768];	/* 512K ROM for now */
static uint8_t rombank;
static uint8_t rambank;

static uint8_t ide;
static struct ide_controller *ide0;
static char *sdcard_path = "sdcard.img";
static uint8_t prop;
static uint8_t timerhack;

static Z80Context cpu_z80;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_RTC	16
#define TRACE_PPIDE	32
#define TRACE_PROP	64
#define TRACE_BANK	128

static int trace = 0;/* TRACE_MEM|TRACE_IO|TRACE_UNK; */

static uint8_t mem_read(int unused, uint16_t addr)
{
    if (trace & TRACE_MEM)
        fprintf(stderr, "R %04X: ", addr);
    if (addr > 32767) {
        if (trace & TRACE_MEM)
            fprintf(stderr, "HR %04X<-%02X\n",
                addr & 0x7FFF, ramrom[32][addr & 0x7FFF]);
        return ramrom[HIRAM][addr & 0x7FFF];
    }
    if (rombank & 0x80) {
        if (trace & TRACE_MEM)
            fprintf(stderr, "LR%d %04X<-%02X\n",
                rambank & 0x1F, addr, ramrom[32 + (rambank & 0x1F)][addr]);
        return ramrom[32 + (rambank & 0x1F)][addr];
    }
    if (trace & TRACE_MEM)
        fprintf(stderr, "LF%d %04X<->%02X\n",
            rombank & 0x1F, addr, ramrom[rombank & 0x1F][addr]);
    return ramrom[rombank & 0x1F][addr];
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
    if (trace & TRACE_MEM)
        fprintf(stderr, "W %04X: ", addr);
    if (addr > 32767) {
        if (trace & TRACE_MEM)
            fprintf(stderr, "HR %04X->%02X\n",addr, val);
        ramrom[HIRAM][addr & 0x7FFF] = val;
    }
    else if (rombank & 0x80) {
        if (trace & TRACE_MEM)
            fprintf(stderr, "LR%d %04X->%02X\n", (rambank & 0x1F), addr, val);
        ramrom[32 + (rambank & 0x1F)][addr] = val;
    } else if (trace & TRACE_MEM)
        fprintf(stderr, "LF%d %04X->ROM\n",
            (rombank & 0x1F), addr);
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

/*
 *	Emulate PPIDE. It's not a particularly good emulation of the actual
 *	port behaviour if misprogrammed but should be accurate for correct
 *	use of the device.
 */
static uint8_t pioreg[4];

static void pio_write(uint8_t addr, uint8_t val)
{
    /* Compute all the deltas */
    uint8_t changed = pioreg[addr] ^ val;
    uint8_t dhigh = val & changed;
    uint8_t dlow = ~val & changed;
    uint16_t d;

    switch(addr) {
        case 0:	/* Port A data */
        case 1:	/* Port B data */
            pioreg[addr] = val;
            if (trace & TRACE_PPIDE)
                fprintf(stderr, "Data now %04X\n", (((uint16_t)pioreg[1]) << 8) | pioreg[0]);
            break;
        case 2:	/* Port C - address/control lines */
            pioreg[addr] = val;
            if (val & 0x80) {
                if (trace & TRACE_PPIDE)
                    fprintf(stderr, "ide in reset.\n");
                ide_reset_begin(ide0);
                return;
            }
            if ((trace & TRACE_PPIDE) && (dlow & 0x80))
                fprintf(stderr, "ide exits reset.\n");

            /* This register is effectively the bus to the IDE device
               bits 0-2 are A0-A2, bit 3 is CS0 bit 4 is CS1 bit 5 is W
               bit 6 is R bit 7 is reset */
            d = val & 0x07;
            /* Altstatus and friends */
            if (val & 0x10)
                d += 2;
            if (dlow & 0x20) {
                if (trace & TRACE_PPIDE)
                    fprintf(stderr, "write edge: %02X = %04X\n", d,
                        ((uint16_t)pioreg[1] << 8) | pioreg[0]);
                ide_write16(ide0, d, ((uint16_t)pioreg[1] << 8) | pioreg[0]);
            } else if (dhigh & 0x40) {
                /* Prime the data ports on the rising edge */
                if (trace & TRACE_PPIDE)
                    fprintf(stderr, "read edge: %02X = ", d);
                d = ide_read16(ide0, d);
                if (trace & TRACE_PPIDE)
                    fprintf(stderr, "%04X\n", d);
                pioreg[0] = d;
                pioreg[1] = d >> 8;
            }
            break;
        case 3: /* Control register */
            /* We could check the direction bits but we don't */
            pioreg[addr] = val;
            break;
    }
}

static uint8_t pio_read(uint8_t addr)
{
    if (trace & TRACE_PPIDE)
        fprintf(stderr, "ide read %d:%02X\n", addr, pioreg[addr]);
    return pioreg[addr];
}

/* UART: very mimimal for the moment */

struct uart16x50 {
    uint8_t ier;
    uint8_t iir;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scratch;
    uint8_t ls;
    uint8_t ms;
    uint8_t dlab;
    uint8_t irq;
#define RXDA	1
#define TEMT	2
#define MODEM	8
    uint8_t irqline;
};

static struct uart16x50 uart[5];

static void uart_init(struct uart16x50 *uptr)
{
    uptr->dlab = 0;
}

/* Compute the interrupt indicator register from what is pending */
static void uart_recalc_iir(struct uart16x50 *uptr)
{
    if (uptr->irq & RXDA)
        uptr->iir = 0x04;
    else if (uptr->irq & TEMT)
        uptr->iir = 0x02;
    else if (uptr->irq & MODEM)
        uptr->iir = 0x00;
    else {
        uptr->iir = 0x01;	/* No interrupt */
        uptr->irqline = 0;
        return;
    }
    /* Ok so we have an event, do we need to waggle the line */
    if (uptr->irqline)
        return;
    uptr->irqline = 1;
    Z80INT(&cpu_z80, 0xFF);	/* actually undefined */
    
}

/* Raise an interrupt source. Only has an effect if enabled in the ier */
static void uart_interrupt(struct uart16x50 *uptr, uint8_t n)
{
    if (uptr->irq & n)
        return;
    if (!(uptr->ier & n))
        return;
    uptr->irq |= n;
    uart_recalc_iir(uptr);
}

static void uart_clear_interrupt(struct uart16x50 *uptr, uint8_t n)
{
    if (!(uptr->irq & n))
        return;
    uptr->irq &= ~n;
    uart_recalc_iir(uptr);
}

static void uart_event(struct uart16x50 *uptr)
{
    uint8_t r = check_chario();
    uint8_t old = uptr->lsr;
    uint8_t dhigh;
    if (r & 1)
        uptr->lsr |= 0x01;	/* RX not empty */
    if (r & 2)
        uptr->lsr |= 0x60;	/* TX empty */
    dhigh = (old ^ uptr->lsr);
    dhigh &= uptr->lsr;		/* Changed high bits */
    if (dhigh & 1)
        uart_interrupt(uptr, RXDA);
    if (dhigh & 0x2)
        uart_interrupt(uptr, TEMT);
}

static void uart_write(struct uart16x50 *uptr, uint8_t addr, uint8_t val)
{
    switch(addr) {
    case 0:	/* If dlab = 0, then write else LS*/
        if (uptr->dlab == 0 && uptr == &uart[0]) {
            putchar(val);
            fflush(stdout);
            uart_clear_interrupt(uptr, TEMT);
            uart_interrupt(uptr, TEMT);
        }
        break;
    case 1:	/* If dlab = 0, then IER (ro) */
        uptr->ier = val & 0x0F;
        break;
    case 2:	/* FCR */
        uptr->fcr = val & 0x9F;
        break;
    case 3:	/* LCR */
        uptr->lcr = val;
        uptr->dlab = (uptr->lcr & 0x80);
        break;
    case 4:	/* MCR */
        uptr->mcr = val & 0x3F;
        break;
    case 5:	/* LSR (r/o) */
        break;
    case 6:	/* MSR (r/o) */
        break;
    case 7:	/* Scratch */
        uptr->scratch = val;
        break;
    }
}

static uint8_t uart_read(struct uart16x50 *uptr, uint8_t addr)
{
    uint8_t r;

    switch(addr) {
    case 0:
        /* receive buffer */
        if (!prop && uptr == &uart[0] && uptr->dlab == 0) {
            uart_clear_interrupt(uptr, RXDA);
            return next_char();
        }
        break;
    case 1:
        /* IER */
        return uptr->ier;
    case 2:
        /* IIR */
        return uptr->iir;
    case 3:
        /* LCR */
        return uptr->lcr;
    case 4:
        /* mcr */
        return uptr->mcr;
    case 5:
        /* lsr */
        r = check_chario();
        uptr->lsr = 0;
        if (!prop && (r & 1))
             uptr->lsr |= 0x01;	/* Data ready */
        if (r & 2)
             uptr->lsr |= 0x60;	/* TX empty | holding empty */
        /* Reading the LSR causes these bits to clear */
        r = uptr->lsr;
        uptr->lsr &= 0xF0;
        return r;
    case 6:
        /* msr */
        return uptr->msr;
    case 7:
        return uptr->scratch;
    }
    return 0xFF;
}

/* Clock timer hack. The (signal level) DSR line on the jumpers is connected
   to a slow clock generator */
static void timer_pulse(void)
{
    struct uart16x50 *uptr = uart;
    if (timerhack) {
        uptr->msr ^= 0x40;	/* DSR toggles */
        uptr->msr |= 0x02;	/* DSR delta */
        uart_interrupt(uptr, MODEM);
    }
}

/* Real time clock state machine and related state.

   Give the host time and don't emulate time setting except for
   the 24/12 hour setting.
   
   Doesn't emulate burst mode which we need to fix for ROMWBW
 */
static uint8_t rtcw;
static uint8_t rtcst;
static uint16_t rtcr;
static uint8_t rtccnt;
static uint8_t rtcstate;
static uint8_t rtcreg;
static uint8_t rtcram[32];
static uint8_t rtcwp = 0x80;
static uint8_t rtc24 = 1;
static uint8_t rtcbp = 0;
static uint8_t rtcbc = 0;
static struct tm *rtc_tm;

static uint8_t rtc_read(void)
{
    if (rtcst & 0x30)
        return (rtcr & 0x01) ? 1 : 0;
    return 0xFF;
}

static uint16_t rtcregread(uint8_t reg)
{
    uint8_t val, v;

    switch(reg)
    {
        case 0:
            val = (rtc_tm->tm_sec % 10) +
                   ((rtc_tm->tm_sec / 10) << 4);
            break;
        case 1:
            val = (rtc_tm->tm_min % 10) +
                   ((rtc_tm->tm_min / 10) << 4);
            break;
        case 2:
            v = rtc_tm->tm_hour;
            if (!rtc24) {
                v %= 12;
                v++;
            }
            val = (v % 10) + ((v / 10) << 4);
            if (!rtc24) {
                if (rtc_tm->tm_hour > 11)
                    val |= 0x20;
                val |= 0x80;
            }
            break;
        case 3:
            val = (rtc_tm->tm_mday % 10) +
                   ((rtc_tm->tm_mday / 10) << 4);
            break;
        case 4:
            val = ((rtc_tm->tm_mon + 1) % 10) +
                   (((rtc_tm->tm_mon + 1) / 10) << 4);
            break;
        case 5:
            val = rtc_tm->tm_wday + 1;
            break;
        case 6:
            v = rtc_tm->tm_year % 100;
            val = (v % 10) + ((v / 10) << 4);
            break;
        case 7:
            val = rtcwp ? 0x80 : 0x00;
            break;
        case 8:
            val = 0;
            break;
        default:
            val = 0xFF;
            /* Check */
            break;
    }
    if (trace & TRACE_RTC)
        fprintf(stderr, "RTCreg %d = %02X\n", reg, val);
    return val;
}

static void rtcop(void)
{
    if (trace & TRACE_RTC)
        fprintf(stderr, "rtcbyte %02X\n", rtcw);
    /* The emulated task asked us to write a byte, and has now provided
       the data byte to go with it */
    if (rtcstate == 2) {
        if (!rtcwp) {
            if (trace & TRACE_RTC)
                fprintf(stderr, "RTC write %d as %d\n", rtcreg, rtcw);
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
        if (trace & TRACE_RTC)
            fprintf(stderr, "rtcw makes no sense %d\n", rtcw);
        rtcstate = 0;
        rtcr = 0x1FF;
        return;
    }
    /* Clock burst ? : for now we only emulate time burst */
    if (rtcw == 0xBF) {
        rtcstate = 3;
        rtcbp = 0;
        rtcbc = 0;
        rtcr = rtcregread(rtcbp++) << 1;
        if (trace & TRACE_RTC)
            fprintf(stderr, "rtc command BF: burst clock read.\n");
        return;
    }
    /* A write request */
    if (!(rtcw & 0x01)) {
        if (trace & TRACE_RTC)
            fprintf(stderr, "rtc write request, waiting byte 2.\n");
        rtcstate = 2;
        rtcreg = (rtcw >> 1) & 0x3F;
        rtcr = 0x1FF;
        return;
    }
    /* A read request */
    rtcstate = 1;
    if (rtcw & 0x40) {
        /* RAM */
        if (rtcw != 0xFE)
            rtcr = rtcram[(rtcw >> 1) & 0x1F] << 1;
        if (trace & TRACE_RTC)
            fprintf(stderr, "RTC RAM read %d, ready to clock out %d.\n",
                    (rtcw >> 1) & 0xFF, rtcr);
        return;
    }
    /* Register read */
    rtcr = rtcregread((rtcw >> 1) & 0x1F) << 1;
    if (trace & TRACE_RTC)
        fprintf(stderr, "RTC read of time register %d is %d\n",
            (rtcw >> 1) & 0x1F, rtcr);
}

static void rtc_write(uint8_t val)
{
    uint8_t changed = val ^ rtcst;
    uint8_t is_read;
    /* Direction */
    if ((trace & TRACE_RTC) && (changed & 0x20))
        fprintf(stderr, "RTC direction now %s.\n", (val & 0x20)?"read":"write");
    is_read = val & 0x20;
    /* Clock */
    if (changed & 0x40) {
        /* The rising edge samples, the falling edge clocks receive */
        if (trace & TRACE_RTC)
            fprintf(stderr, "RTC clock low.\n");
        if (!(val & 0x40)) {
            rtcr >>= 1;
            /* Burst read of time */
            rtcbc++;
            if (rtcbc == 8 && rtcbp) {
                rtcr = rtcregread(rtcbp++) << 1;
                rtcbc = 0;
            }
            if (trace & TRACE_RTC)
                fprintf(stderr, "rtcr now %02X\n", rtcr);
        } else {
            if (trace & TRACE_RTC)
                fprintf(stderr, "RTC clock high.\n");
            rtcw >>= 1;
            if ((val & 0x30) == 0x10)
                rtcw |= val & 0x80;
            else
                rtcw |= 0xFF;
            rtccnt++;
            if (trace & TRACE_RTC)
                fprintf(stderr, "rtcw now %02x (%d)\n", rtcw, rtccnt);
            if (rtccnt == 8 && !is_read)
                rtcop();
        }
    }
    /* CE */
    if (changed & 0x10) {
        if (rtcst & 0x10) {
            if (trace & TRACE_RTC)
                fprintf(stderr, "RTC CE dropped.\n");
            rtccnt = 0;
            rtcr = 0;
            rtcw = 0;
            rtcstate = 0;
        } else {
            /* Latch imaginary registers on rising edge */
            time_t t= time(NULL);
            rtc_tm = localtime(&t);
            if (trace & TRACE_RTC)
                fprintf(stderr, "RTC CE raised and latched time.\n");
        }
    }
    rtcst = val;
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
    prop_rptr = prop_rbuf;
    prop_tptr = prop_rbuf;
    prop_rlen = 4;
    prop_tlen = 4;
}

static uint8_t prop_read(uint8_t addr)
{
    uint8_t r, v;
    switch(addr) {
    case 0:		/* Console status */
        v = check_chario();
        r = (v & 1) ? 0x20:0x00;
        if (v & 2)
            r |= 0x10;
        return r;
    case 1:		/* Keyboard input */
        return next_char();
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

static uint8_t io_read(int unused, uint16_t addr)
{
    if (trace & TRACE_IO)
        fprintf(stderr, "read %02x\n", addr);
    addr &= 0xFF;
    if (addr >= 0x60 && addr <= 0x67) 	/* Aliased */
        return pio_read(addr & 3);
    if (addr >= 0x68 && addr < 0x70)
        return uart_read(&uart[0], addr & 7);
    if (addr >= 0x70 && addr <= 0x77)
        return rtc_read();
    if (prop && (addr >= 0xA8 && addr <= 0xAF))
        return prop_read(addr & 7);
    if (addr >= 0xC0 && addr <= 0xDF)
        return uart_read(&uart[((addr - 0xC0) >> 3) + 1], addr & 7);
    if (trace & TRACE_UNK)
        fprintf(stderr, "Unknown read from port %04X\n", addr);
    return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
    if (trace & TRACE_IO)
        fprintf(stderr, "write %02x <- %02x\n", addr & 0xFF, val);
    addr &= 0xFF;
    if (addr >= 0x60 && addr <= 0x67)	/* Aliased */
        pio_write(addr & 3, val);
    else if (addr >= 0x68 && addr < 0x70)
        uart_write(&uart[0], addr & 7, val);
    else if (addr >= 0x70 && addr <= 0x77)
        rtc_write(val);
    else if (addr >= 0x78 && addr <= 0x79) {
        if (trace & TRACE_BANK)
            fprintf(stderr, "RAM bank to %02X\n", val);
        rambank = val;
    } else if (addr >= 0x7C && addr <= 0x7F) {
        if (trace & TRACE_BANK) {
            fprintf(stderr, "ROM bank to %02X\n", val);
            if (val & 0x80)
                fprintf(stderr, "Using RAM bank %d\n", rambank & 0x1F);
        }
        rombank = val;
    }
    else if (prop && addr >= 0xA8 && addr <= 0xAF)
        prop_write(addr & 0x07, val);
    else if (addr >= 0xC0 && addr <= 0xDF)
        uart_write(&uart[((addr - 0xC0) >> 3) + 1], addr & 7, val);
    else if (trace & TRACE_UNK)
        fprintf(stderr, "Unknown write to port %02X of %02X\n",
            addr & 0xFF, val);
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
    fprintf(stderr, "n8vem: [-r rompath] [-i idepath]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    static struct timespec tc;
    int opt;
    int fd;
    char *rompath = "sbc.rom";
    char *idepath[2] = { NULL, NULL };
    int i;

    while((opt = getopt(argc, argv, "r:i:s:ptd:")) != -1) {
        switch(opt) {
            case 'r':
                rompath = optarg;
                break;
                break;
            case 'i':
                if (ide == 2)
                    fprintf(stderr, "n8vem2: only two disks per controller.\n");
                else
                    idepath[ide++] = optarg;
                break;
            case 's':
                sdcard_path = optarg;
            case 'p':
                prop = 1;
                break;
            case 't':
                timerhack = 1;
                break;
            case 'd':
                trace = atoi(optarg);
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
    for (i = 0; i < 16; i++) {
        if (read(fd, ramrom[i], 32768) != 32768) {
            fprintf(stderr, "n8vem2: banked rom image should be 512K.\n");
            exit(EXIT_FAILURE);
        }
    }
    close(fd);

    if (ide) {
        ide0 = ide_allocate("cf");
        if (ide0) {
            fd = open(idepath[0], O_RDWR);
            if (fd == -1) {
                perror(idepath[0]);
                ide = 0;
            } else if (ide_attach(ide0, 0, fd) == 0) {
                ide = 1;
                ide_reset_begin(ide0);
            }
            if (idepath[1]) {
                fd = open(idepath[1], O_RDWR);
                if (fd == -1)
                    perror(idepath[1]);
                ide_attach(ide0, 1, fd);
            }
        } else
            ide = 0;
    }

    prop_init();

    uart_init(&uart[0]);
    uart_init(&uart[1]);
    uart_init(&uart[2]);
    uart_init(&uart[3]);
    uart_init(&uart[4]);

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
	term.c_lflag &= ~(ICANON|ECHO);
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 0;
	term.c_cc[VINTR] = 0;
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

    /* 4MHz Z80 - 4,000,000 tstates / second */
    while (!done) {
        Z80ExecuteTStates(&cpu_z80, 800000);
	/* Do 20ms of I/O and delays */
	nanosleep(&tc, NULL);
	uart_event(uart);
	timer_pulse();
    }
    exit(0);
}
