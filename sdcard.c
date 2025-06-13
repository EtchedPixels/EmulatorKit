/* Minimal SD card emulation (needs extracting into generic code */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include "sdcard.h"

struct sdcard {
	int sd_mode;
	int sd_cmdp;
	int sd_ext;
	uint8_t sd_cmd[9];
	uint8_t sd_in[520];
	int sd_inlen;
	int sd_inp;
	uint8_t sd_out[520];
	int sd_outlen;
	int sd_outp;
	int sd_fd;
	off_t sd_lba;
	int sd_stuff;
	uint8_t sd_poststuff;
	int sd_cs;
	const char *sd_name;
	int debug;
	unsigned block;
};

static const uint8_t sd_csd[17] = {

	0xFE,		/* Sync byte before CSD */
	/* Taken from a Toshiba 64MB card c/o softgun */
	0x00, 0x2D, 0x00, 0x32,
	0x13, 0x59, 0x83, 0xB1,
	0xF6, 0xD9, 0xCF, 0x80,
	0x16, 0x40, 0x00, 0x00
};

static const uint8_t sdhc_csd[17] = {

	0xFE,		/* Sync byte before CSD */
	/* Taken from unbranded 4GB SDHC card */
	0x40, 0x0E, 0x00, 0x32,
	0x5B, 0x59, 0x00, 0x00,
	0x1D, 0xFF, 0x7F, 0x80,
	0x0A, 0x40, 0x00, 0x7D
};

static const uint8_t sd_cid[] = {
	0xFE,	/* Sync byte */
	0x02,	/* Toshiba */
	'T','S',
	'R','C','E','M','U',
	1,
	0xAA,0x55,0xAA,0x55,
	0x00, 0x14,
	0xFF	/* should be a checksum */
};

static uint8_t sd_process_command(struct sdcard *c)
{
	c->sd_stuff = 2 + (rand() & 7);
	if (c->sd_ext) {
		c->sd_ext = 0;
		if (c->debug)
			fprintf(stderr, "%s: Extended command %x\n", c->sd_name, c->sd_cmd[0]);
		switch(c->sd_cmd[0]) {
		case 0x40+41:
			return 0x00;
		default:
			return 0x7F;
		}
	}
	if (c->debug)
		fprintf(stderr, "%s: Command received %x\n", c->sd_name, c->sd_cmd[0]);
	switch(c->sd_cmd[0]) {
	case 0x40+0:		/* CMD 0 */
		return 0x01;	/* Just respond 0x01 */
	case 0x40+1:		/* CMD 1 - leave idle */
		return 0x00;	/* Immediately indicate we did */
	case 0x40+8:		/* CMD 8 - send iface cond */
		c->sd_out[0] = 0x00;	/* Version 0 */
		c->sd_out[1] = 0x00;	/* Blah... */
		c->sd_out[2] = 0x01;	/* 0x01 - 3v3 */
		c->sd_out[3] = c->sd_cmd[4];
		c->sd_outlen = 4;
		c->sd_outp = 0;
		c->sd_mode = 2;
		return 0x01;
	case 0x40+9:		/* CMD 9 - read the CSD */
		if (c->block)
			memcpy(c->sd_out,sdhc_csd, 17);
		else
			memcpy(c->sd_out,sd_csd, 17);
		c->sd_outlen = 17;
		c->sd_outp = 0;
		c->sd_mode = 2;
		return 0x00;
	case 0x40+10:		/* CMD10 - read the CID */
		memcpy(c->sd_out, sd_cid, 17);
		c->sd_outlen = 17;
		c->sd_outp = 0;
		c->sd_mode = 2;
		return 0x00;
	case 0x40+13:		/* CMD 13 - send status*/
		c->sd_out[0] = 0x00;	/* Return 2 0-Bytes */
		c->sd_out[1] = 0x00;	/* To indicate no error */
		c->sd_outlen = 1;
		c->sd_outp = 0;
		c->sd_mode = 2;
		return 0x00;
	case 0x40+16:		/* CMD 16 - set block size */
		/* Should check data is 512 !! FIXME */
		return 0x00;	/* Sure */
	case 0x40+17:		/* Read */
		c->sd_outlen = 514;
		c->sd_outp = 0;
		/* Sync mark then data */
		c->sd_out[0] = 0xFF;
		c->sd_out[1] = 0xFE;
		c->sd_lba = c->sd_cmd[4] + 256 * c->sd_cmd[3] + 65536 * c->sd_cmd[2] +
			16777216 * c->sd_cmd[1];
		if (c->block)
			c->sd_lba <<= 9;
		if (c->debug)
			fprintf(stderr, "%s: Read LBA %lx\n", c->sd_name, (long)c->sd_lba);
		if (lseek(c->sd_fd, c->sd_lba, SEEK_SET) < 0 || read(c->sd_fd, c->sd_out + 2, 512) != 512) {
			if (c->debug)
				fprintf(stderr, "%s: Read LBA failed.\n", c->sd_name);
			return 0x01;
		}
		c->sd_mode = 2;
		/* Result */
		return 0x00;
	case 0x40+24:		/* Write */
		/* Will send us FE data FF FF */
		c->sd_inlen = 515;	/* Data FF FF FF */
		c->sd_lba = c->sd_cmd[4] + 256 * c->sd_cmd[3] + 65536 * c->sd_cmd[2] +
			16777216 * c->sd_cmd[1];
		if (c->block)
			c->sd_lba <<= 9;
		if (c->debug)
			fprintf(stderr, "%s: Write LBA %lx\n", c->sd_name, (long)c->sd_lba);
		c->sd_inp = 0;
		c->sd_mode = 4;	/* Send a pad then go to mode 3 */
		return 0x00;	/* The expected OK */
	case 0x40+55:
		c->sd_ext = 1;
		return 0x01;
	case 0x40+58:
		/* Minimal bits of HC/XC support */
		c->sd_outlen = 4;
		c->sd_outp = 0;
		c->sd_out[0] = 0x80;
		c->sd_out[1] = 0xFF;
		c->sd_out[2] = 0;
		c->sd_out[3] = 0;
		c->sd_mode = 2;
		if (c->block)
			c->sd_out[0] = 0xC0;
		return 0x00;
	default:
		return 0x7F;
	}
}

static uint8_t sd_process_data(struct sdcard *c)
{
	switch(c->sd_cmd[0]) {
	case 0x40+24:		/* Write */
		c->sd_mode = 0;
		if (lseek(c->sd_fd, c->sd_lba, SEEK_SET) < 0 ||
			write(c->sd_fd, c->sd_in, 512) != 512) {
			if (c->debug)
				fprintf(stderr, "%s: Write failed.\n", c->sd_name);
			return 0x1E;	/* Need to look up real values */
		}
		return 0x05;	/* Indicate it worked */
	default:
		c->sd_mode = 0;
		return 0xFF;
	}
}

static uint8_t sd_card_byte(struct sdcard *c, uint8_t in)
{
	/* No card present */
	if (c->sd_fd == -1)
		return 0xFF;

	/* Stuffing on commands */
	if (c->sd_stuff) {
		if (--c->sd_stuff)
			return 0xFF;
		return c->sd_poststuff;
	}

	if (c->sd_mode == 0) {
		if (in != 0xFF && in != 0x00) {
			c->sd_mode = 1;	/* Command wait */
			c->sd_cmdp = 1;
			c->sd_cmd[0] = in;
		}
		return 0xFF;
	}
	if (c->sd_mode == 1) {
		c->sd_cmd[c->sd_cmdp++] = in;
		if (c->sd_cmdp == 6) {	/* Command complete */
			c->sd_cmdp = 0;
			c->sd_mode = 0;
			/* Reply with either a stuff byte (CMD12) or a
			   status */
			c->sd_poststuff = sd_process_command(c);
			return 0xFF;
		}
		/* Keep talking */
		return 0xFF;
	}
	/* Writing out the response */
	if (c->sd_mode == 2) {
		if (c->sd_outp + 1 == c->sd_outlen)
			c->sd_mode = 0;
		return c->sd_out[c->sd_outp++];
	}
	/* Commands that need input blocks first */
	if (c->sd_mode == 3) {
		c->sd_in[c->sd_inp++] = in;
		if (c->sd_inp == c->sd_inlen)
			return sd_process_data(c);
		/* Keep sending */
		return 0xFF;
	}
	/* Sync up before data flow starts */
	if (c->sd_mode == 4) {
		/* Sync */
		if (in == 0xFE)
			c->sd_mode = 3;
		return 0xFF;
	}
	return 0xFF;
}

/*
 *	Public interface
 */
void sd_spi_raise_cs(struct sdcard *c)
{
	c->sd_mode = 0;
	c->sd_cs = 1;
}

void sd_spi_lower_cs(struct sdcard *c)
{
	c->sd_cs = 0;
}

uint8_t sd_spi_in(struct sdcard *c, uint8_t v)
{
	if (c->sd_cs)
		return 0xFF;
	return sd_card_byte(c, v);
}

void sd_detach(struct sdcard *c)
{
	if (c->sd_fd != -1) {
		close(c->sd_fd);
		c->sd_fd = -1;
	}
}

void sd_attach(struct sdcard *c, int fd)
{
	sd_detach(c);
	c->sd_fd = fd;
}

void sd_trace(struct sdcard *c, int onoff)
{
	c->debug = onoff;
}

void sd_reset(struct sdcard *c)
{
	c->sd_cs = 1;
	c->sd_mode = 0;
}

struct sdcard *sd_create(const char *name)
{
	struct sdcard *c = malloc(sizeof(struct sdcard));
	if (c == NULL) {
		fprintf(stderr, "sd_create: out of memory.\n");
		exit(1);
	}
	memset(c, 0, sizeof(struct sdcard));
	c->sd_name = name;
	c->sd_fd = -1;
	sd_reset(c);
	return c;
}

void sd_free(struct sdcard *c)
{
	sd_detach(c);
	free(c);
}

void sd_blockmode(struct sdcard *c)
{
	c->block = 1;
}
