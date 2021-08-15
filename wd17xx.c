#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "system.h"
#include "wd17xx.h"

/*
 *	A very primitive WD17xx simulation
 */

struct wd17xx {
	int fd[4];
	unsigned int tracks[4];
	unsigned int spt[4];
	unsigned int secsize[4];
	unsigned int sides[4];
	unsigned int drive;
	uint8_t buf[512];
	unsigned int pos;
	unsigned int wr;
	unsigned int rd;
	uint8_t track;
	uint8_t sector;
	uint8_t status;
	uint8_t side;
	unsigned int intrq;

	unsigned int trace;
};

#define NOTREADY 	0x80
#define WPROT 		0x40
#define HEADLOAD 	0x20
#define CRCERR 		0x10
#define TRACK0 		0x08
#define INDEX 		0x04
#define DRQ 		0x02
#define BUSY		0x01

static void wd17xx_diskseek(struct wd17xx *fdc)
{
	off_t pos = fdc->track * fdc->spt[fdc->drive] * fdc->sides[fdc->drive];
	pos += fdc->sector - 1;
	if (fdc->sides[fdc->drive] == 2 && fdc->side)
		pos += fdc->spt[fdc->drive];
	pos *= fdc->secsize[fdc->drive];
	if (lseek(fdc->fd[fdc->drive], pos, SEEK_SET) < 0) {
		perror("lseek");
		exit(1);
	}
	if (fdc->trace) {
		fprintf(stderr, "fdc%d: seek to %d,%d,%d = %lx\n",
			fdc->drive, fdc->side, fdc->track, fdc->sector,
			(long)pos);
	}
}

uint8_t wd17xx_read_data(struct wd17xx *fdc)
{
	unsigned int end = fdc->secsize[fdc->drive] - 1 ;
	if (fdc->rd == 0) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: read without data ready.\n", fdc->drive);
		return fdc->buf[0];
	}
	if (fdc->pos > end) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: read beyond data end.\n", fdc->drive);
		return fdc->buf[end];
	}
	if (fdc->pos == end) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: last byte read dropping BUSY, DRQ.\n", fdc->drive);
		fdc->status &= ~(BUSY | DRQ);
		fdc->intrq = 1;
		fdc->rd = 0;
	}
	fflush(stdout);
	return fdc->buf[fdc->pos++];
}

void wd17xx_write_data(struct wd17xx *fdc, uint8_t v)
{
	unsigned int size = fdc->secsize[fdc->drive];
	if (fdc->wr == 0) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: write without data ready.\n", fdc->drive);
		fdc->buf[0] = v;
		return;
	}
	if (fdc->pos >= size) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: write beyond size.\n", fdc->drive);
		return;
	}
	fdc->buf[fdc->pos++] = v;
	if (fdc->pos == size) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: write final byte, dropping BUSY and DRQ.\n", fdc->drive);
		wd17xx_diskseek(fdc);
		if (write(fdc->fd[fdc->drive], fdc->buf, size) != size)
			fprintf(stderr, "wd: I/O error.\n");
		fdc->status &= ~(BUSY | DRQ);
		fdc->wr = 0;
		fdc->intrq = 1;
	}
}

uint8_t wd17xx_read_sector(struct wd17xx *fdc)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: read sector reg = %d.\n", fdc->drive, fdc->sector);
	return fdc->sector;
}

void wd17xx_write_sector(struct wd17xx *fdc, uint8_t v)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: write sector reg = %d.\n", fdc->drive, v);
	fdc->sector = v;
}

uint8_t wd17xx_read_track(struct wd17xx *fdc)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: read track reg = %d.\n", fdc->drive, fdc->track);
	return fdc->sector;
}

void wd17xx_write_track(struct wd17xx *fdc, uint8_t v)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: write track reg = %d.\n", fdc->drive, v);
	fdc->sector = v;
}

void wd17xx_command(struct wd17xx *fdc, uint8_t v)
{
	unsigned int size = fdc->secsize[fdc->drive];
	if (fdc->fd[fdc->drive] == -1) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: command to empty drive.\n", fdc->drive);
		fdc->status = NOTREADY;
		return;
	}
	if (fdc->trace)
		fprintf(stderr, "fdc%d: command %x.\n", fdc->drive, v);

	
	switch (v & 0xF0 ) {
	case 0x00:
		fdc->track = 0;
		fdc->status &= ~(BUSY | DRQ);
		fdc->intrq = 1;
		break;
	case 0x10:	/* seek */
		fdc->track = fdc->buf[0];
		fdc->status &= ~(BUSY | DRQ);
		fdc->intrq = 1;
		break;
	case 0x80:	/* Read sector */
		wd17xx_diskseek(fdc);
		fdc->rd = 1;
		if (read(fdc->fd[fdc->drive], fdc->buf, size) != size)
			fprintf(stderr, "wd: I/O error.\n");
		fdc->status |= BUSY | DRQ;
		fdc->pos = 0;
		break;
	case 0xA0:	/* Write sector */
		fdc->status |= BUSY | DRQ;
		fdc->pos = 0;
		fdc->wr = 1;
		break;
	case 0xD0:	/* Force interrupt */
		fdc->status &= ~(BUSY | DRQ);
		fdc->rd = 0;
		fdc->wr = 0;
		fdc->intrq = 1;
		return;
	case 0x20:	/* step */
	case 0x30:
	case 0x40:	/* step out */
	case 0x50:
	case 0x60:	/* step out */
	case 0x70:
	case 0x90:	/* read multi */
	case 0xB0:	/* write multi */
	case 0xC0:	/* read address */
	case 0xE0:	/* read track */
	case 0xF0:	/* write track */
	default:
		fprintf(stderr, "wd: unemulated command %02X.\n", v);
		fdc->intrq = 1;
		break;
	}
}

uint8_t wd17xx_status(struct wd17xx *fdc)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: status %x.\n", fdc->drive, fdc->status);
	fdc->intrq = 0;
	return fdc->status;
}

uint8_t wd17xx_status_noclear(struct wd17xx *fdc)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: status %x.\n", fdc->drive, fdc->status);
	return fdc->status;
}

struct wd17xx *wd17xx_create(void)
{
	struct wd17xx *fdc = malloc(sizeof(struct wd17xx));
	memset(fdc, 0, sizeof(*fdc));
	fdc->fd[0] = -1;
	fdc->fd[1] = -1;
	fdc->fd[2] = -1;
	fdc->fd[3] = -1;
	/* 35 track double sided */
	return fdc;
}

void wd17xx_detach(struct wd17xx *fdc, int dev)
{
	if (fdc->fd[dev] != -1)
		close(fdc->fd[dev]);
	fdc->fd[dev] = -1;
}

int wd17xx_attach(struct wd17xx *fdc, int dev, const char *path,
	unsigned int sides, unsigned int tracks,
	unsigned int sectors, unsigned int secsize)
{
	if (fdc->fd[dev])
		close(fdc->fd[dev]);
	fdc->fd[dev] = open(path, O_RDWR);
	if (fdc->fd[dev] == -1)
		perror(path);
	fdc->spt[dev] = sectors;
	fdc->tracks[dev] = tracks;
	fdc->sides[dev] = sides;
	fdc->secsize[dev] = secsize;
	return fdc->fd[dev];
}

void wd17xx_free(struct wd17xx *fdc)
{
	unsigned int i;
	for (i = 0; i < 4; i++)
		wd17xx_detach(fdc, i);
	free(fdc);
}

void wd17xx_set_drive(struct wd17xx *fdc, unsigned int drive)
{
	fdc->drive = drive;
}

void wd17xx_trace(struct wd17xx *fdc, unsigned int onoff)
{
	fdc->trace = onoff;
}

uint8_t wd17xx_intrq(struct wd17xx *fdc)
{
	return fdc->intrq;
}
