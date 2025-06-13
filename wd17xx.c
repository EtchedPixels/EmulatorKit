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
	unsigned int sector0[4];/* Sector base - usually 1, now and then 0
				   and on the Ampro sometimes 17 */
	unsigned int side1[4];	/* Base track number for second side. Usually 0
				   but some systems do strange stuff */
	unsigned int diskden[4];
	unsigned int drive;
	uint8_t buf[2048];
	unsigned int pos;
	unsigned int wr;
	unsigned int rd;
	unsigned int rdsize;
	uint8_t track;
	uint8_t sector;
	uint8_t status;
	uint8_t side;
	uint8_t lastcmd;
	unsigned int intrq;
	unsigned int density;
	int stepdir;
	unsigned int motor;
	unsigned int spinup;
	unsigned int trace;

	unsigned int type;	/* Type - 1791, 1772 for now */
	unsigned motor_timeout;	/* Timeout in ms */

	unsigned int busy;
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
	off_t pos;
	unsigned track = fdc->track;

	/* Devices with different numbering for side 1 */
	if (fdc->side)
		track -= fdc->side1[fdc->drive];

	pos = track * fdc->spt[fdc->drive] * fdc->sides[fdc->drive];
	pos += fdc->sector - fdc->sector0[fdc->drive];
	if (fdc->sides[fdc->drive] == 2 && fdc->side)
		pos += fdc->spt[fdc->drive];
	pos *= fdc->secsize[fdc->drive];
	if (lseek(fdc->fd[fdc->drive], pos, SEEK_SET) < 0) {
		perror("lseek");
		exit(1);
	}
	if (fdc->trace) {
		fprintf(stderr, "fdc%d: seek to %d,%d,%d = %lx\n",
			fdc->drive, fdc->side, track, fdc->sector,
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

/* Some of the controllers support setting the side by command bits */
void wd17xx_side_control(struct wd17xx *fdc, unsigned v)
{
	if (fdc->type == 2795 || fdc->type == 1795 ||
		fdc->type == 2797 || fdc->type == 1797) {
		fdc->side = (v & 2) ? 1 : 0;
	}
}

void wd17xx_motor(struct wd17xx *fdc, unsigned on)
{
	if (on && fdc->motor == 0) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: motor starts.\n",
				fdc->drive);
		/* Whatever spin up we need to do */
		fdc->motor = fdc->motor_timeout;	/* 10,000 ms */
		fdc->spinup = 1000;
	}
}

void wd17xx_set_motor_time(struct wd17xx *fdc, unsigned n)
{
	fdc->motor_timeout = n;
}

unsigned wd17xx_get_motor(struct wd17xx *fdc)
{
	return !!fdc->motor;
}

/* We only use this for very crude motor stuff at the moment */
void wd17xx_tick(struct wd17xx *fdc, unsigned ms)
{
	if (fdc->motor == 0)
		return;
	if (fdc->motor <= ms) {
		fdc->motor = 0;
		if (fdc->trace)
			fprintf(stderr, "fdc%d: motor stops.\n",
				fdc->drive);
	} else
		fdc->motor -= ms;
	if (fdc->spinup) {
		if (fdc->spinup <= ms) {
			fdc->spinup = 0;
			if (fdc->lastcmd < 0x80)
				fdc->status |= HEADLOAD;
		}
	}
}

static void wd17xx_check_density(struct wd17xx *fdc)
{
	if (fdc->density == DEN_ANY || fdc->diskden[fdc->drive] == DEN_ANY)
		return;
	if (fdc->density == fdc->diskden[fdc->drive])
		return;
	fdc->status &= TRACK0 | HEADLOAD;
	/* SEEKER on type 1 RECNFERR on others so this works fine for both */
	fdc->status |= SEEKERR;
}

void wd17xx_command(struct wd17xx *fdc, uint8_t v)
{
	unsigned int size = fdc->secsize[fdc->drive];
	unsigned motor = !(v & 0x08);
	unsigned track;


	if (fdc->drive == NO_DRIVE || fdc->fd[fdc->drive] == -1) {
		if (fdc->trace)
			fprintf(stderr, "fdc%d: command to empty drive.\n", fdc->drive);
		fdc->status = NOTREADY;
		return;
	}

	fdc->lastcmd = v;
	fdc->busy = 0;

	track = fdc->track;
	if (fdc->side)
		track -= fdc->side1[fdc->drive];

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
		if (v & 0x0B)	/* not ready->ready, index, immediate */
			fdc->intrq = 1;
		wd17xx_motor(fdc, 1);
		return;
	}
	if (fdc->status & BUSY)
		return;

	fdc->status = BUSY;
	fdc->busy = 64;

	fdc->rd = 0;
	fdc->wr = 0;
	fdc->pos = 0;
	fdc->intrq = 0;

	switch (v & 0xF0 ) {
	case 0x00:
		fdc->track = 0;
		fdc->status |= TRACK0 | INDEX;
		if (v & 8)
			fdc->status |= HEADLOAD;
		fdc->stepdir = 1;
		wd17xx_motor(fdc, motor);
		if (v & 0x08) {
			if (fdc->side >= fdc->sides[fdc->drive]) {
				fdc->status |= RECNFERR;
				return;
			}
			wd17xx_check_density(fdc);
		}
		/* Some systems issue a command and then poll for busy to check the command
		   started */
		break;
	case 0x10:	/* seek */
		if (fdc->track < fdc->buf[0])
			fdc->stepdir = 1;
		if (fdc->track > fdc->buf[0])
			fdc->stepdir = -1;
		fdc->track = fdc->buf[0];
		fdc->status |= INDEX;
		if (fdc->track >= fdc->tracks[fdc->drive]) {
			fdc->status |= SEEKERR;
			if (v & 0x08)
				fdc->status |= HEADLOAD;
			break;
		}
		if (fdc->track == 0)
			fdc->status |= TRACK0;
		if (v & 0x08) {
			if (fdc->side >= fdc->sides[fdc->drive]) {
				fdc->status |= SEEKERR;
				return;
			}
			fdc->status |= HEADLOAD;
			wd17xx_check_density(fdc);
		}
		wd17xx_motor(fdc, 1/*DEBUGME motor*/);
		break;
	case 0x20:	/* step */
	case 0x30:
		if (fdc->track && fdc->stepdir == -1)
			fdc->track--;
		if (fdc->track < 128 && fdc->stepdir == 1)
			fdc->track++;
		fdc->status |= INDEX;
		if (fdc->track == 0)
			fdc->status |= TRACK0;
		if (v & 0x08) {
			if (fdc->side >= fdc->sides[fdc->drive]) {
				fdc->status = SEEKERR;
				fdc->intrq = 1;
				return;
			}
			fdc->status |= HEADLOAD;
			wd17xx_check_density(fdc);
		}
		break;
	case 0x40:	/* step in */
	case 0x50:
		/* We really need to keep track of true head and logical
		   head position TODO */
		if (track < 128)
			fdc->track++;
		fdc->status |= INDEX;
		if (v & 0x08) {
			if (fdc->side >= fdc->sides[fdc->drive]) {
				fdc->status |= RECNFERR;
				return;
			}
			fdc->status |= HEADLOAD;
			wd17xx_check_density(fdc);
		}
		fdc->stepdir = 1;
		wd17xx_motor(fdc, motor);
		break;
	case 0x60:	/* step out */
	case 0x70:
		if (fdc->track)
			fdc->track--;
		fdc->status = INDEX;
		if (fdc->track == 0)
			fdc->status |= TRACK0;
		if (v & 0x08) {
			if (fdc->side >= fdc->sides[fdc->drive]) {
				fdc->status = SEEKERR;
				fdc->intrq = 1;
				return;
			}
			fdc->status |= HEADLOAD;
			wd17xx_check_density(fdc);
		}
		fdc->stepdir = -1;
		wd17xx_motor(fdc, motor);
		break;
	case 0x80:	/* Read sector */
		if (track >= fdc->tracks[fdc->drive] ||
			fdc->sector - fdc->sector0[fdc->drive]
				>= fdc->spt[fdc->drive]) {
			if (fdc->trace)
				fprintf(stderr, "want track %d, max %d: want sector %d, spt %d s0 %d\n",
					track, fdc->tracks[fdc->drive], fdc->sector, fdc->spt[fdc->drive],
					fdc->sector0[fdc->drive]);
			fdc->status |= INDEX | RECNFERR;
			return;
		}
		if (fdc->side >= fdc->sides[fdc->drive]) {
			if (fdc->trace)
				fprintf(stderr, "want side %d max %d\n", fdc->side, fdc->sides[fdc->drive]);
			fdc->status |= INDEX | RECNFERR;
			return;
		}
		wd17xx_side_control(fdc, v);
		wd17xx_diskseek(fdc);
		fdc->rd = 1;
		if (read(fdc->fd[fdc->drive], fdc->buf, size) != size) {
			perror("wd17xx: read: ");
			fprintf(stderr, "wd17xx: I/O error.\n");
			fdc->status |= RECNFERR;
			fdc->intrq = 1;
			return;
		}
		fdc->rdsize = size;
		fdc->status |= DRQ;
		fdc->busy = 0;
#if 0
		if (track == 20)
			fdc->status |= 0x20;	/* HACK for DDAM */
#endif
		wd17xx_check_density(fdc);
		wd17xx_motor(fdc, motor);
		break;
	case 0xA0:	/* Write sector */
		if (track >= fdc->tracks[fdc->drive] ||
			fdc->sector - fdc->sector0[fdc->drive]
				>= fdc->spt[fdc->drive]) {
			fdc->status |= RECNFERR;
			return;
		}
		if (fdc->side >= fdc->sides[fdc->drive]) {
			fdc->status |= RECNFERR;
			return;
		}
		fdc->status |= DRQ;
		fdc->busy = 0;
		fdc->wr = 1;
		wd17xx_motor(fdc, motor);
		wd17xx_side_control(fdc, v);
		wd17xx_check_density(fdc);
		break;
	case 0xC0:	/* read address */
		wd17xx_side_control(fdc, v);
		if (fdc->side >= fdc->sides[fdc->drive]) {
			fdc->status |= RECNFERR;
			return;
		}
		fdc->status |= DRQ;
		fdc->busy = 0;

		fdc->rd = 1;
		fdc->rdsize = 7;

		/* If we tried to seek off the end of the disk then
		   we'll stop at the end track and see the data there */

		/* This differs between 1772 and later devices. 1772 does not have the first
		   byte it seems */
		fdc->buf[0] = 0x00;	/* Junk byte ? FIXME what goes here */

		fdc->buf[1] = track;
		if (fdc->track >= fdc->tracks[fdc->drive]) {
			fdc->buf[1] = fdc->tracks[fdc->drive] - 1;
		}
		fdc->buf[2] = fdc->side;
		fdc->buf[3] = fdc->sector0[fdc->drive];
		switch(fdc->secsize[fdc->drive]) {
		case 128:
			fdc->buf[4] = 0x00;
			break;
		case 256:
			fdc->buf[4] = 0x01;
			break;
		case 512:
			fdc->buf[4] = 0x02;
			break;
		case 1024:
			fdc->buf[4] = 0x03;
			break;
		}
		fdc->buf[5] = 0xFA;
		fdc->buf[6] = 0xAF;
		if (fdc->type == 1772)
			fdc->pos = 1;
		/* Hardware weirdness */
		fdc->sector = fdc->track;
		fdc->intrq = 1;
		/* busy handling ?? */
//		fdc->status &= ~BUSY;	/* ?? need to know what real chip does */
		wd17xx_motor(fdc, 1);
		fdc->busy = 64;
		wd17xx_check_density(fdc);
		break;
	case 0xD0:	/* Force interrupt : handled above */
		break;
	case 0x90:	/* read multi */
	case 0xB0:	/* write multi */
	case 0xE0:	/* read track */
	case 0xF0:	/* write track */
	default:
		fprintf(stderr, "wd17xx: unemulated command %02X.\n", v);
		fdc->status &= ~DRQ;
		fdc->rd = 0;
		fdc->wr = 0;
		break;
	}
}

uint8_t wd17xx_status(struct wd17xx *fdc)
{
	if (fdc->trace)
		fprintf(stderr, "fdc%d: status %x.\n", fdc->drive, fdc->status);
	fdc->intrq = 0;
	if (fdc->busy) {
		fdc->busy--;
		if (!fdc->busy) {
			fdc->status &= ~BUSY;
			fdc->intrq = 1;
		}
	}
	/* On the 1793 0x80 is high when the drive is not ready
	   On the 1772 it means motor on so is inverted */
	if (fdc->type == 1772)
		return fdc->status | (fdc->motor ? 0x80 : 0x00);
	else	/* Treat not ready as motor off - really it lags TODO */
		return fdc->status | (fdc->motor ? 0x00 : 0x80);
}

uint8_t wd17xx_status_noclear(struct wd17xx *fdc)
{
	if (fdc->busy) {
		fdc->busy--;
		if (!fdc->busy) {
			fdc->status &= ~BUSY;
			fdc->intrq = 1;
			if (fdc->trace)
				fprintf(stderr, "INTRQ goes high.\n");
		}
	}
	return fdc->status | (fdc->motor ? 0x80 : 0x00);
}

struct wd17xx *wd17xx_create(unsigned type)
{
	struct wd17xx *fdc = malloc(sizeof(struct wd17xx));
	memset(fdc, 0, sizeof(*fdc));
	fdc->fd[0] = -1;
	fdc->fd[1] = -1;
	fdc->fd[2] = -1;
	fdc->fd[3] = -1;
	fdc->sector0[0] = 1;
	fdc->sector0[1] = 1;
	fdc->sector0[2] = 1;
	fdc->sector0[3] = 1;
	fdc->type = type;
	fdc->motor_timeout = 10000;	/* 10 seconds */
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

void wd17xx_set_sector0(struct wd17xx *fdc, unsigned drive, unsigned offset)
{
	fdc->sector0[drive] = offset;
}

void wd17xx_set_side1(struct wd17xx *fdc, unsigned drive, unsigned offset)
{
	fdc->side1[drive] = offset;
}

uint8_t wd17xx_intrq(struct wd17xx *fdc)
{
	return fdc->intrq;
}

void wd17xx_set_density(struct wd17xx *fdc, unsigned den)
{
	fdc->density = den;
}

void wd17xx_set_media_density(struct wd17xx *fdc, unsigned drive, unsigned den)
{
	fdc->diskden[drive] = den;
}
