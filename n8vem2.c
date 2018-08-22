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
struct ide_controller *ide0;

static Z80Context cpu_z80;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8

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

/* For now: We will model a PPIDE later on */
static uint8_t pioreg[4];

static void pio_write(uint8_t addr, uint8_t val)
{
    pioreg[addr] = val;
}

static uint8_t pio_read(uint8_t addr)
{
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
};

static struct uart16x50 uart[5];

static void uart_init(struct uart16x50 *uptr)
{
    uptr->dlab = 0;
}

static void uart_write(struct uart16x50 *uptr, uint8_t addr, uint8_t val)
{
    switch(addr) {
    case 0:	/* If dlab = 0, then write else LS*/
        if (uptr->dlab == 0 && uptr == &uart[0]) {
            putchar(val);
            fflush(stdout);
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
        if (uptr == &uart[0] && uptr->dlab == 0)
            return next_char();
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
        if (r & 1)
            uptr->lsr |= 0x01;	/* Data ready */
        if (r & 2)
            uptr->lsr |= 0x60;	/* TX empty | holding empty */
        return uptr->lsr;
    case 6:
        /* msr */
        return uptr->msr;
    case 7:
        return uptr->scratch;
    }
    return 0xFF;
}

#if 0
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

#endif

static uint8_t io_read(int unused, uint16_t addr)
{
    if (trace & TRACE_IO)
        fprintf(stderr, "read %02x\n", addr);
    addr &= 0xFF;
    if (addr >= 0x60 && addr < 0x64)
        return pio_read(addr & 3);
    if (addr >= 0x68 && addr < 0x70)
        return uart_read(&uart[0], addr & 7);
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
    if (addr >= 0x60 && addr < 0x64)
        pio_write(addr & 3, val);
    else if (addr >= 0x68 && addr < 0x70)
        uart_write(&uart[0], addr & 7, val);
    else if (addr >= 0x78 && addr <= 0x7B)
        rambank = val;
    else if (addr >= 0x7C && addr <= 0x7F)
        rombank = val;
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
    char *idepath;
    int i;

    while((opt = getopt(argc, argv, "r:i:")) != -1) {
        switch(opt) {
            case 'r':
                rompath = optarg;
                break;
                break;
            case 'i':
                ide = 1;
                idepath = optarg;
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

    uart_init(&uart[0]);
    uart_init(&uart[1]);
    uart_init(&uart[2]);
    uart_init(&uart[3]);
    uart_init(&uart[4]);

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
        for (i = 0;i < 10; i++) {
            Z80ExecuteTStates(&cpu_z80, 3640);
        }
	/* Do 5ms of I/O and delays */
	nanosleep(&tc, NULL);
    }
    exit(0);
}
