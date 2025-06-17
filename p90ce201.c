/*
 *	Additional logic on the P90CE201
 */

#include <stdint.h>
#include <stdio.h>
#include <m68k.h>
#include "system.h"
#include "p90ce201.h"

static uint16_t syscon1;
static uint16_t syscon2;
static uint8_t scon;
static uint8_t gpp;
static uint8_t gp;
static uint8_t aux;
static uint8_t auxcon;
static uint8_t urir;
static uint8_t uriv;
static uint8_t utir;
static uint8_t utiv;
static uint8_t p90_irq;

struct timer {
	uint16_t count;
	uint16_t rcap;
	uint8_t tcon;
	uint8_t tir;
	uint8_t tiv;
} tm[3];

/* SCON changed so recompute serial interrupts */
static void uart_p90_recalc_ints(void)
{
	unsigned n = check_chario();

	if (n & 1) {
		scon |= 1;
		if (urir & 7) {
			urir |= 8;
			p90_irq |= 1 << (urir & 7);
		}
	}
	if (n & 2) {
		scon |= 2;
		if (utir & 7) {
			utir |= 8;
			p90_irq |= 1 << (utir & 7);
		}
	}
}

static void timer_p90_recalc_ints(struct timer *t)
{
	if (t->tcon & 0x80) {
		/* Look at interrupting */
		if (!(t->tir & 7))
			return;
		t->tir |= 8;
		p90_irq |= 1 << (t->tir & 7);
	}
}

static void p90_recalc_ints(void)
{
	p90_irq = 0;
	uart_p90_recalc_ints();
	timer_p90_recalc_ints(tm);
	timer_p90_recalc_ints(tm + 1);
	timer_p90_recalc_ints(tm + 2);
}

uint8_t p90_read(uint32_t addr)
{
	unsigned r;
	switch(addr & 0xFFFF) {
	case 0x1000:		/* control 1 */
		return syscon1 >> 8;
	case 0x1001:
		return syscon1;
	case 0x1002:		/* control 2* */
		return syscon2 >> 8;
	case 0x1003:
		return syscon2;
	case 0x2021:		/* Sbuf */
		return next_char();
	case 0x2023:		/* Scon */
		scon &= 0xFC;
		r = check_chario();
		/* Emulate the pending input and output bits */
		scon |= r;
/*		if (r & 2)
			scon |= 1;
		if (r & 1)
			scon |= 2; */
		return scon;
	case 0x2025:		/* Uart rx int */
		return urir;
	case 0x2027:
		return uriv;
	case 0x2029:		/* UART tx int */
		return utir;
	case 0x202B:
		return utiv;
	case 0x2030:
		return tm[0].count >> 8;
	case 0x2031:
		return tm[0].count;
	case 0x2032:		/* Timer reload 0 high */
		return tm[0].rcap >> 8;
	case 0x2033:		/* TImer 0 reload low */
		return tm[0].rcap;
	case 0x2035:		/* Timer 0 control */
		return tm[0].tcon;
	case 0x2037:		/* Timer 0 interrupt */
		return tm[0].tir;
	case 0x2039:		/* Timer 0 interrupt */
		return tm[0].tiv;
	case 0x2040:
		return tm[1].count >> 8;
	case 0x2041:
		return tm[1].count;
	case 0x2042:		/* Timer reload 0 high */
		return tm[1].rcap >> 8;
	case 0x2043:		/* TImer 0 reload low */
		return tm[1].rcap;
	case 0x2045:		/* Timer 0 control */
		return tm[1].tcon;
	case 0x2047:		/* Timer 0 interrupt */
		return tm[1].tir;
	case 0x2049:		/* Timer 0 interrupt */
		return tm[1].tiv;
	case 0x2050:
		return tm[2].count >> 8;
	case 0x2051:
		return tm[2].count;
	case 0x2052:		/* Timer reload 0 high */
		return tm[2].rcap >> 8;
	case 0x2053:		/* TImer 0 reload low */
		return tm[2].rcap;
	case 0x2055:		/* Timer 0 control */
		return tm[2].tcon;
	case 0x2057:		/* Timer 0 interrupt */
		return tm[2].tir;
	case 0x2059:		/* Timer 0 interrupt */
		return tm[2].tiv;
	case 0x2071:		/* GP pad */
		return gpp;
	case 0x2073:		/* GP register */
		return gp;
	case 0x2081:		/* Aux */
		/* TODO: call back to read aux lines */
		return aux;
	case 0x2803:		/* Aux control */
		return auxcon;
	default:
		fprintf(stderr, "Unemulated P90 read %06X", addr);
	}
	return 0xFF;
}

void p90_write(uint32_t addr, uint8_t val)
{
	switch(addr & 0xFFFF) {
	case 0x1000:		/* control 1 */
		syscon1 &= 0xFF; /* Wait states etc */
		syscon1 |=  val << 8;
		break;
	case 0x1001:
		syscon1 &= 0xFF00;
		syscon1 |=  val ;
		break;
	case 0x1002:		/* control 2* */
		syscon2 &= 0xFF;
		syscon2 |=  val << 8;
		break;
	case 0x1003:
		syscon2 &= 0xFF00;
		syscon2 |=  val ;
		break;
	case 0x2021:		/* Sbuf */
		putchar(val);	/* TODO : IRQ */
		fflush(stdout);
		break;		/* Minimal emulation will do fine for now */
	case 0x2023:		/* Scon */
		scon = val;
		break;
	case 0x2025:		/* Uart rx int */
		urir = val;
		break;
	case 0x2027:
		uriv = val;
		break;
	case 0x2029:		/* UART tx int */
		utir = val;
		break;
	case 0x202B:
		utiv = val;
		break;
	case 0x2032:		/* Timer reload 0 high */
		tm[0].rcap &= 0xFF;
		tm[0].rcap |= val << 8;
		break;
	case 0x2033:		/* TImer 0 reload low */
		tm[0].rcap &= 0xFF00;
		tm[0].rcap |= val;
		break;
	case 0x2035:		/* Timer 0 control */
		tm[0].tcon = val;
		break;
	case 0x2037:		/* Timer 0 interrupt */
		tm[0].tir = val;
		break;
	case 0x2039:
		tm[0].tiv = val;
		break;
	case 0x2042:		/* Timer reload 0 high */
		tm[1].rcap &= 0xFF;
		tm[1].rcap |= val << 8;
		break;
	case 0x2043:		/* TImer 0 reload low */
		tm[1].rcap &= 0xFF00;
		tm[1].rcap |= val;
		break;
	case 0x2045:		/* Timer 0 control */
		tm[1].tcon = val;
		break;
	case 0x2047:		/* Timer 0 interrupt */
		tm[1].tir = val;
		break;
	case 0x2049:
		tm[1].tiv = val;
	case 0x2052:		/* Timer reload 0 high */
		tm[2].rcap &= 0xFF;
		tm[2].rcap |= val << 8;
		break;
	case 0x2053:		/* TImer 0 reload low */
		tm[2].rcap &= 0xFF00;
		tm[2].rcap |= val;
		break;
	case 0x2055:		/* Timer 0 control */
		tm[2].tcon = val;
		break;
	case 0x2057:		/* Timer 0 interrupt */
		tm[2].tir = val;
		break;
	case 0x2059:
		tm[2].tiv = val;
	case 0x2081:		/* Aux */
		aux = val;
		/* Notify the system the lines have changed */
		p90_set_aux(auxcon, aux);
		break;
	case 0x2083:		/* Aux control */
		auxcon = val;
		/* Notify the system the lines have changed */
		p90_set_aux(auxcon, aux);
		break;
	default:
		fprintf(stderr, "Unemulated P90 write %06X,%02X", addr, val);
	}
	p90_recalc_ints();
}

/* Do time based emulation */

static void run_timer(struct timer *t, unsigned ticks)
{
	unsigned n;
	/* Counting external events (not emulated) */
	if (t->tcon & 0x03)
		return;
	/* Disabled */
	if (!(t->tcon & 0x04))
		return;
	/* TODO: event count mode */
	if (t->count + ticks > 0xFFFF) {
		n = 0x10000 - t->count;	/* Ticks to first overflow */;
		ticks -= n;	/* To complete first overflow */
		n = 0x10000 - t->rcap;	/* Ticks per overflow */
		ticks %= n;	/* Remaining overflows */
		/* Count the leftovers up */
		t->count = 0xFFFF - n;
		t->tcon |= 0x80;	/* Overflow flag on */
		/* Look at interrupting */
		if (!(t->tir & 7))
			return;
		t->tir |= 8;
		p90_irq |= 1 << (t->tir & 7);
	}
	t->count += ticks;
}

void p90_cycles(unsigned ticks)
{
	unsigned bpclk;

	bpclk = ticks;

	switch(syscon2 & 0x300) {
	case 0x000:
		bpclk /= 6;
		break;
	case 0x100:
		bpclk /= 5;
		break;
	case 0x200:
		bpclk /= 4;
		break;
	case 0x300:
		bpclk /= 3;
	}
	/* SYSCON2 timer bits we emulate */
	/* 1: prescaler for t1 1 = extra /16 */
	/* 4: prescaler for t0 1 = extra /16 */
	/* 10: set causes t2 to run at bpclk/4 */
	/* 8-9 -set bpclock divider to 3 2.5 2 or 1.5 */
	run_timer(tm, (syscon2 & 0x10) ? ticks / 32 : ticks / 2);
	run_timer(tm + 1, (syscon2 & 0x02) ? ticks / 32 : ticks / 2);
	run_timer(tm + 2, (syscon2 & 0x400) ? bpclk : ticks / 2);

	p90_recalc_ints();
}

int p90_autovector(unsigned n)
{
	if ((urir & 7) == n) {
		urir &= ~8;
		if (urir & 0x20)
			return uriv;
		else
			return M68K_INT_ACK_AUTOVECTOR;
	}
	if ((utir & 7) == n) {
		utir &= ~8;
		if (utir & 0x20)
			return utiv;
		else
			return M68K_INT_ACK_AUTOVECTOR;
	}
	if ((tm[0].tir & 7) == n) {
		tm[0].tir &= ~8;
		if (tm[0].tir & 0x20)
			return tm[0].tiv;
		else
			return M68K_INT_ACK_AUTOVECTOR;
	}
	if ((tm[1].tir & 7) == n) {
		tm[1].tir &= ~8;
		if (tm[1].tir & 0x20)
			return tm[1].tiv;
		else
			return M68K_INT_ACK_AUTOVECTOR;
	}
	if ((tm[2].tir & 7) == n) {
		tm[2].tir &= ~8;
		if (tm[2].tir & 0x20)
			return tm[2].tiv;
		else
			return M68K_INT_ACK_AUTOVECTOR;
	}
	/* Wasn't use - could be someone else, up to caller */
	return M68K_INT_ACK_SPURIOUS;
}

/* Return the highest interrupt */
unsigned p90_interrupts(void)
{
	unsigned i;
	for (i = 7; i > 0; i--) {
		if (p90_irq & (1 << i))
			return i;
	}
	return 0;
}
