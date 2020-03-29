/*
 *	Platform features
 *
 *	80C188 at 12MHz
 *	IDE at 0x10-0x17 no high or control access (mirrored at 0x90-97)
 *	First 512K RAM Second 512K ROM
 *	RTC at 0x0C
 *	16550A at 0xC0
 *	WizNET ethernet
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
#include <errno.h>
#include "80x86/e8086.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];

static uint8_t rtc;
static uint8_t fast = 0;
static uint8_t wiznet = 0;

e8086_t *cpu;
struct ppide *ppide;
struct rtc *rtcdev;
static nic_w5100_t *wiz;

static uint16_t tstate_steps = 369;	/* RC2014 speed (7.4MHz)  for now */

static volatile int done;

/* Who is pulling on the interrupt line */
static uint8_t live_irq;

#define IRQ_SIOA	1
#define IRQ_SIOB	2
#define IRQ_CTC		3
#define IRQ_ACIA	4
#define IRQ_16550A	5


#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_PPIDE	16
#define TRACE_512	32
#define TRACE_RTC	64
#define TRACE_ACIA	128
#define TRACE_CTC	256
#define TRACE_CPU	512
#define TRACE_IRQ	1024
#define TRACE_UART	2048

static int trace = 0;

uint8_t i808x_do_read(uint32_t addr)
{
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %06X = %02X\n", addr, ramrom[addr]);
	return ramrom[addr];
}

uint8_t i808x_debug_read(uint16_t addr)
{
	return i808x_do_read(addr);	/* No side effects */
}

uint8_t i808x_read8(void *mem, unsigned long addr)
{
	return i808x_do_read(addr);
}

uint16_t i808x_read16(void *mem, unsigned long addr)
{
	uint16_t r = i808x_do_read(addr);
	r |= i808x_do_read(addr + 1);
	return r;
}


void do_i808x_write(uint32_t addr, uint8_t val)
{
	if (addr >= 512 * 1024) {
		fprintf(stderr, "W: %06X: write to ROM of %02X.\n", addr, val);
		return;
	}
	ramrom[addr] = val;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W: %06X = %02X\n", addr, val);
}

void i808x_write8(void *mem, unsigned long addr, uint8_t val)
{
	do_i808x_write(addr, val);
}

void i808x_write16(void *mem, unsigned long addr, uint16_t val)
{
	do_i808x_write(addr, val);
	do_i808x_write(addr + 1, val >> 8);
}


int check_chario(void)
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

/* FIXME: we really need to do the right 80C188 vectoring */
void recalc_interrupts(void)
{
	if (live_irq)
		e86_irq(cpu, 0x20);
	else
		e86_irq(cpu, 0);
}

static void int_set(int src)
{
	live_irq |= (1 << src);
	recalc_interrupts();
}

static void int_clear(int src)
{
	live_irq &= ~(1 << src);
	recalc_interrupts();
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

static struct uart16x50 uart;
static unsigned int uart_16550a;

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
        int_clear(IRQ_16550A);
        return;
    }
    /* Ok so we have an event, do we need to waggle the line */
    if (uptr->irqline)
        return;
    uptr->irqline = uptr->irq;
    int_set(IRQ_16550A);
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
            if (uptr == &uart) {
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
        show_settings(uptr);
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
	if (uptr->dlab)
		return uptr->ls;
        /* receive buffer */
        if (uptr == &uart && uptr->dlab == 0) {
            uart_clear_interrupt(uptr, RXDA);
            return next_char();
        }
        break;
    case 1:
	if (uptr->dlab)
		return uptr->ms;
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
        /* Reading the LSR causes these bits to clear */
        r = uptr->lsr;
        uptr->lsr &= 0xF0;
        return r;
    case 6:
        /* msr */
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

/* Our port handling is 8bit wide because our bus is 8bit wide without any
   wide transaction indications */

static uint8_t i808x_inport(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %04x\n", addr);
	addr &= 0xFF;
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		return ppide_read(ppide, addr & 3);
	if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read(rtcdev);
	else if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		return uart_read(&uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

static void i808x_outport(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %04x <- %02x\n", addr, val);
	addr &= 0xFF;
	if ((addr >= 0x10 && addr <= 0x17) && ide == 1)
		my_ide_write(addr & 7, val);
	else if ((addr >= 0x90 && addr <= 0x97) && ide == 1)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x20 && addr <= 0x23 && ide == 2)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr == 0x0C && rtc)
		rtc_write(rtcdev, val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		uart_write(&uart, addr & 0x0F, val);
	else if (addr == 0xFD) {
		printf("trace set to %d\n", val);
		trace = val;
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

static uint8_t i808x_in8(void *mem, unsigned long addr)
{
	return i808x_inport(addr & 0xFFFF);
}

static uint16_t i808x_in16(void *mem, unsigned long addr)
{
	uint16_t r = i808x_inport(addr);
	r |= i808x_inport(addr + 1) << 8;
	return r;
}

static void i808x_out8(void *mem, unsigned long addr, uint8_t val)
{
	i808x_outport(addr, val);
}

static void i808x_out16(void *mem, unsigned long addr, uint16_t val)
{
	i808x_outport(addr, val);
	i808x_outport(addr + 1, val >> 8);
}

static void poll_irq_event(void)
{
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "rc2014-80c188: [-1] [-f] [-R] [-r rompath] [-e rombank] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "rc2014-808x.rom";
	char *idepath;

	uart_16550a = 1;

	while ((opt = getopt(argc, argv, "d:fi:I:r:Rw")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
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
			rtc = 1;
			break;
		case 'w':
			wiznet = 1;
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
	if (read(fd, ramrom + 512 * 1024, 512 * 1024) < 512 * 1024) {
		fprintf(stderr, "rc2014: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide) {
		/* FIXME: clean up when classic cf becomes a driver */
		if (ide == 1) {
			ide0 = ide_allocate("cf");
			if (ide0) {
				int ide_fd = open(idepath, O_RDWR);
				if (ide_fd == -1) {
					perror(idepath);
					ide = 0;
				}
				else if (ide_attach(ide0, 0, ide_fd) == 0) {
					ide = 1;
					ide_reset_begin(ide0);
				}
			} else
				ide = 0;
		} else {
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
	}

	if (uart_16550a)
		uart_init(&uart);

	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

	if (rtc) {
		rtcdev = rtc_create();
		rtc_reset(rtcdev);
		if (trace & TRACE_RTC)
			rtc_trace(rtcdev, 1);
	}

	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;

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

	cpu = e86_new();
	if (cpu == NULL) {
		fprintf(stderr, "rc2014: failed to create CPU instance.\n");
		exit(1);
	}
	e86_init(cpu);
	/* FIXME: no 80C188 emulation so need to tweak emulator later */
	e86_set_80186(cpu);
	/* Bus interfaces */
	e86_set_mem(cpu, NULL, i808x_read8, i808x_write8, i808x_read16, i808x_write16);
	e86_set_prt(cpu, NULL, i808x_in8, i808x_out8, i808x_in16, i808x_out16);
	e86_set_ram(cpu, ramrom, sizeof(ramrom));

	/* Reset the CPU */	
	e86_reset(cpu);

	if (trace & TRACE_CPU) {
//		i808x_log = stderr;
	}

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	/* We run 7372000 t-states per second */
	/* We run 369 cycles per I/O check, do that 100 times then poll the
	   slow stuff and nap for 5ms. */
	while (!done) {
		int i;
		/* 36400 T states for base RC2014 - varies for others */
		for (i = 0; i < 100; i++) {
			e86_clock(cpu, tstate_steps);
			if (uart_16550a)
				uart_event(&uart);
		}
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
