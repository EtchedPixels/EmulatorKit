#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "pprop.h"

/* Parallel Port Propellor */

/* TODO
	- keyboard input(kwait)
	- correct error returns
	- emulate 82C55 bitops and register behaviour
 */

typedef void (*prop_callback_t)(struct pprop *);

struct pprop {
	uint8_t csd[16];	/* What goes here ??? */
	uint8_t buf[512];
	uint8_t sbuf[512];
	uint8_t *rptr;
	uint16_t rlen;
	uint8_t *tptr;
	uint16_t tlen;
	uint8_t portc;
	uint8_t kwait;
	int fd;
	prop_callback_t callback;
	off_t cardsize;
	unsigned int input;
	unsigned int trace;
};

static uint8_t zeros[16];
static uint8_t prop_ver[] = { 0x36, 0x3E, 0xE, 0x03 };

static uint32_t buftou32(struct pprop *prop)
{
	uint32_t x;
	x = prop->buf[0];
	x |= ((uint8_t) prop->buf[1]) << 8;
	x |= ((uint8_t) prop->buf[2]) << 16;
	x |= ((uint8_t) prop->buf[3]) << 24;
	return x;
}

static void u32tobuf(struct pprop *prop, uint32_t t)
{
	prop->buf[0] = t;
	prop->buf[1] = t >> 8;
	prop->buf[2] = t >> 16;
	prop->buf[3] = t >> 24;
}

static void prop_queue_tx(struct pprop *prop, uint8_t *buf, unsigned len)
{
	if (prop->buf != buf)
		memcpy(prop->buf, buf, len);
	prop->tptr = prop->buf;
	prop->tlen = len;
	fprintf(stderr, "pprop: queue tx of %d\n", len);
}

static void prop_wait_rx(struct pprop *prop, unsigned n,
			 prop_callback_t fn)
{
	prop->callback = fn;
	prop->rptr = prop->buf;
	prop->rlen = n;
	fprintf(stderr, "pprop: wait for rx of %d\n", n);
}

static void prop_rsector(struct pprop *prop)
{
	uint32_t lba = buftou32(prop);
	uint8_t n = 5;		/* FIXME - proper error code for errors and no media */

	if (lseek(prop->fd, lba, SEEK_SET) < 0 ||
	    read(prop->fd, prop->sbuf, 512) != 512) {
		prop_queue_tx(prop, &n, 1);
		return;
	}
	prop_queue_tx(prop, zeros, 1);
}

static void prop_wsector(struct pprop *prop)
{
	uint32_t lba = buftou32(prop);
	uint8_t n = 5;		/* FIXME - proper error code for errors and no media */

	if (lseek(prop->fd, lba, SEEK_SET) < 0 ||
	    write(prop->fd, prop->sbuf, 512) != 512) {
		prop_queue_tx(prop, &n, 1);
		return;
	}
	prop_queue_tx(prop, zeros, 1);
}

static void prop_putsec(struct pprop *prop)
{
	memcpy(prop->sbuf, prop->buf, 512);
}

static void prop_write_scr(struct pprop *prop)
{
	write(1, prop->buf, 1);
}

static void prop_command(struct pprop *prop, uint8_t cmd)
{
	uint8_t n;

	fprintf(stderr, "prop_cmd %02X\n", cmd);
	switch (cmd) {
		break;
	case 0x10:		/* SD Restart */
		prop_queue_tx(prop, zeros, 1);
		break;
	case 0x11:		/* SD last status */
		prop_queue_tx(prop, zeros, 4);
		break;
	case 0x12:		/* Put sector buffer */
		prop_wait_rx(prop, 512, prop_putsec);
		break;
	case 0x13:		/* Get sector buffer */
		prop_queue_tx(prop, prop->sbuf, 512);
		break;
	case 0x14:		/* Read sector into buffer, return status byte */
		prop_wait_rx(prop, 4, prop_rsector);
		break;
	case 0x15:		/* Write sector from buffer, return status byte */
		prop_wait_rx(prop, 4, prop_wsector);
		break;
	case 0x16:		/* Get SD type */
		n = 2;		/* SDSC */
		prop_queue_tx(prop, &n, 1);
		break;
	case 0x17:		/* Get capacity */
		u32tobuf(prop, prop->cardsize >> 9);
		prop_queue_tx(prop, prop->buf, 4);
		break;
	case 0x18:		/* Get CSD */
		prop_queue_tx(prop, prop->csd, 16);
		break;
	case 0x20:		/* Write to screen */
		prop_wait_rx(prop, 1, prop_write_scr);
		break;
	case 0x30:		/* Check keyboard buffer pending */
		if (!prop->input)
			prop_queue_tx(prop, zeros, 1);
		else {
			n = check_chario() & 2;
			if (n)
				n = 1;
			prop_queue_tx(prop, &n, 1);
		}
		break;
	case 0x31:		/* Return char, wait if needed */
		if (prop->input)
			prop->kwait = 1;
		break;
	case 0xF0:		/* Soft reset */
		pprop_reset(prop);
		n = 0xAA;
		prop_queue_tx(prop, &n, 1);
		break;
	case 0xF1:		/* Send firmware version */
		prop_queue_tx(prop, prop_ver, 4);
		break;

		/* Not used by ROMWBW */

	case 0x00:		/* NOP - not used by ROMWBW */
	case 0x01:		/* Reply inverted - not used by ROMWBW ? */
	case 0x02:		/* Reply buffer inverted - not used by ROMWBW */
	case 0x40:		/* Beep */
	case 0x50:		/* Init serial port */
	case 0x51:		/* RX serial */
	case 0x52:		/* TX serial */
	case 0x53:		/* Serial RX status */
	case 0x54:		/* Serial TX status */
	case 0x56:		/* Serial flush (unimp in real device) */
		fprintf(stderr, "pprop: %02X not emulated.\n", cmd);
		exit(1);
	}
}

static void prop_data(struct pprop *prop, uint8_t data)
{
	/* data write */
	if (prop->rlen) {
		if (prop->trace)
			fprintf(stderr, "pprop: queue byte %02X\n", data);
		*prop->rptr++ = data;
		prop->rlen--;
		if (prop->rlen == 0 && prop->callback) {
			prop_callback_t fn = prop->callback;
			prop->callback = NULL;
			fn(prop);
		}
	} else if (prop->trace)
		fprintf(stderr, "pprop: write byte over.\n");
}

static uint8_t do_pprop_read(struct pprop *prop, uint8_t addr)
{
	addr &= 3;

	switch (addr) {
	case 0:
		/* Communications */
		fprintf(stderr, "RD tlen %d\n", prop->tlen);
		if (prop->tlen) {
			if (prop->trace)
				fprintf(stderr, "pprop: read byte %02X\n",
					*prop->tptr);
			prop->tlen--;
			return *prop->tptr++;
		}
		/* Nothing to return */
		return 0xFF;
	case 1:
		break;
	case 2:
		/* Status. Set the empty bit */
		prop->portc &= 0x0F;
		/* Always RX ready */
		prop->portc |= 0x20;
		/* TODO: sort out TX */
		prop->portc |= 0x80;
		return prop->portc;
	case 3:
		break;
	}
	return 0xFF;
}

uint8_t pprop_read(struct pprop *prop, uint8_t addr)
{
	uint8_t val = do_pprop_read(prop, addr);
	fprintf(stderr, "pprop: R %d %02X\n", addr, val);
	return val;
}

void pprop_write(struct pprop *prop, uint8_t addr, uint8_t val)
{
	uint8_t bit;

	addr &= 3;

	fprintf(stderr, "pprop: W %d %02X %c\n", addr, val, "DC"[prop->portc & 1]);
	switch (addr) {
	case 0:
		/* Communications */
		if (prop->portc & 1)
			prop_command(prop, val);
		else
			prop_data(prop, val);
		break;
	case 1:
		/* Unused */
		break;
	case 2:
		/* Control port */
		prop->portc &= 0xF0;
		prop->portc |= val & 0x0F;
		/* Reset behaviour ? */
		break;
	case 3:
		/* 82C55 control.. emulate bit ops */
		if (val & 0x80)
			return;
		bit = 1 << ((val >> 1) & 7);
		if (val & 1)
			pprop_write(prop, 2, prop->portc | bit);
		else
			pprop_write(prop, 2, prop->portc & ~bit);
		break;
	}
}

struct pprop *pprop_create(const char *path)
{
	struct pprop *prop = malloc(sizeof(struct pprop));

	if (prop == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(prop, 0, sizeof(struct pprop));

	prop->rptr = prop->buf;
	prop->tptr = prop->buf;
	prop->rlen = 0;
	prop->tlen = 0;
	prop->fd = -1;
	prop->trace = 1; /* TODO */

	if (path != NULL) {
		prop->fd = open(path, O_RDWR);
		if (prop->fd == -1)
			perror(path);
		else if ((prop->cardsize =
			  lseek(prop->fd, 0L, SEEK_END)) == -1) {
			perror(path);
			close(prop->fd);
			prop->fd = -1;
		}
	}
	return prop;
}

void pprop_free(struct pprop *prop)
{
	if (prop->fd != -1)
		close(prop->fd);
	free(prop);
}

void pprop_reset(struct pprop *prop)
{
	prop->rptr = prop->buf;
	prop->tptr = prop->buf;
	prop->rlen = 0;
	prop->tlen = 0;
}

void pprop_set_input(struct pprop *prop, int onoff)
{
	prop->input = onoff;
}

void pprop_trace(struct pprop *prop, int onoff)
{
	prop->trace = onoff;
}
