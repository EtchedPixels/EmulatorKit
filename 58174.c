#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "system.h"
#include "58174.h"

/*
 *	MM58174 RTC. Nybble wide interface
 */

struct mm58174 {
	uint8_t interrupt;

	unsigned int irqpending;
	unsigned int trace;

	unsigned int count;
};

static void rtc_reload(struct mm58174 *rtc)
{
	/* Not clear what undocumented cases do */
	switch(rtc->interrupt & 0x07) {
	case 0x04:
		rtc->count = 6000;
		break;
	case 0x02:
		rtc->count = 500;
		break;
	case 0x01:
		rtc->count = 50;		/* 0.5 seconds */
		break;
	}
}

uint8_t mm58174_read(struct mm58174 *rtc, uint8_t reg)
{
	time_t t;
	struct tm *tm;
	static unsigned fake_tenths;

	t = time(NULL);
	tm = gmtime(&t);
	if (tm == NULL)
		return 0xFF;

	switch(reg) {
	case 0:
		return 0xFF;	/* Test port */
	case 1:
		/* Some stuff probes by watching this so fake a tick */
		if (fake_tenths == 10)
			fake_tenths = 0;
		return fake_tenths++;
	case 2:
		return tm->tm_sec % 10;
	case 3:
		return tm->tm_sec / 10;
	case 4:
		return tm->tm_min % 10;
	case 5:
		return tm->tm_min / 10;
	case 6:
		return tm->tm_hour % 10;
	case 7:
		return tm->tm_hour / 10;
	case 8:
		return tm->tm_mday % 10;
	case 9:
		return tm->tm_mday / 10;
	case 10:
		return tm->tm_wday + 1;
	case 11:
		return (tm->tm_mon + 1) % 10;
	case 12:
		return (tm->tm_mon + 1) / 10;
	case 13:	/* No year capability */
		return 0xFF;
	case 14:
		return 0;
	case 15:
		/* Really you need to read it 3 times ?? */
		rtc->irqpending = 0;
		if (rtc->interrupt & 8)
			rtc_reload(rtc);
		return rtc->interrupt;
	}
	return 0xFF;
}


void mm58174_write(struct mm58174 *rtc, uint8_t reg, uint8_t val)
{
	/* We don't support time setting */
	if (reg < 15)
		return;
	if (reg == 15) {
		rtc->interrupt = val & 15;
		rtc->irqpending = 0;
		rtc_reload(rtc);
	}
}

void mm58174_reset(struct mm58174 *rtc)
{
	memset(rtc, 0, sizeof(struct mm58174));
}

struct mm58174 *mm58174_create(void)
{
	struct mm58174 *rtc = malloc(sizeof(struct mm58174));
	if (rtc == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	mm58174_reset(rtc);
	return rtc;
}

void mm58174_free(struct mm58174 *rtc)
{
	free(rtc);
}

void mm58174_trace(struct mm58174 *rtc, unsigned int onoff)
{
	rtc->trace = onoff;
}

unsigned int mm58174_irqpending(struct mm58174 *rtc)
{
	return rtc->irqpending;
}

/* Call at 100Hz */
void mm58174_tick(struct mm58174 *rtc)
{
	if ((rtc->interrupt & 7) == 0)
		return;
	if (rtc->count) {
		rtc->count--;
		if (rtc->count == 0)
			rtc->irqpending = 1;
	}
}
