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
	unsigned int rdsize;
	uint8_t track;
	uint8_t sector;
	uint8_t status;
	uint8_t side;
	unsigned int intrq;

	unsigned int trace;
};

#define NOTREADY 	0x80	/* all commands */
#define WPROT 		0x40	/* some commands, 0 otherwise */
#define HEADLOAD 	0x20	/* type 1 only */
#define SEEKERR		0x10	/* type 1 only */
#define RECNFERR	0x10	/* record not found only some */
#define CRCERR 		0x08	/* crc error, not read/write track */
#define TRACK0 		0x04	/* type 1 only */
#define LOSTERR		0x04	/* lost data - not type 1 */
#define INDEX 		0x02	/* type 1 only */
#define DRQ 		0x02	/* not type 1 */
#define BUSY		0x01	/* all */

#define NO_DRIVE	0xFF

static void wd17xx_diskseek(struct wd17xx *fdc)
{
	off_t pos = (fdc->track & 0x7F) * fdc->spt[fdc->drive] * fdc->sides[fdc->drive];
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
	unsigned int end = fdc->rdsize - 1;
	/* No data ??? */
	if (fdc->rd == 0) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: read without data ready.\n", fdc->drive);
		return fdc->buf[0];
	}
	/* Data end, status follows. On the data end we drop BUSY */
	if (fdc->pos == end) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: last byte read dropping DRQ.\n", fdc->drive);
		fdc->status &= ~(DRQ|BUSY);
		fdc->intrq = 1;
		fdc->rd = 0;
		return fdc->buf[fdc->pos];
	}
	/* Hand out data */
	if (fdc->pos < end)
		return fdc->buf[fdc->pos++];
	if (fdc->trace)
		fprintf(stderr, "fdc%d: read beyond data end.\n", fdc->drive);
	return fdc->buf[end];
}

void wd17xx_write_data(struct wd17xx *fdc, uint8_t v)
{
	unsigned int size = fdc->secsize[fdc->drive];
	if (fdc->wr == 0) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: write %d without data ready.\n", fdc->drive, v);
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
		if (write(fdc->fd[fdc->drive], fdc->buf, size) != size) {
			perror("wd17xx: write: ");
			fprintf(stderr, "wd17xx: I/O error.\n");
		}
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
	return fdc->track;
}

void wd17xx_write_track(struct wd17xx *fdc, uint8_t v)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: write track reg = %d.\n", fdc->drive, v);
	fdc->track = v;
}

void wd17xx_command(struct wd17xx *fdc, uint8_t v)
{
	unsigned int size = fdc->secsize[fdc->drive];
	if (fdc->drive == NO_DRIVE || fdc->fd[fdc->drive] == -1) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: command to empty drive.\n", fdc->drive);
		fdc->status = NOTREADY;
		return;
	}
	if (fdc->trace)
		fprintf(stderr, "fdc%d: command %x.\n", fdc->drive, v);

	/* Special case */
	if ((v & 0xF0) == 0xD0) {
		fdc->rd = 0;
		fdc->wr = 0;
		fdc->pos = 0;
		if (fdc->status & BUSY)
			fdc->status &= ~BUSY;
		else {
			fdc->status = 0;
			if (fdc->track == 0)
				fdc->status |= TRACK0;
		}
		if (v & 0x01)
			fdc->intrq = 1;
		return;
	}
	if (fdc->status & BUSY)
		return;

	fdc->status = BUSY;

	fdc->rd = 0;
	fdc->wr = 0;
	fdc->pos = 0;
	
	switch (v & 0xF0 ) {
	case 0x00:
		fdc->track = 0;
		fdc->status = TRACK0 | INDEX;
		fdc->intrq = 1;
		break;
	case 0x10:	/* seek */
		fdc->intrq = 1;
		fdc->track = fdc->buf[0];
		fdc->status = INDEX;
		if ((fdc->buf[0] & 0x7F) >= fdc->tracks[fdc->drive]) {
			fdc->status |= SEEKERR;
			if (v & 0x08)
				fdc->status |= HEADLOAD;
			break;
		}
		if (fdc->track == 0)
			fdc->status |= TRACK0;
		if (v & 0x08)
			fdc->status |= HEADLOAD;
		break;
	case 0x80:	/* Read sector */
		if ((fdc->track & 0x7F) >= fdc->tracks[fdc->drive] ||
			fdc->sector > fdc->spt[fdc->drive] ||
			fdc->sector == 0) {
			fdc->status = INDEX | RECNFERR;
			return;
		}
		wd17xx_diskseek(fdc);
		fdc->rd = 1;
		if (read(fdc->fd[fdc->drive], fdc->buf, size) != size) {
			perror("wd17xx: read: ");
			fprintf(stderr, "wd17xx: I/O error.\n");
			fdc->status = INDEX | RECNFERR;
			return;
		}
		fdc->rdsize = size;
		fdc->status |= BUSY | DRQ;
		break;
	case 0xA0:	/* Write sector */
		if ((fdc->track & 0x7F) >= fdc->tracks[fdc->drive] ||
			fdc->sector > fdc->spt[fdc->drive] ||
			fdc->sector == 0) {
			fdc->status = INDEX | RECNFERR;
			return;
		}
		fdc->status |= BUSY | DRQ;
		fdc->wr = 1;
		break;
	case 0xC0:	/* read address */
		fdc->status |= BUSY | DRQ;
		fdc->rd = 1;
		fdc->rdsize = 6;
		fdc->buf[0] = fdc->track;
		/* If we tried to seek off the end of the disk then
		   we'll stop at the end track and see the data there */
		if (fdc->track >= fdc->tracks[fdc->drive]) {
			fdc->buf[0] = fdc->tracks[fdc->drive] - 1;
		}
		fdc->buf[1] = fdc->side;
		fdc->buf[2] = 1;
		fdc->buf[3] = 0x02;	/* FIXME: should be length encoding */
		fdc->buf[4] = 0xFA;
		fdc->buf[5] = 0xAF;
		/* Hardware weirdness */
		fdc->sector = fdc->track;
		fdc->intrq = 1;
		break;
	case 0xD0:	/* Force interrupt : handled above */
		break;
	case 0x20:	/* step */
	case 0x30:
	case 0x40:	/* step out */
	case 0x50:
	case 0x60:	/* step out */
	case 0x70:
	case 0x90:	/* read multi */
	case 0xB0:	/* write multi */
	case 0xE0:	/* read track */
	case 0xF0:	/* write track */
	default:
		fprintf(stderr, "wd17xx: unemulated command %02X.\n", v);
		fdc->status &= ~(BUSY | DRQ);
		fdc->rd = 0;
		fdc->wr = 0;
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
	if (fdc->trace)
		fprintf(stderr, "fdc%d: drive %d selected.\n",  fdc->drive, drive);
}

void wd17xx_no_drive(struct wd17xx *fdc)
{
	fdc->drive = NO_DRIVE;
}

void wd17xx_set_side(struct wd17xx *fdc, unsigned int side)
{
	fdc->side = side;
	if (fdc->trace)
		fprintf(stderr, "fdc%d: side %d selected.\n",  fdc->drive, side);
}

void wd17xx_trace(struct wd17xx *fdc, unsigned int onoff)
{
	fdc->trace = onoff;
}

uint8_t wd17xx_intrq(struct wd17xx *fdc)
{
	return fdc->intrq;
}
