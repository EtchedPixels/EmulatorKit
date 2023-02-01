#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "ds3234.h"

struct ds3234 {
	uint8_t ram[256];
	uint8_t clock24;
	uint8_t ar;
	uint8_t srptr;
	unsigned state;
	struct tm *tm;
	int trace;
};

/* We only emulate the clock part for now */
static uint8_t ds3234_regread(struct ds3234 *rtc, uint8_t reg)
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
		val = rtc->tm->tm_wday + 1;
		break;
	case 4:
		val = (rtc->tm->tm_mday % 10) + ((rtc->tm->tm_mday / 10) << 4);
		break;
	case 5:
		val = ((rtc->tm->tm_mon + 1) % 10) + (((rtc->tm->tm_mon + 1) / 10) << 4);
		break;
	case 6:
		v = rtc->tm->tm_year % 100;
		val = (v % 10) + ((v / 10) << 4);
		break;
	case 24:
		val = rtc->srptr;
		break;
	case 25:
		val = rtc->ram[rtc->srptr++];
		rtc->srptr &= 0xFF;
		break;
	default:
		/* We don't emulate the alarms and stuff */
		val = 0;
		break;
	}
	if (rtc->trace)
		fprintf(stderr, "RTCreg %d = %02X\n", reg, val);
	return val;
}

static void ds3234_regwrite(struct ds3234 *rtc, uint8_t reg, uint8_t val)
{
	switch(reg) {
	case 2:
		rtc->clock24 = val & 0x80;
		break;
	case 24:
		rtc->srptr = val;
		break;
	case 25:
		rtc->ram[rtc->srptr++] = val;
		rtc->srptr &= 0xFF;
		break;
	}
	if (rtc->trace)
		fprintf(stderr, "RTC write time %d as %d\n", reg, val);
}

uint8_t ds3234_spi_rxtx(struct ds3234 *rtc, uint8_t val)
{
	if (rtc->state == 0) {
		rtc->ar = val;
		rtc->state = 1;
		return 0xFF;
	}
	if (rtc->ar & 0x80) {
		ds3234_regwrite(rtc, rtc->ar & 0x7F, val);
		return 0xFF;
	} else
		return ds3234_regread(rtc, rtc->ar);
}

void ds3234_spi_cs(struct ds3234 *rtc, unsigned cs)
{
	if (cs)
		rtc->state = 0;
}

void ds3234_reset(struct ds3234 *rtc)
{
	memset(rtc, 0, sizeof(struct ds3234));
}

struct ds3234 *ds3234_create(void)
{
	struct ds3234 *rtc = malloc(sizeof(struct ds3234));
	if (rtc == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	ds3234_reset(rtc);
	return rtc;
}

void ds3234_free(struct ds3234 *rtc)
{
	free(rtc);
}

void ds3234_trace(struct ds3234 *rtc, int onoff)
{
	rtc->trace = onoff;
}

void ds3234_save(struct ds3234 *rtc, const char *path)
{
	int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (fd == -1) {
		perror(path);
		return;
	}
	if (write(fd, rtc->ram, 256) != 256)
		fprintf(stderr, "%s: write failed.\n", path);
	close(fd);
}

void ds3234_load(struct ds3234 *rtc, const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd == -1)
		return;
	if (read(fd, rtc->ram, 256) != 256)
		fprintf(stderr, "%s: read failed.\n", path);
	close(fd);
}
