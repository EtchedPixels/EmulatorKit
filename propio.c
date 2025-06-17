#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "serialdevice.h"
#include "propio.h"

/* PropIO v2 */

/* TODO:
    - The rx/tx pointers don't appear to be separate nor the data buffer
      so use one buffer and pointer set. Copy out the LBA on PREP
    - Stat isn't used but how should it work
    - What should be in the CSD ?
    - Four byte error blocks
    - Command byte for console does what ?
    - Status command does what ?
*/

struct propio {
	uint8_t csd[16];	/* What goes here ??? */
	uint8_t rbuf[4];
	uint8_t sbuf[512];
	uint8_t *rptr;
	uint16_t rlen;
	uint8_t *tptr;
	uint16_t tlen;
	uint8_t st;
	uint8_t err;
	int fd;
	off_t cardsize;
	unsigned int trace;
	struct serial_device *dev;
};

static uint32_t buftou32(struct propio *prop)
{
	uint32_t x;
	x = prop->rbuf[0];
	x |= ((uint8_t)prop->rbuf[1]) << 8;
	x |= ((uint8_t)prop->rbuf[2]) << 16;
	x |= ((uint8_t)prop->rbuf[3]) << 24;
	return x;
}

static void u32tobuf(struct propio *prop, uint32_t t)
{
	prop->rbuf[0] = t;
	prop->rbuf[1] = t >> 8;
	prop->rbuf[2] = t >> 16;
	prop->rbuf[3] = t >> 24;
	prop->st = 0;
	prop->err = 0;
	prop->rptr = prop->rbuf;
	prop->rlen = 4;
}

uint8_t propio_read(struct propio *prop, uint8_t addr)
{
	uint8_t r, v;

	addr &= 3;

	switch(addr) {
	case 0:		/* Console status */
		v = prop->dev->ready(prop->dev);
		r = (v & 1) ? 0x20:0x00;
		if (v & 2)
			r |= 0x10;
		return r;
	case 1:		/* Keyboard input */
		return prop->dev->get(prop->dev);
	case 2:		/* Disk status */
		if (prop->trace)
			fprintf(stderr, "propio: read status %02X\n", prop->st);
		return prop->st;
	case 3:		/* Data transfer */
		if (prop->rlen) {
			if (prop->trace)
				fprintf(stderr, "propio: read byte %02X\n", *prop->rptr);
			prop->rlen--;
			return *prop->rptr++;
		}
		else {
			if (prop->trace)
				fprintf(stderr, "propio: read byte - empty.\n");
			return 0xFF;
		}
	}
	return 0xFF;
}

void propio_write(struct propio *prop, uint8_t addr, uint8_t val)
{
	off_t lba;

	addr &= 3;

	switch(addr) {
		case 0:
			/* command port */
			break;
		case 1:
			/* write to screen */
			prop->dev->put(prop->dev, val);
			break;
		case 2:
			if (prop->trace)
				fprintf(stderr, "Command %02X\n", val);
			/* commands */
			switch(val) {
			case 0x00:	/* NOP */
				prop->err = 0;
				prop->st = 0;
				prop->tptr = prop->rbuf;
				prop->rptr = prop->rbuf;
				prop->tlen = 4;
				prop->rlen = 4;
				break;
			case 0x01:	/* STAT */
				prop->rptr = prop->rbuf;
				prop->rlen = 1;
				*prop->rbuf = prop->err;
				prop->err = 0;
				prop->st = 0;
				break;
			case 0x02:	/* TYPE */
				prop->err = 0;
				prop->rptr = prop->rbuf;
				prop->rlen = 1;
				prop->st = 0;
				if (prop->fd == -1) {
					*prop->rbuf = 0;
					prop->err = -9;
					prop->st |= 0x40;
				} else
					*prop->rbuf = 1;	/* MMC */
				break;
			case 0x03:	/* CAP */
				prop->err = 0;
				u32tobuf(prop, prop->cardsize >> 9);
				break;
			case 0x04:	/* CSD */
				prop->err = 0;
				prop->rptr = prop->csd;
				prop->rlen = sizeof(prop->csd);
				prop->st = 0;
				break;
			case 0x10:	/* RESET */
				prop->st = 0;
				prop->err = 0;
				prop->tptr = prop->rbuf;
				prop->rptr = prop->rbuf;
				prop->tlen = 4;
				prop->rlen = 4;
				break;
			case 0x20:	/* INIT */
				if (prop->fd == -1) {
					prop->st = 0x40;
					prop->err = -9;
					/* Error packet */
				} else {
					prop->st = 0;
					prop->err = 0;
				}
				break;
			case 0x30:	/* READ */
				lba = buftou32(prop);
				lba <<= 9;
				prop->err = 0;
				prop->st = 0;
				if (lseek(prop->fd, lba, SEEK_SET) < 0 ||
					read(prop->fd, prop->sbuf, 512) != 512) {
					prop->err = -6;
					/* Do error packet FIXME */
				} else {
					prop->rptr = prop->sbuf;
					prop->rlen = sizeof(prop->sbuf);
				}
				prop->tptr = prop->rbuf;
				prop->tlen = 4;
				break;
			case 0x40:	/* PREP */
				prop->st = 0;
				prop->err = 0;
				prop->tptr = prop->sbuf;
				prop->tlen = sizeof(prop->sbuf);
				prop->rlen = 0;
				break;
			case 0x50:	/* WRITE */
				lba = buftou32(prop);
				lba <<= 9;
				prop->err = 0;
				prop->st = 0;
				if (lseek(prop->fd, lba, SEEK_SET) < 0 ||
					write(prop->fd, prop->sbuf, 512) != 512) {
					prop->err = -6;
					/* FIXME: do error packet */
				}
				prop->tptr = prop->rbuf;
				prop->tlen = 4;
				break;
			case 0xF0:	/* VER */
				prop->rptr = prop->rbuf;
				prop->rlen = 4;
				/* Whatever.. use a version that's obvious fake */
				memcpy(prop->rbuf,"\x9F\x00\x0E\x03", 4);
				break;
			default:
				if (prop->trace)
					fprintf(stderr, "propio: unknown command %02X\n", val);
				break;
			}
			break;
		case 3:
			/* data write */
			if (prop->tlen) {
				if (prop->trace)
					fprintf(stderr, "propio: queue byte %02X\n", val);
				*prop->tptr++ = val;
				prop->tlen--;
			} else if (prop->trace)
				fprintf(stderr, "propio: write byte over.\n");
			break;
	}
}

struct propio *propio_create(const char *path)
{
	struct propio *prop = malloc(sizeof(struct propio));

	if (prop == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(prop, 0, sizeof(struct propio));

	prop->rptr = prop->rbuf;
	prop->tptr = prop->rbuf;
	prop->rlen = 4;
	prop->tlen = 4;
	prop->fd = -1;

	if (path != NULL) {
		prop->fd = open(path, O_RDWR);
		if (prop->fd == -1)
			perror(path);
		else if ((prop->cardsize = lseek(prop->fd, 0L, SEEK_END)) == -1) {
			perror(path);
			close(prop->fd);
			prop->fd = -1;
		}
	}
	return prop;
}

void propio_free(struct propio *prop)
{
	if (prop->fd != -1)
		close(prop->fd);
	free(prop);
}

void propio_reset(struct propio *prop)
{
	prop->rptr = prop->rbuf;
	prop->tptr = prop->rbuf;
	prop->rlen = 4;
	prop->tlen = 4;
}

void propio_attach(struct propio *prop, struct serial_device *dev)
{
	prop->dev = dev;
}

void propio_trace(struct propio *prop, int onoff)
{
	prop->trace = onoff;
}
