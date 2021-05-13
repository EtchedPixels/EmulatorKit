/*
 *	Platform features
 *
 *	Main board:
 *	Z80A @ 4MHz
 *	Bitbanger port (with IRQ)	(emulated as unused)
 *	32K EPROM, 32K base RAM
 *
 *	Front panel board:
 *	7 x 7 segment displays (scanned)	(not emulated)
 *	16 key keypad				(emulated as never pressed)
 *	Hardware reset
 *	1ms timer interrupt
 *
 *	CP/M board
 *	8250 UART @1.8432Mhz
 *	128K or 512K RAM (banked low 32K over EPROM)
 *	MicroSD card
 *
 *	TODO
 *	Is there any sane way to handle the 7 segment displays ?
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
#include <sys/types.h>
#include <sys/mman.h>
#include "libz80/z80.h"
#include "sdcard.h"

static uint8_t bankram[16][32768];
static uint8_t eprom[32768];
static uint8_t ram[32768];

static uint8_t fpreg = 0xB8;	/* No keys no bitbanger */
static uint8_t fpcol;
static uint8_t bankreg;
static uint8_t qreg[8];

static uint8_t fast;

static Z80Context cpu_z80;
static volatile int done;

static struct sdcard *sdcard;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_UNK	4
#define TRACE_BANK	8
#define TRACE_UART	16
#define TRACE_LED	32
#define TRACE_QREG	64
#define TRACE_FPREG	128
#define TRACE_SD	256
#define TRACE_SPI	512

static int trace = 0;

static uint8_t mem_read(int unused, uint16_t addr)
{
    uint8_t r;

    if (trace & TRACE_MEM)
        fprintf(stderr, "R %04X: ", addr);
    if (addr >= 0x8000) {
        r = ram[addr & 0x7FFF];
    } else if (qreg[1] == 0)
        r = eprom[addr];
    else
        r = bankram[bankreg][addr];

    if (trace & TRACE_MEM)
        fprintf(stderr, "<- %02X\n", r);
    return r;
}

static void mem_write(int unused, uint16_t addr, uint8_t val)
{
    if (trace & TRACE_MEM)
        fprintf(stderr, "W %04X: -> %02X\n", addr, val);
    if (addr >= 0x8000)
        ram[addr & 0x7FFF] = val;
    else /* Writes through under the EPROM even if EPROM mapped */
        bankram[bankreg][addr] = val;
}


static uint8_t sd_bits;
static uint8_t sd_bitct;
static uint8_t sd_miso;


static uint8_t spi_byte_sent(uint8_t val)
{
	uint8_t r = 0xFF;
	if (sdcard)
	    r  = sd_spi_in(sdcard, val);
	if (trace & TRACE_SPI)
		fprintf(stderr,	"[SPI %02X:%02X]\n", val, r);
	fflush(stdout);
	return r;
}

static void spi_select(uint8_t val)
{
    if (val) {
	if (trace & TRACE_SPI)
	    fprintf(stderr,	"[Raised \\CS]\n");
	sd_bits = 0;
	sd_bitct = 0;
	if (sdcard)
	    sd_spi_raise_cs(sdcard);
	return;
    } else {
	if (trace & TRACE_SPI)
	    fprintf(stderr,	"[Lowered \\CS]\n");
        if (sdcard)
            sd_spi_lower_cs(sdcard);
    }
}

static void spi_clock(void)
{
	static uint8_t rxbits = 0xFF;

	if (!qreg[0]) {
	    fprintf(stderr, "SPI clock: no op.\n");
	    return;
        }

        if (trace & TRACE_SPI)
            fprintf(stderr, "[SPI clock - txbit = %d ", qreg[5]);
	sd_bits <<= 1;
	sd_bits |= qreg[5];
	sd_bitct++;
	if (sd_bitct == 8) {
		rxbits = spi_byte_sent(sd_bits);
		sd_bitct = 0;
	}
	/* Falling edge */
	sd_miso = (rxbits & 0x80) ? 0x01 : 0x00;
	rxbits <<= 1;
	rxbits |= 0x01;
	if (trace & TRACE_SPI)
	    fprintf(stderr, "rxbit = %d]\n", sd_miso);
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

static struct uart16x50 uart[1];

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
    uptr->irqline = uptr->irq;
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

static void show_settings(struct uart16x50 *uptr)
{
    uint32_t baud;

    if (!(trace & TRACE_UART))
        return;

    baud = uptr->ls + (uptr->ms << 8);
    if (baud == 0)
        baud = 1843200;
    baud = 1843200 / baud;
    baud /= 16;
    fprintf(stderr, "[%d:%d",
            baud, (uptr->lcr &3) + 5);
    switch(uptr->lcr & 0x38) {
        case 0x00:
        case 0x10:
        case 0x20:
        case 0x30:
            fprintf(stderr, "N");
            break;
        case 0x08:
            fprintf(stderr, "O");
            break;
        case 0x18:
            fprintf(stderr, "E");
            break;
        case 0x28:
            fprintf(stderr, "M");
            break;
        case 0x38:
            fprintf(stderr, "S");
            break;
    }
    fprintf(stderr, "%d ",
            (uptr->lcr & 4) ? 2 : 1);

    if (uptr->lcr & 0x40)
        fprintf(stderr, "break ");
    if (uptr->lcr & 0x80)
        fprintf(stderr, "dlab ");
    if (uptr->mcr & 1)
        fprintf(stderr, "DTR ");
    if (uptr->mcr & 2)
        fprintf(stderr, "RTS ");
    if (uptr->mcr & 4)
        fprintf(stderr, "OUT1 ");
    if (uptr->mcr & 8)
        fprintf(stderr, "OUT2 ");
    if (uptr->mcr & 16)
        fprintf(stderr, "LOOP ");
    fprintf(stderr, "ier %02x]\n", uptr->ier);
}

static void uart_write(struct uart16x50 *uptr, uint8_t addr, uint8_t val)
{
    switch(addr) {
    case 0:	/* If dlab = 0, then write else LS*/
        if (uptr->dlab == 0) {
            if (uptr == &uart[0]) {
                putchar(val);
                fflush(stdout);
            }
            uart_clear_interrupt(uptr, TEMT);
            uart_interrupt(uptr, TEMT);
        } else {
            uptr->ls = val;
            show_settings(uptr);
        }
        break;
    case 1:	/* If dlab = 0, then IER */
        if (uptr->dlab) {
            uptr->ms= val;
            show_settings(uptr);
        }
        else
            uptr->ier = val;
        break;
    case 2:	/* FCR */
        uptr->fcr = val & 0x9F;
        break;
    case 3:	/* LCR */
        uptr->lcr = val;
        uptr->dlab = (uptr->lcr & 0x80);
        show_settings(uptr);
        break;
    case 4:	/* MCR */
        uptr->mcr = val & 0x3F;
        bankreg = uptr->mcr & 0x0F;
        if (trace & TRACE_BANK)
            fprintf(stderr, "[Bank %d selected.\n]", bankreg);
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
        if (uptr == &uart[0] && uptr->dlab == 0) {
            uart_clear_interrupt(uptr, RXDA);
            if (check_chario() & 1)
                return next_char();
            return 0x00;
        } else
            return uptr->ls;
        break;
    case 1:
        /* IER */
        if (uptr->dlab == 0)
            return uptr->ier;
        return uptr->ms;
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
        uptr->lsr &=0x90;
        if (r & 1)
             uptr->lsr |= 0x01;	/* Data ready */
        if (r & 2)
             uptr->lsr |= 0x60;	/* TX empty | holding empty */
        /* Reading the LSR causes these bits to clear */
        r = uptr->lsr;
        uptr->lsr &= 0xF0;
        return r;
    case 6:
        /* MSR is used for the SD card MISO line */
        /* msr */
        uptr->msr &= 0x7F;
        uptr->msr |= sd_miso ? 0x00 : 0x80;
        r = uptr->msr;
        /* Reading clears the delta bits */
        uptr->msr &= 0xF0;
        uart_clear_interrupt(uptr, MODEM);
        return r;
    case 7:
        return uptr->scratch;
    }
    return 0xFF;
}

static void fpreg_write(uint8_t val)
{
    fpreg &= ~0x47;	/* IRQ clear, clear counter */
    /* 7 segment scanner not yet simulated */
    fpcol++;
    fpcol &=7;
    fpreg |= fpcol;
    if (trace & TRACE_FPREG)
        fprintf(stderr, "fpreg write %02x now %02x\n", val, fpreg);
}

static uint8_t qreg_read(uint8_t addr)
{
    if (trace & TRACE_QREG)
        fprintf(stderr, "Q%d Read\n", addr);
    spi_clock();
    return 0xFF;
}

static void qreg_write(uint8_t reg, uint8_t v)
{
    if (trace & TRACE_QREG)
        fprintf(stderr, "Q%d -> %d.\n", reg, v);
    qreg[reg] = v;

    if (qreg[reg] == v)
        return;

    if ((trace & TRACE_LED) && reg == 2)
        fprintf(stderr, "Yellow LED to %d.\n", v);
    if ((trace & TRACE_LED) && reg == 6)
        fprintf(stderr, "Green LED to %d.\n", v);
    /* FIXME: model UART reset */
    if (reg == 4)
        spi_select(v);
}

static uint8_t io_read(int unused, uint16_t addr)
{
    if (trace & TRACE_IO)
        fprintf(stderr, "read %02x\n", addr);
    addr &= 0xFF;
    if (addr >= 0x40 && addr <= 0x4F)
        return fpreg;
    if (addr >= 0xC0 && addr <= 0xC7)
        qreg_read(addr & 7);
    if (addr >= 0xC8 && addr <= 0xCF)
        return uart_read(&uart[0], addr & 7);
    if (trace & TRACE_UNK)
        fprintf(stderr, "Unknown read from port %04X\n", addr);
    return 0xFF;
}

static void io_write(int unused, uint16_t addr, uint8_t val)
{
    if (trace & TRACE_IO)
        fprintf(stderr, "write %02x <- %02x\n", addr & 0xFF, val);
    addr &= 0xFF;
    if (addr >= 0x40 && addr <= 0x4F)
        fpreg_write(val);
    else if (addr >= 0xC0 && addr <= 0xC7)
        qreg_write(addr & 7, val & 1);
    else if (addr >= 0xC8 && addr <= 0xCF)
        uart_write(&uart[0], addr & 7, val);
    else if (addr == 0xFD) {
        printf("trace set to %d\n", val);
        trace = val;
    }
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
    fprintf(stderr, "z80mc: [-f] [-r rompath] [-s sdcardpath] [-d tracemask]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    static struct timespec tc;
    int opt;
    int fd;
    char *rompath = "z80mc.rom";
    char *sdpath = NULL;

    while((opt = getopt(argc, argv, "r:s:d:f")) != -1) {
        switch(opt) {
            case 'r':
                rompath = optarg;
                break;
            case 's':
                sdpath = optarg;
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
    if (read(fd, eprom, 16384) != 16384) {
        fprintf(stderr, "z80mc: ROM image should be 16K.\n");
        exit(EXIT_FAILURE);
    }
    close(fd);

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

    uart_init(&uart[0]);

    /* No real need for interrupt accuracy so just go with the timer. If we
       ever do the UART as timer hack it'll need addressing! */
    tc.tv_sec = 0;
    tc.tv_nsec = 1000000L;

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
	term.c_cc[VSUSP] = 0;
	term.c_cc[VSTOP] = 0;
	tcsetattr(0, TCSADRAIN, &term);
    }

    Z80RESET(&cpu_z80);
    cpu_z80.ioRead = io_read;
    cpu_z80.ioWrite = io_write;
    cpu_z80.memRead = mem_read;
    cpu_z80.memWrite = mem_write;

    qreg[5] = 1;
    /* This is the wrong way to do it but it's easier for the moment. We
       should track how much real time has occurred and try to keep cycle
       matched with that. The scheme here works fine except when the host
       is loaded though */

    /* 4MHz Z80 - 4,000,000 tstates / second, and 1000 inits/sec */
    while (!done) {
        Z80ExecuteTStates(&cpu_z80, 4000);
	/* Do 1ms of I/O and delays */
	if (!fast)
	    nanosleep(&tc, NULL);
	uart_event(uart);
	fpreg |= 0x40;
        Z80INT(&cpu_z80, 0xFF);	/* actually undefined */
    }
    exit(0);
}
