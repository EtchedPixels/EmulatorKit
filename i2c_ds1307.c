#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "system.h"
#include "i2c_bitbang.h"
#include "i2c_ds1307.h"


/* Real time clock state machine and related state.

   Based on rtc_bitbang.c but with an i2c interface and modified
	 to emulate a DS1307+ device (54 bytes of RAM).

   Give the host time and don't emulate time setting except for
   the 24/12 hour setting.
*/

enum {
	BIT0 = 0x01,
	BIT1 = 0x02,
	BIT2 = 0x04,
	BIT3 = 0x08,
	BIT4 = 0x10,
	BIT5 = 0x20,
	BIT6 = 0x40,
	BIT7 = 0x80
};
enum {
	FALSE = 0,
	TRUE  = 1
};

/* The i2c address of the DS1307+ */
#define RTC_ID 0x68

struct ds1307 {
	uint8_t    ram[64]; /* Register values and NVRAM */
	uint8_t    clock24; /* Output mode for the clock */
	uint8_t    bp;      /* Index into the 'ram' array of current byte */
	uint8_t    first;   /* Set to TRUE at start of block. Next byte is the bp address */
	struct tm *tm;      /* OS version of time */
	uint8_t    trace;   /* Non-zero to cause trace output */
};

/* rtc_read
 * Return the byte at the current ram location and move the
 * pointer to the next byte.
 */
uint8_t rtc_read(struct ds1307 *rtc) {
	const uint8_t byte = rtc->ram[rtc->bp++];

	/* Wrap at 64 bytes */
	rtc->bp &= 0x3f;

	return byte;
}

/* rtc_write
 * Write a byte into memory at the current 'bp' position then
 * increment the counter. Pull out the 12/24 clock marker.
 *
 * NOTE: Writes to the first 8 bytes *should* set the time but
 * the current implementation ignores this. The values are
 * stored in rtc->ram but will be overwritten on the next read
 * with the current system time.
 *
 * Increment the 'bp' pointer after the write.
 */
static void rtc_write(struct ds1307 *rtc, uint8_t byte) {
	if (rtc->first) {
		/* The first byte is the initial address (bp) */
		rtc->bp = byte &0x3f;
		if (rtc->trace)
			fprintf(stderr, "ds1307: Set pointer = %d\n", (int)rtc->bp);
		rtc->first = FALSE;
	}
	else {
		if (rtc->trace)
			fprintf(stderr, "ds1307: Write @addr %02x Value %02x\n", (int)rtc->bp, byte);
		rtc->ram[rtc->bp] = byte;
		if (rtc->bp==2)
			rtc->clock24 = (byte & 0x20);

		rtc->bp = (rtc->bp+1) & 0x3f;
	}
}

/* rtc_freeze
 * At the start of a read request freeze the time and copy
 * data into the output registers.
 */
static void rtc_freeze(struct ds1307 *rtc) {
uint8_t v, val;

	time_t t = time(NULL);
	rtc->tm = localtime(&t);

	if (rtc->tm == NULL) {
		fprintf(stderr, "ds1307: unable to process time.\n");
		exit(1);
	}
	if (rtc->trace)
		fprintf(stderr, "ds1307: time latched.\n");

	/* Copy values into the output buffers */
	rtc->ram[0] = (rtc->tm->tm_sec % 10) + ((rtc->tm->tm_sec / 10) << 4);
	rtc->ram[1] = (rtc->tm->tm_min % 10) + ((rtc->tm->tm_min / 10) << 4);

	v = rtc->tm->tm_hour;
	if (!rtc->clock24) {
		/* 12 hour clock */
		v = (v % 12) + 1;
	}
	val = (v % 10) + ((v / 10) << 4);
	if (!rtc->clock24) {
		/* 12 hour clock, set the AM/PM bit */
		if (rtc->tm->tm_hour > 11)
			val |= BIT5;

		/* Set bit 6 - the 12 hour marker*/
		val |= BIT6;
	}
	rtc->ram[2] = val;
	rtc->ram[3] = (rtc->tm->tm_wday % 10)+1;
	rtc->ram[4] = (rtc->tm->tm_mday % 10) + ((rtc->tm->tm_mday / 10) << 4);
	rtc->ram[5] = ((rtc->tm->tm_mon + 1) % 10) + (((rtc->tm->tm_mon + 1) / 10) << 4);

	v = rtc->tm->tm_year % 100;
	rtc->ram[6] = (v % 10) + ((v / 10) << 4);
}

void rtc_reset(struct ds1307 *rtc) {
	memset(rtc, 0, sizeof(struct ds1307));
	rtc->clock24 = 1;
}

void rtc_free(struct ds1307 *rtc) {
	free(rtc);
}

void rtc_trace(struct ds1307 *rtc, int onoff) {
	rtc->trace = (onoff!=0);
}

/* rtc_save
 * Save the content of the 54 bytes of NVRAM (battery backed) in
 * the DS1307+ to a physical file. Allows the data to correctly
 * persist between executions of the emulator.
 */
void rtc_save(struct ds1307 *rtc, const char *path) {
	int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (fd == -1) {
		perror(path);
		return;
	}
	if (write(fd, &rtc->ram[8], 54) != 54)
		fprintf(stderr, "%s: write failed.\n", path);
	close(fd);
}

/* rtc_load
 * Load the initial state of the 54 bytes of NVRAM from a file
 */
void rtc_load(struct ds1307 *rtc, const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd == -1)
		return;
	if (read(fd, &rtc->ram[8], 54) != 54)
		fprintf(stderr, "%s: read failed.\n", path);
	close(fd);
}

const char *opdesc[] = {
	"***", "START", "READ", "WRITE", "END"
};

/* rtc_callback
 * Entry point from the i2c_bitbang implementation. This
 * function gets called at each major state transition
 * in the i2c state machine,.
 */
uint8_t rtc_callback(void *client, I2C_OP op, uint8_t data) {
	struct ds1307 *rtc = (struct ds1307 *)client;

	uint8_t res = 0xff;

	switch (op) {
	case START:
		if (rtc->trace)
			fprintf(stderr, "ds1307: OP: ----- 'START' -----\n");
		rtc_freeze(rtc);
		rtc->first = TRUE;
		break;
	case READ:
		res = rtc_read(rtc);
		if (rtc->trace)
			fprintf(stderr, "ds1307: OP: ----- 'READ' ----- Byte: %02x\n", res);
		break;
	case WRITE:
		if (rtc->trace)
			fprintf(stderr, "ds1307: OP: ----- 'WRITE' ----- Byte: %02x\n", data);
		rtc_write(rtc, data);
		break;
	case END:
		if (rtc->trace)
			fprintf(stderr, "ds1307: OP: ----- 'END' -----\n");
		break;
	}
	return res;
}

/* rtc_create
 * Create a new RTC (DS1307+) and register with the i2c bus emulation.
 */
struct ds1307 *rtc_create(struct i2c_bus *i2cbus) {
	struct ds1307 *rtc = malloc(sizeof(struct ds1307));
	if (rtc == NULL)
	{
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	rtc_reset(rtc);

	i2c_register(i2cbus, rtc, RTC_ID, rtc_callback);

	return rtc;
}
