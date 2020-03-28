/*
 *	Platform features
 *
 *	68000 processor card for RC2014 with 20bit addressing mapped
 *	000000-00FFFF	ROM
 *	010000-01FFFF	IO Window (hides ROM in supervisor mode only)
 *	020000-07FFFF	ROM
 *	080000-0FFFFF	RAM
 *
 *	Motorola 68B50
 *	IDE at 0x10-0x17 no high or control access
 *	Memory banking Zeta style 16K page at 0x78-0x7B (enable at 0x7C)
 *	Flat 1MB address space with the low 512K as ROM
 *	RTC at 0x0C
 *	16550A at 0xC0
 *
 *	TODO: QUART or similar and timer emulation
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
#include <m68k.h>
#include "ide.h"
#include "w5100.h"

static uint8_t ramrom[1024 * 1024];	/* ROM low RAM high */

static uint8_t rtc = 0;
static uint8_t fast = 0;
static uint8_t wiznet = 0;

static uint16_t tstate_steps = 200;

/* Who is pulling on the interrupt line */

#define IRQ_ACIA	1
#define IRQ_16550A	2

static nic_w5100_t *wiz;

static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_IRQ	4
#define TRACE_UNK	8
#define TRACE_RTC	16
#define TRACE_CPU	32
#define TRACE_ACIA	64
#define TRACE_UART	128

static int trace = 0;
static int irq_mask;

static void add_irq(int n)
{
	if (!(irq_mask & (1 << n)) && (trace & TRACE_IRQ))
		fprintf(stderr, "[IRQ]\n");
	irq_mask |= (1 << n);
	m68k_set_irq(2);
}

static void remove_irq(int n)
{
	if (irq_mask & (1 << n)) {
		irq_mask &= ~(1 << n);
		if (!irq_mask) {
			m68k_set_irq(0);
			if (trace & TRACE_IRQ)
				fprintf(stderr, "[IRQ cleared]\n");
		}
	}
}

int cpu_irq_ack(int level)
{
	return M68K_INT_ACK_AUTOVECTOR;
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
static uint8_t acia_input;
static uint8_t acia_inint = 0;
static uint8_t acia_narrow;

static void acia_irq_compute(void)
{
	if (!acia_inint && acia_config && acia_status & 0x80) {
		if (trace & TRACE_ACIA)
			fprintf(stderr, "ACIA interrupt.\n");
		acia_inint = 1;
		add_irq(IRQ_ACIA);
	}
	if (acia_inint) {
		remove_irq(IRQ_ACIA);
		acia_inint = 0;
	}
}

static void acia_receive(void)
{
	uint8_t old_status = acia_status;
	acia_status = old_status & 0x02;
	if (old_status & 1)
		acia_status |= 0x20;
	acia_char = next_char();
	if (trace & TRACE_ACIA)
		fprintf(stderr, "ACIA rx.\n");
	acia_status |= 0x81;	/* IRQ, and rx data full */
}

static void acia_transmit(void)
{
	if (!(acia_status & 2)) {
		if (trace & TRACE_ACIA)
			fprintf(stderr, "ACIA tx is clear.\n");
		acia_status |= 0x82;	/* IRQ, and tx data empty */
	}
}

static void acia_timer(void)
{
	int s = check_chario();
	if ((s & 1) && acia_input)
		acia_receive();
	if (s & 2)
		acia_transmit();
	if (s)
		acia_irq_compute();
}

static uint8_t acia_read(uint8_t addr)
{
	if (trace & TRACE_ACIA)
		fprintf(stderr, "acia_read %d ", addr);
	switch (addr) {
	case 0:
		/* bits 7: irq pending, 6 parity error, 5 rx over
		 * 4 framing error, 3 cts, 2 dcd, 1 tx empty, 0 rx full.
		 * Bits are set on char arrival and cleared on next not by
		 * user
		 */
		acia_status &= ~0x80;
		acia_irq_compute();
		if (trace & TRACE_ACIA)
			fprintf(stderr, "acia_status %d\n", acia_status);
		return acia_status;
	case 1:
		acia_status &= ~0x81;	/* No IRQ, rx empty */
		acia_irq_compute();
		if (trace & TRACE_ACIA)
			fprintf(stderr, "acia_char %d\n", acia_char);
		return acia_char;
	default:
		fprintf(stderr, "acia: bad addr.\n");
		exit(1);
	}
}

static void acia_write(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_ACIA)
		fprintf(stderr, "acia_write %d %d\n", addr, val);
	switch (addr) {
	case 0:
		/* bit 7 enables interrupts, bits 5-6 are tx control
		   bits 2-4 select the word size and 0-1 counter divider
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
        remove_irq(IRQ_16550A);
        return;
    }
    /* Ok so we have an event, do we need to waggle the line */
    if (uptr->irqline)
        return;
    uptr->irqline = uptr->irq;
    add_irq(IRQ_16550A);
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
        /* receive buffer */
        if (uptr == &uart && uptr->dlab == 0) {
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

/* Real time clock state machine and related state.

   Give the host time and don't emulate time setting except for
   the 24/12 hour setting.
   
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

	switch (reg) {
	case 0:
		val = (rtc_tm->tm_sec % 10) + ((rtc_tm->tm_sec / 10) << 4);
		break;
	case 1:
		val = (rtc_tm->tm_min % 10) + ((rtc_tm->tm_min / 10) << 4);
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
		val = (rtc_tm->tm_mday % 10) + ((rtc_tm->tm_mday / 10) << 4);
		break;
	case 4:
		val = ((rtc_tm->tm_mon + 1) % 10) + (((rtc_tm->tm_mon + 1) / 10) << 4);
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
				rtcram[rtcreg & 0x1F] = rtcw;
			else if (rtcreg == 2)
				rtc24 = rtcw & 0x80;
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
			fprintf(stderr, "RTC RAM read %d, ready to clock out %d.\n", (rtcw >> 1) & 0xFF, rtcr);
		return;
	}
	/* Register read */
	rtcr = rtcregread((rtcw >> 1) & 0x1F) << 1;
	if (trace & TRACE_RTC)
		fprintf(stderr, "RTC read of time register %d is %d\n", (rtcw >> 1) & 0x1F, rtcr);
}

static void rtc_write(uint8_t val)
{
	uint8_t changed = val ^ rtcst;
	uint8_t is_read;
	/* Direction */
	if ((trace & TRACE_RTC) && (changed & 0x20))
		fprintf(stderr, "RTC direction now %s.\n", (val & 0x20) ? "read" : "write");
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
			time_t t = time(NULL);
			rtc_tm = localtime(&t);
			if (trace & TRACE_RTC)
				fprintf(stderr, "RTC CE raised and latched time.\n");
		}
	}
	rtcst = val;
}

uint8_t mmio_read_68000(uint16_t addr)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "read %02x\n", addr);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		return acia_read(addr & 1);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		return acia_read(addr & 1);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read();
	if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		return uart_read(&uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

void mmio_write_68000(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %02x <- %02x\n", addr, val);
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		acia_write(addr & 1, val);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(addr & 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr == 0x0C && rtc)
		rtc_write(val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart_16550a)
		uart_write(&uart, addr & 0x0F, val);
	else if (addr == 0x00) {
		printf("trace set to %d\n", val);
		trace = val;
#if 0		
		if (trace & TRACE_CPU)
		else
#endif		
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

unsigned int cpu_read_byte_dasm(unsigned int addr)
{
	addr &= 0xFFFFF;
	return ramrom[addr];
}

unsigned int cpu_read_word_dasm(unsigned int addr)
{
	return (cpu_read_byte_dasm(addr) << 8) | cpu_read_byte_dasm(addr + 1);
}

unsigned int cpu_read_long_dasm(unsigned int addr)
{
	return (cpu_read_word_dasm(addr) << 16) | cpu_read_word_dasm(addr + 2);
}

static unsigned int do_cpu_read_byte(unsigned int addr)
{
	addr &= 0xFFFFF;
	if ((addr & 0xF0000) == 0x10000)
		return mmio_read_68000(addr);
	return ramrom[addr];
}

unsigned int cpu_read_byte(unsigned int addr)
{
	unsigned int r;
	
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %06X = ", addr & 0xFFFFF);
	r = do_cpu_read_byte(addr);
	if (trace & TRACE_MEM)
		fprintf(stderr, "%02X\n", r);
	return r;
}

unsigned int cpu_read_word(unsigned int addr)
{
	return (cpu_read_byte(addr) << 8) | cpu_read_byte(addr + 1);
}

unsigned int cpu_read_long(unsigned int addr)
{
	return (cpu_read_word(addr) << 16) | cpu_read_word(addr + 2);
}

void cpu_write_byte(unsigned int addr, unsigned int value)
{
	addr &= 0xFFFFF;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %06X = %02X\n",
			addr & 0xFFFFF, value);
	if ((addr & 0xF0000) == 0x10000)
		mmio_write_68000(addr, value);
	else if (addr >= 0x80000)
		ramrom[addr] = value;
}

void cpu_write_word(unsigned int addr, unsigned int value)
{
	cpu_write_byte(addr, value >> 8);
	cpu_write_byte(addr + 1, value);
}

void cpu_write_long(unsigned int addr, unsigned int value)
{
	cpu_write_word(addr, value >> 16);
	cpu_write_word(addr + 2, value);
}

void cpu_write_pd(unsigned int addr, unsigned int value)
{
	cpu_write_word(addr + 2, value);
	cpu_write_word(addr, value >> 16);
}

void cpu_instr_callback(void)
{
	if (trace & TRACE_CPU) {
		char buf[128];
		unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
		m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
		fprintf(stderr, ">%06X %s\n", pc, buf);
	}
}

void cpu_pulse_reset(void)
{
}

void cpu_set_fc(int fc)
{
}

void system_process(void)
{
	static int n = 0;
	static struct timespec tc;
	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;
	if (acia)
		acia_timer();
	if (uart_16550a)
		uart_event(&uart);
	if (n++ == 100) {
		n = 0;
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
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
	fprintf(stderr, "rc2014: [-1] [-A] [-a] [-c] [-f] [-R] [-r rompath] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int opt;
	int fd;
	char *rompath = "rc2014-68000.rom";
	char *idepath;

	while ((opt = getopt(argc, argv, "1Aad:fi:r:Rw")) != -1) {
		switch (opt) {
		case '1':
			uart_16550a = 1;
			acia = 0;
			break;
		case 'a':
			acia = 1;
			acia_input = 1;
			acia_narrow = 0;
			uart_16550a = 0;
			break;
		case 'A':
			acia = 1;
			acia_narrow = 1;
			acia_input = 1;
			uart_16550a = 0;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			ide = 1;
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

	if (acia == 0 && uart_16550a == 0) {
		fprintf(stderr, "rc2014: no UART selected, defaulting to 16550A\n");
		uart_16550a = 1;
	}

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ramrom, 524288) != 524288) {
		fprintf(stderr, "rc2014: ROM image should be 512K.\n");
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

	if (uart_16550a)
		uart_init(&uart);

	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}

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
	m68k_init();
	/* Really should be 68008 */
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	while(1) {
		m68k_execute(tstate_steps);	/* 4MHz roughly right for 8MHz 68008 */
		system_process();
	}
}
