#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "drivewire.h"

#define DW_IDLE		0
#define DW_DATA_OUT	1
#define DW_DATA_IN	2
#define DW_ERR_OUT	3
#define DW_CSUM_IN_1	4
#define DW_CSUM_IN_2	5

#define DW_DRIVES	16

static uint8_t dw_mode = DW_IDLE;
static uint8_t dw_cmd;
static uint8_t dw_err;
static uint16_t dw_csum;
static uint16_t dw_rcsum;
static uint8_t dw_buf[262];
static uint8_t *dw_ptr = dw_buf;
static int dw_fd[DW_DRIVES];
static unsigned dw_len;

/*
 *	Produce a time block
 */

static void dw_setup_time(void)
{
	time_t t;
	struct tm *tm;
	time(&t);
	tm = localtime(&t);
	if (tm == NULL) {
		memset(dw_buf, 0, 7);
		return;
	}
	dw_buf[0] = tm->tm_year;
	dw_buf[1] = tm->tm_mon;
	dw_buf[2] = tm->tm_mday;
	dw_buf[3] = tm->tm_hour;
	dw_buf[4] = tm->tm_min;
	dw_buf[5] = tm->tm_sec;
	dw_buf[6] = tm->tm_wday;
}

/*
 *	Start a block receive
 */

static void dw_rx_block(unsigned len)
{
	dw_len = len;
	dw_ptr = dw_buf;
	dw_mode = DW_DATA_IN;
}

/*
 *	Ditto transmit
 */

static void dw_tx_block(unsigned len)
{
	dw_len = len;
	dw_ptr = dw_buf;
	dw_mode = DW_DATA_OUT;
	drivewire_byte_pending();
}

/*
 *	Emulate a microcontroller running drivewire (maybe this wants extracting into its own
 *	files). Need to implement idle timeouts (250ms)
 */

static void dw_command(uint8_t c)
{
	dw_cmd = c;
	switch(c) {
	case 0xFE:		/* Reset */
	case 0xFF:
		/* Meh */
		break;
	case 0x00:		/* Nop */
		break;
	case 0x49:		/* Initialze */
		/* Meh */
		break;
	case 0x54:		/* Terminate (client is rebooting etc */
		/* Meh */
		break;
	case 0xF2:		/* Re-read but it's the same functionality so .. */
	case 0xD2:
		dw_rx_block(4);	/* Should now get a 4 byte request block */
		break;
	case 0x47:		/* Getstat, setstat */
		dw_rx_block(2);
		break;
	case 0x53:
	case 0x77:		/* Write */
	case 0x57:
		dw_tx_block(262);	/* drive, LSN, 256 bytes, checksum */
		break;
	case 0x23:		/* Get date */
		dw_len = 7;
		/* Fill in buf */
		dw_setup_time();
		dw_tx_block(7);
		break;
	case 0x50:		/* Print */
		dw_rx_block(1);
		break;
	case 0x46:		/* Print flush */
		break;
	default:
		break;
	}
}

static uint16_t dw_checksum(uint8_t *p, unsigned len)
{
	uint16_t sum = 0;
	while(len--)
		sum += *p;
	return sum;
}

static void dw_csum_1(uint8_t c)
{
	dw_rcsum = c;
}

static void dw_csum_2(uint8_t c)
{
	dw_rcsum <<= 8;
	dw_rcsum |= c;
	/* Checksum done, report accordingly then go idle */
	if (dw_csum != dw_rcsum)
		dw_err = 0xF3;
	dw_mode = DW_ERR_OUT;
	drivewire_byte_pending();
}

static int dw_prepare(int *fd)
{
	uint32_t lsn;
	unsigned drive = dw_buf[0];
	if (drive >= DW_DRIVES || dw_fd[drive] == -1)
		return -1;
	lsn = dw_buf[1] << 24;
	lsn |= dw_buf[2] << 16;
	lsn |= dw_buf[3] << 8;
	*fd = dw_fd[drive];
	if (lseek(*fd, lsn, SEEK_SET) < 0)
		return -1;
	return 0;
}

static void dw_read(void)
{
	int fd;
	/* Bytes in buffer 0: drive, 1-3 LSN. Seek disk and get ready */
	if (dw_prepare(&fd))
		memset(dw_buf, 0, 256);	/* Send zeros on error */
	else if (read(fd, dw_buf, 256) != 256) {
		memset(dw_buf, 0, 256);
		dw_err = 0xF5;
	} else
		dw_err = 0x00;
	/* Now stream the bytes to the client */
	dw_len = 256;
	dw_ptr = dw_buf;
	dw_mode = DW_DATA_OUT;
	/* Weirdly the checksum is sent by the client and checked, not sent by server so adds
	   extra turn arounds and latency */
	dw_csum = dw_checksum(dw_buf, 256);
}

/* The block to write has arrived. Process it, set up for an error
   return and move to the err return state */
static void dw_write(void)
{
	int fd;
	dw_csum = dw_checksum(dw_buf + 4, 256);
	if ((dw_csum >> 8) != dw_buf[260] ||
		(dw_csum & 0xFF) != dw_buf[261]) {
		dw_err = 0xF3;
		return;
	}
	if (dw_prepare(&fd))
		return;
	if (write(fd, dw_buf + 4, 256) != 256) {
		dw_err = 0xF5;
		return;
	}
	dw_err = 0;
}

/* We received the block we were supposed to */
static void dw_in_done(void)
{
	switch(dw_cmd) {
	case 0xF2:
	case 0xD2:
		/* Read the data info block, set error */
		dw_read();
		/* Now send the 256 bytes */
		dw_len = 256;
		dw_mode = DW_DATA_OUT;
		drivewire_byte_pending();
		break;
	case 0x57:
	case 0x77:
		/* Data arrived. Do disk write */
		dw_write();
		dw_mode = DW_ERR_OUT;
		drivewire_byte_pending();
		break;
	default:
		dw_mode = DW_IDLE;
	}
}

/* We finished outputting a block.. now what ? */
static void dw_out_done(void)
{
	switch(dw_cmd) {
	case 0xD2:
	case 0xF2:
		/* We sent 256 bytes */
		/* Wait for checksum */
		dw_mode = DW_CSUM_IN_1;
		break;
	default:
		dw_mode = DW_IDLE;
	}
}

static void dw_data(uint8_t c)
{
	if (dw_len) {
		*dw_ptr++ = c;
		dw_len--;
	}
	if (dw_len == 0)
		dw_in_done();
}


/* Last byte acked, client wants the next */
uint8_t drivewire_tx(void)
{
	uint8_t r;
	switch(dw_mode) {
	case DW_DATA_OUT:
		r = *dw_ptr;
		dw_len--;
		if (dw_len == 0)
			dw_out_done();
		else
			drivewire_byte_pending();	/* Byte waiting */
		return r;
	case DW_ERR_OUT:
		r = dw_err;
		dw_mode = DW_IDLE;
		return r;
	default:
		return 0xFF;
	}
}

void drivewire_rx(uint8_t c)
{
	switch(dw_mode) {
	case DW_IDLE:
		dw_command(c);
		break;
	case DW_DATA_IN:
		dw_data(c);
		break;
	case DW_CSUM_IN_1:
		dw_csum_1(c);
		break;
	case DW_CSUM_IN_2:
		dw_csum_2(c);
		break;
	}
	/* Immediately ack the byte */
	drivewire_byte_read();
}

void drivewire_init(void)
{
	unsigned i;
	dw_mode = DW_IDLE;
	for (i = 0; i < DW_DRIVES; i++)
		dw_fd[i] = -1;
}

void drivewire_shutdown(void)
{
	unsigned i;
	for (i = 0; i < DW_DRIVES; i++) {
		if (dw_fd[i] != -1) {
			close(dw_fd[i]);
			dw_fd[i] = -1;
		}
	}
}

int drivewire_attach(unsigned drive, const char *path, unsigned ro)
{
	if (drive >= DW_DRIVES)
		return -1;
	if (ro)
		dw_fd[drive] = open(path, O_RDONLY);
	else
		dw_fd[drive] = open(path, O_RDWR);
	return dw_fd[drive] >= 0 ? 0 : -1;
}

void drivewire_detach(unsigned drive)
{
	if (drive >= DW_DRIVES)
		return;
	if (dw_fd[drive] != -1) {
		close(dw_fd[drive]);
		dw_fd[drive] = -1;
	}
}
