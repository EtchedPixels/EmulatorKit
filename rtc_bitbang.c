#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "system.h"
#include "rtc_bitbang.h"


/* Real time clock state machine and related state.

   Give the host time and don't emulate time setting except for
   the 24/12 hour setting.
   
 */

struct rtc {
	uint8_t w;
	uint8_t st;
	uint16_t r;
	uint8_t cnt;
	uint8_t state;
	uint8_t reg;
	uint8_t ram[32];
	uint8_t wp;
	uint8_t clock24;
	uint8_t bp;
	uint8_t bc;
	struct tm *tm;
	int trace;
};

uint8_t rtc_read(struct rtc *rtc)
{
	if (rtc->st & 0x30)
		return (rtc->r & 0x01) ? 1 : 0;
	return 0xFF;
}

static uint16_t rtc_regread(struct rtc *rtc, uint8_t reg)
{
	uint8_t val, v;

	switch (reg) {
	case 0:
		val = (rtc->tm->tm_sec % 10) + ((rtc->tm->tm_sec / 10) << 4);
		break;
	case 1:
		val = (rtc->tm->tm_min % 10) + ((rtc->tm->tm_min / 10) << 4);
		break;
	case 2:
		v = rtc->tm->tm_hour;
		if (!rtc->clock24) {
			v %= 12;
			v++;
		}
		val = (v % 10) + ((v / 10) << 4);
		if (!rtc->clock24) {
			if (rtc->tm->tm_hour > 11)
				val |= 0x20;
			val |= 0x80;
		}
		break;
	case 3:
		val = (rtc->tm->tm_mday % 10) + ((rtc->tm->tm_mday / 10) << 4);
		break;
	case 4:
		val = ((rtc->tm->tm_mon + 1) % 10) + (((rtc->tm->tm_mon + 1) / 10) << 4);
		break;
	case 5:
		val = rtc->tm->tm_wday + 1;
		break;
	case 6:
		v = rtc->tm->tm_year % 100;
		val = (v % 10) + ((v / 10) << 4);
		break;
	case 7:
		val = rtc->wp ? 0x80 : 0x00;
		break;
	case 8:
		val = 0;
		break;
	default:
		val = 0xFF;
		/* Check */
		break;
	}
	if (rtc->trace)
		fprintf(stderr, "RTCreg %d = %02X\n", reg, val);
	return val;
}

static void rtcop(struct rtc *rtc)
{
	if (rtc->trace)
		fprintf(stderr, "rtcbyte %02X\n", rtc->w);
	/* The emulated task asked us to write a byte, and has now provided
	   the data byte to go with it */
	if (rtc->state == 2) {
		if ((!rtc->wp) || (rtc->reg == 7)) {
			if (rtc->trace)
				fprintf(stderr, "RTC write %d as %d\n", rtc->reg, rtc->w);
			/* We did a real write! */
			/* Not yet tackled burst mode */
			if (rtc->reg != 0x3F && (rtc->reg & 0x20))	/* NVRAM */
				rtc->ram[rtc->reg & 0x1F] = rtc->w;
			else if (rtc->reg == 2)
				rtc->clock24 = rtc->w & 0x80;
			else if (rtc->reg == 7)
				rtc->wp = rtc->w & 0x80;
		}
		/* For now don't emulate writes to the time */
		rtc->state = 0;
		return;
	}
	/* Check for broken requests */
	if (!(rtc->w & 0x80)) {
		if (rtc->trace)
			fprintf(stderr, "rtc->w makes no sense %d\n", rtc->w);
		rtc->state = 0;
		rtc->r = 0x1FF;
		return;
	}
	/* Clock burst ? : for now we only emulate time burst */
	if (rtc->w == 0xBF) {
		rtc->state = 3;
		rtc->bp = 0;
		rtc->bc = 0;
		rtc->r = rtc_regread(rtc, rtc->bp++) << 1;
		if (rtc->trace)
			fprintf(stderr, "rtc command BF: burst clock read.\n");
		return;
	}
	/* A write request */
	if (!(rtc->w & 0x01)) {
		if (rtc->trace)
			fprintf(stderr, "rtc write request, waiting byte 2.\n");
		rtc->state = 2;
		rtc->reg = (rtc->w >> 1) & 0x3F;
		rtc->r = 0x1FF;
		return;
	}
	/* A read request */
	rtc->state = 1;
	if (rtc->w & 0x40) {
		/* RAM */
		if (rtc->w != 0xFE)
			rtc->r = rtc->ram[(rtc->w >> 1) & 0x1F] << 1;
		if (rtc->trace)
			fprintf(stderr, "RTC RAM read %d, ready to clock out %d.\n", (rtc->w >> 1) & 0xFF, rtc->r);
		return;
	}
	/* Register read */
	rtc->r = rtc_regread(rtc, (rtc->w >> 1) & 0x1F) << 1;
	if (rtc->trace)
		fprintf(stderr, "RTC read of time register %d is %d\n", (rtc->w >> 1) & 0x1F, rtc->r);
}

void rtc_write(struct rtc *rtc, uint8_t val)
{
	uint8_t changed = val ^ rtc->st;
	uint8_t is_read;
	/* Direction */
	if ((rtc->trace) && (changed & 0x20))
		fprintf(stderr, "RTC direction now %s.\n", (val & 0x20) ? "read" : "write");
	is_read = val & 0x20;
	/* Clock */
	if (changed & 0x40) {
		/* The rising edge samples, the falling edge clocks receive */
		if (rtc->trace)
			fprintf(stderr, "RTC clock low.\n");
		if (!(val & 0x40)) {
			rtc->r >>= 1;
			/* Burst read of time */
			rtc->bc++;
			if (rtc->bc == 8 && rtc->bp) {
				rtc->r = rtc_regread(rtc, rtc->bp++) << 1;
				rtc->bc = 0;
			}
			if (rtc->trace)
				fprintf(stderr, "rtc->r now %02X\n", rtc->r);
		} else {
			if (rtc->trace)
				fprintf(stderr, "RTC clock high.\n");
			rtc->w >>= 1;
			if ((val & 0x30) == 0x10)
				rtc->w |= val & 0x80;
			else
				rtc->w |= 0xFF;
			rtc->cnt++;
			if (rtc->trace)
				fprintf(stderr, "rtc->w now %02x (%d)\n", rtc->w, rtc->cnt);
			if (!(rtc->cnt % 8) && !is_read)
				rtcop(rtc);
		}
	}
	/* CE */
	if (changed & 0x10) {
		if (rtc->st & 0x10) {
			if (rtc->trace)
				fprintf(stderr, "RTC CE dropped.\n");
			rtc->cnt = 0;
			rtc->r = 0;
			rtc->w = 0;
			rtc->state = 0;
		} else {
			/* Latch imaginary registers on rising edge */
			time_t t = time(NULL);
			rtc->tm = localtime(&t);
			if (rtc->trace)
				fprintf(stderr, "RTC CE raised and latched time.\n");
		}
	}
	rtc->st = val;
}

void rtc_reset(struct rtc *rtc)
{
	memset(rtc, 0, sizeof(struct rtc));
	rtc->wp = 0x80;
	rtc->clock24 = 1;
}

struct rtc *rtc_create(void)
{
	struct rtc *rtc = malloc(sizeof(struct rtc));
	if (rtc == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	rtc_reset(rtc);
	return rtc;
}

void rtc_free(struct rtc *rtc)
{
	free(rtc);
}

void rtc_trace(struct rtc *rtc, int onoff)
{
	rtc->trace = onoff;
}
