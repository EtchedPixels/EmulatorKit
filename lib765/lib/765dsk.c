/* 765: Library to emulate the uPD765a floppy controller (aka Intel 8272)

    Copyright (C) 2000  John Elliott <jce@seasip.demon.co.uk>

    Modifications to add dirty flags
    (c) 2005 Philip Kendall <pak21-spectrum@srcf.ucam.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
#include "765i.h"

#define SHORT_TIMEOUT	1000
#define LONGER_TIMEOUT  1333333L

/* Get the status of a DSK file. In fact this routine does not depend on 
 * the drive being a DSK file and could be used by other drive types. */

fdc_byte fdd_drive_status(FLOPPY_DRIVE *fd)
{
	fdc_byte v = 0;

	/* 5.25" drives don't report read-only when they're not ready */
	if (fd->fd_type == FD_525)
	{
		if (fd_isready(fd))
		{
			v |= 0x20;
			if (fd->fd_readonly) v |= 0x40;
		}
	}
	else
	/* 3" and 3.5" drives always report read-only when not ready */
	{
		if (fd_isready(fd))  v |= 0x20;
		else		     v |= 0x40;
		if (fd->fd_readonly) v |= 0x40;
	}

	if (fd->fd_cylinder == 0  ) v |= 0x10;	/* Track 0   */

	if (fd->fd_type == FD_35)		/* 3.5" does not give track 0
					         * if motor is off */
	{
		if (! fd->fd_motor ) v &= ~0x10;
	}
	if (fd->fd_heads > 1) v |= 0x08;	/* Double sided */
	return v;
}


/* Reset variables: No DSK loaded. Called on eject and on initialisation */
static void fdd_reset(FLOPPY_DRIVE *fd)
{
	DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

        fdd->fdd_filename[0] = 0;
        fdd->fdd_fp = NULL;
        memset(fdd->fdd_disk_header,  0, sizeof(fdd->fdd_disk_header));
        memset(fdd->fdd_track_header, 0, sizeof(fdd->fdd_track_header));
}



/* Return 1 if this drive is ready, else 0
 * Attempts to open the DSK and load its DSK header, and must
 * therefore be called before any attempted DSK file access. */
static int fdd_isready(FLOPPY_DRIVE *fd)
{
	DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

	if (!fd->fd_motor) return 0;	/* Motor is not running */

	if (fdd->fdd_fp) return 1;		 /* DSK file is open and OK */	
	if (fdd->fdd_filename[0] == 0) return 0; /* No filename */

	fdd->fdd_fp = fopen(fdd->fdd_filename, "r+b");
	if (!fdd->fdd_fp)
	{
		fdd->fdd_fp = fopen(fdd->fdd_filename, "rb");
		if (fdd->fdd_fp)
		{
			fd->fd_readonly = 1;	/* Read-only drive */
			fdc_dprintf(0, "Could only open %s read-only.\n", 
					fdd->fdd_filename);
		}
		else fdc_dprintf(0, "Could not open %s.\n", fdd->fdd_filename);
	}
	if (!fdd->fdd_fp) 
	{
		fdd_reset(fd);
		return 0;
	}
/* File has been newly opened. Read in its header */
	fseek(fdd->fdd_fp, 0, SEEK_SET);
	if (fread(fdd->fdd_disk_header, 1, 256, fdd->fdd_fp) < 256)
	{
		fdc_dprintf(0, "Could not load DSK file header: %s\n", 
				fdd->fdd_filename);
		fdd_reset(fd);
		return 0;	
	}
	if (memcmp("MV - CPC", fdd->fdd_disk_header, 8) &&
	    memcmp("EXTENDED", fdd->fdd_disk_header, 8)) 
	{
		fdc_dprintf(0, "File %s is not in DSK or extended DSK format\n",
				fdd->fdd_filename);
		fdd_reset(fd);
		return 0;
	} 
/* File loaded OK. */
	fdd->fdd_track_header[0] = 0;	/* Track header not loaded */
	
        return 1;
}

/* Find the offset in a DSK for a particular cylinder/head. 
 *
 * CPCEMU DSK files work in "tracks". For a single-sided disk, track number
 * is the same as cylinder number. For a double-sided disk, track number is
 * (2 * cylinder + head). This is independent of disc format.
 */
static long fdd_lookup_track(DSK_FLOPPY_DRIVE *fdd, int cylinder, int head)
{
	fdc_byte *b;
	int track;
	long trk_offset;
	int nt;
	if (!fdd->fdd_fp) return -1;

	/* Seek off the edge of the drive */
	if (cylinder >  fdd->fdd.fd_cylinders) return -1;
	if (head     >= fdd->fdd.fd_heads)     return -1;

	/* Support for double-stepping. If this is a 3" or 5.25" drive, and *
         * the DSK has <44 tracks, and the drive has >= 80 tracks, then     *
         * when asked for cylinder (n) we actually go to cylinder (n/2).    */ 

	if ((fdd->fdd.fd_type == FD_30 || fdd->fdd.fd_type == FD_525) &&
	    fdd->fdd_disk_header[0x30] > 43 && fdd->fdd.fd_cylinders >= 80)
	{
	        fprintf(stderr, "Double stepping\n");
		cylinder /= 2;	/* Simulate double-stepping */
	}

	/* Convert cylinder & head to CPCEMU "track" */

	track = cylinder;
	if (fdd->fdd_disk_header[0x31] > 1) track *= 2;
	track += head;

        /* Look up the cylinder and head using the header. This behaves 
         * differently in normal and extended DSK files */
	
	if (!memcmp(fdd->fdd_disk_header, "EXTENDED", 8))
	{
		trk_offset = 256;	/* DSK header = 256 bytes */
		b = fdd->fdd_disk_header + 0x34;
		for (nt = 0; nt < track; nt++)
		{
			trk_offset += 256 * (1 + b[nt]);
		}
	}
	else	/* Normal; all tracks have the same length */
	{
		trk_offset = (fdd->fdd_disk_header[0x33] * 256);
		trk_offset += fdd->fdd_disk_header[0x32];

		trk_offset *= track;		/* No. of tracks */
		trk_offset += 256;		/* DSK header */	
	}
	return trk_offset;
}


static unsigned char *sector_head(DSK_FLOPPY_DRIVE *fdd, int sector)
{
        int ms = fdd->fdd_track_header[0x15];
        int sec;

        for (sec = 0; sec < ms; sec++)
        {
                if (fdd->fdd_track_header[0x1A + 8 * sec] == sector)
                        return fdd->fdd_track_header + 0x18 + 8 * sec;
        }
        return NULL;
}



/* Seek to a cylinder. Checks if that particular cylinder exists. 
 * We test for the existence of a cylinder by looking for Track <n>, Head 0.
 * Fortunately the DSK format does not allow for discs with different numbers
 * of tracks on each side (though this is obviously possible with a real disc)
 * so if head 0 exists then the whole cylinder does. */


static fd_err_t fdd_seek_cylinder(FLOPPY_DRIVE *fd, int cylinder)
{
	int req_cyl = cylinder;
	long nr;
        DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

	fdc_dprintf(4, "fdd_seek_cylinder: cylinder=%d\n",cylinder);

	if (!fdd->fdd_fp) return FD_E_NOTRDY;

	fdc_dprintf(6, "fdd_seek_cylinder: DSK file open OK\n");

	/* Check if the DSK image goes out to the correct cylinder */
	nr = fdd_lookup_track(fdd, cylinder, 0);
	
	if (nr < 0) return FD_E_SEEKFAIL;

	fdc_dprintf(6, "fdd_seek_cylinder: OK\n");

	fd->fd_cylinder = req_cyl;	
	return 0;
}

/* Load the "Track-Info" header for the current cylinder and given head */
static fd_err_t fdd_load_track_header(DSK_FLOPPY_DRIVE *fdd, int head)
{
        long track = fdd_lookup_track(fdd, fdd->fdd.fd_cylinder, head);
        if (track < 0) return FD_E_SEEKFAIL;       /* Bad track */
        fseek(fdd->fdd_fp, track, SEEK_SET);
        if (fread(fdd->fdd_track_header, 1, 256, fdd->fdd_fp) < 256)
                return FD_E_NOADDR;              /* Missing address mark */
        if (memcmp(fdd->fdd_track_header, "Track-Info", 10))
        {
                fdc_dprintf(0, "FDC: Did not find track %d header at 0x%lx in %s\n",
                        fdd->fdd.fd_cylinder, track, fdd->fdd_filename);
                return FD_E_NOADDR;
        }
	return 0;
}


/* Read a sector ID from the current track */
static fd_err_t fdd_read_id(FLOPPY_DRIVE *fd, int head, int sector, fdc_byte *buf)
{
	DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;
	int n, offs;

	n = fdd_load_track_header(fdd, head);
	if (n < 0) return n;

	/* Offset of the chosen sector header */
	offs = 0x18 + 8 * (sector % fdd->fdd_track_header[0x15]);	

	for (n = 0; n < 4; n++) buf[n] = fdd->fdd_track_header[offs+n];
	return 0;	
}


/* Find the offset of a sector in the current track 
 * Enter with fdd_track_header loaded and the file pointer 
 * just after it (ie, you have just called fdd_load_track_header() ) */

static long fdd_sector_offset(DSK_FLOPPY_DRIVE *fdd, int sector, int *seclen,
			      fdc_byte **secid)
{
	int maxsec = fdd->fdd_track_header[0x15];
	long offset = 0;
	int n;

	/* Pointer to sector details */
	*secid = fdd->fdd_track_header + 0x18;

	/* Length of sector */	
	*seclen = (0x80 << fdd->fdd_track_header[0x14]);

	/* Extended DSKs have individual sector sizes */
	if (!memcmp(fdd->fdd_disk_header, "EXTENDED", 8))
	{
		for (n = 0; n < maxsec; n++)
		{
			*seclen = (*secid)[7] + 256 * (*secid)[8];
                       if ((*secid)[2] == sector) return offset;
			offset   += (*seclen);
			(*secid) += 8;
		}
	}
	else	/* Non-extended, all sector sizes are the same */
	{
		for (n = 0; n < maxsec; n++)
		{
			if ((*secid)[2] == sector) return offset;
			offset   += (*seclen);
			(*secid) += 8;
		}
	}
	return -1;	/* Sector not found */
}



/* Seek within the DSK file to a given head & sector in the current cylinder.
 * Then check that "xhead" and "xcylinder" match the sector's ID fields */
static fd_err_t fdd_seekto_sector(FLOPPY_DRIVE *fd, int xcylinder, int xhead,
		int head, int sector, fdc_byte *buf, int *len)
{
        DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;
        int n, offs, seclen;
	fd_err_t err = FD_E_OK;
	fdc_byte *secid;

        n = fdd_load_track_header(fdd, head);
        if (n < 0) return n;
	offs = fdd_sector_offset(fdd, sector, &seclen, &secid);
	if (offs < 0) return FD_E_NOSECTOR;	/* Sector not found */

	if (xcylinder != secid[0] || xhead != secid[1])
	{
		fdc_dprintf(0, "FDC: Looking for cyl=%d head=%d but found "
		    "cyl=%d head=%d\n", xcylinder, xhead, secid[0], secid[1]);
		return FD_E_NOSECTOR;
	}
	if (seclen < *len) 
	{
		err = FD_E_DATAERR; 
		*len = seclen;
	}
	else if (seclen > *len)
	{
		err = FD_E_DATAERR;
		seclen = *len;
	}	
	offs += ftell(fdd->fdd_fp);	
	fseek(fdd->fdd_fp, offs, SEEK_SET);				
	return err;			
}


/* Read a sector */
static fd_err_t fdd_read_sector(FLOPPY_DRIVE *fd, int xcylinder, int xhead, 
		int head,  int sector, fdc_byte *buf, int len, 
		int *deleted, int skip_deleted, int mfm, int multi)
{
        int rdeleted = 0;
        int try_again = 0;
        DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;
	unsigned char *sh;
	fd_err_t err;

	fdc_dprintf(4, "fdd_read_sector: Expected cyl=%d head=%d sector=%d\n",
			xcylinder, xhead, sector);
        if (deleted && *deleted) rdeleted = 0x40;
	
	do
	{
		err  = fdd_seekto_sector(fd,xcylinder,xhead,head,
							sector,buf,&len);
/* Are we retrying because we are looking for deleted data and found 
 * nondeleted or vice versa?
 *
 * Strictly speaking, we should allow for multitrack mode (like libdsk 
 * does in drvcpcem.c) and search the rest of the cylinder. But lib765
 * doesn't claim to support multitrack mode, so I won't do it.
 */ 
                if (try_again == 1 && err == FD_E_NOADDR)
                {
                        err = FD_E_NODATA;
                }
		try_again = 0;
		if (err != FD_E_DATAERR && err != FD_E_OK) return err;
/* Check if the sector contains deleted data rather than nondeleted */
                sh = sector_head(fdd, sector);
                if (!sh) return FD_E_NODATA;
                *deleted = 0;
                if (rdeleted != (sh[5] & 0x40)) /* Mismatch! */
                {
                        if (skip_deleted) 
                        {
/* Try the next sector. */
                                try_again = 1;
                                ++sector;
                                continue;
                        }
			else *deleted = 1;
                }
		if (fread(buf, 1, len, ((DSK_FLOPPY_DRIVE *)fd)->fdd_fp) < len) 
			err = FD_E_DATAERR;
	} while (try_again);
	return err;
}		

/* Read a track */
static fd_err_t fdd_read_track(FLOPPY_DRIVE *fd, int xcylinder, int xhead,
                int head,  fdc_byte *buf, int *len)
{
        DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;
        int n, trklen;
        fd_err_t err = FD_E_OK;

        fdc_dprintf(4, "fdd_read_track: Expected cyl=%d head=%d\n",
                        xcylinder, xhead);

        n = fdd_load_track_header(fdd, head);
        if (n < 0) return n;

        if (xcylinder != fdd->fdd_track_header[0x18] || 
	    xhead     != fdd->fdd_track_header[0x19])
        {
                fdc_dprintf(0, "FDC: Looking for cyl=%d head=%d but found "
                    "cyl=%d head=%d\n", xcylinder, xhead, 
		    fdd->fdd_track_header[0x18], fdd->fdd_track_header[0x19]);
                return FD_E_NOSECTOR;
        }
        if (!memcmp(fdd->fdd_disk_header, "EXTENDED", 8))
        {
		trklen = fdd->fdd_disk_header[0x34 + 
		(fdd->fdd_track_header[0x10] * fdd->fdd_disk_header[0x31]) +
		 fdd->fdd_track_header[0x11]];
              
		trklen *= 256; 
        }
        else    /* Normal; all tracks have the same length */
        {
                trklen = (fdd->fdd_disk_header[0x33] * 256);
                trklen += fdd->fdd_disk_header[0x32];
        }
	if (trklen > *len) 
        {
                err = FD_E_DATAERR;
		trklen = (*len);	
	}

        if (err == FD_E_DATAERR || err == FD_E_OK)
        {
                if (fread(buf, 1, trklen, fdd->fdd_fp) < (*len))
			err = FD_E_DATAERR;
        }
        return err;
}



/* Write a sector */
static fd_err_t fdd_write_sector(FLOPPY_DRIVE *fd, int xcylinder, int xhead,
			int head, int sector, fdc_byte *buf, int len, 
			int deleted, int skip_deleted, int mfm, int multi)
{
	fd_err_t err;
	DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

        fdc_dprintf(4, "fdd_write_sector: Expected cyl=%d head=%d sector=%d\n",
                        xcylinder, xhead, sector);

	err = fdd_seekto_sector(fd,xcylinder,xhead,head,sector,buf,
						&len);

	if (fd->fd_readonly) return FD_E_READONLY;
	if (err == FD_E_DATAERR || err == 0)
	{
                unsigned char odel, *sh = sector_head(fdd, sector);
		if (fwrite(buf, 1, len, fdd->fdd_fp) < len)
			err = FD_E_READONLY;
		fdd->fdd_dirty = 1;

/* If writing deleted data, update the sector header accordingly */
                odel = sh[5];
                if (deleted) sh[5] |= 0x40;
                else         sh[5] &= ~0x40;

                if (sh[5] != odel)
                {
                        long track = fdd_lookup_track(fdd, fd->fd_cylinder, head);
                        if (track < 0) return FD_E_SEEKFAIL;       /* Bad track */
                        fseek(fdd->fdd_fp, track, SEEK_SET);
                        if (fwrite(fdd->fdd_track_header, 1, 256, fdd->fdd_fp) < 256)
                                return FD_E_DATAERR; 
                }

	}
	return err;
}


/* Format a track on a DSK. Can grow the DSK file. */
static fd_err_t fdd_format_track(FLOPPY_DRIVE *fd, int head,
                int sectors, fdc_byte *track, fdc_byte filler)
{
	DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;
	int n, img_trklen, trklen, trkoff, trkno, ext, seclen;
	fdc_byte oldhead[256];     

        fdc_dprintf(4, "fdd_format_track: head=%d sectors=%d\n",
                        head, sectors); 

 
	if (!fdd->fdd_fp) return FD_E_NOTRDY;
	if (fd->fd_readonly) return FD_E_READONLY;
	ext = 0;
	memcpy(oldhead, fdd->fdd_disk_header, 256);
	
/* 1. Only if the DSK has either (1 track & 1 head) or (2 heads) can we 
 *   format the second head
 */
	if (head)
	{
		if (fdd->fdd_disk_header[0x31] == 1 && 
		    fdd->fdd_disk_header[0x30] > 1) return FD_E_READONLY;

		if (fdd->fdd_disk_header[0x31] == 1) 
			fdd->fdd_disk_header[0x31] = 2;
	}
/* 2. Find out the CPCEMU number of the new cylinder/head */

	if (fdd->fdd_disk_header[0x31] < 1) fdd->fdd_disk_header[0x31] = 1;
	trkno = fd->fd_cylinder;
	trkno *= fdd->fdd_disk_header[0x31];
	trkno += head;

	printf("fdc_format: %d, %d -> %d [%d]\n", fd->fd_cylinder, head, trkno,
		sectors);

/* 3. Find out how long the proposed new track is
 *
 * nb: All sizes *include* track header
 */
	trklen = 0;
	for (n = 0; n < sectors; n++)
	{
		trklen += (128 << (track[4 * n + 3]));
		printf("%02x %02x %02x %02x\n",
			track[4*n], track[4*n+1], track[4*n+2], track[4*n+3]);	
	}
	trklen += 256;	/* For header */
	printf("fdc_format: trklen = %d\n", trklen);
/* 4. Work out if this length is suitable
 */
	if (!memcmp(fdd->fdd_disk_header, "EXTENDED", 8))
	{
		fdc_byte *b;
		/* For an extended DSK, work as follows: 
		 * If the track is reformatting an existing one, 
		 * the length must be <= what's there. 
		 * If the track is new, it must be contiguous with the 
		 * others */

		ext = 1;
		img_trklen = (fdd->fdd_disk_header[0x34 + trkno] * 256) + 256;
		if (img_trklen)
		{
			if (trklen > img_trklen) return FD_E_READONLY;
		}
		else if (trkno > 0) 
		{
			if (!fdd->fdd_disk_header[0x34 + trkno - 1]) 
			{
				memcpy(fdd->fdd_disk_header, oldhead, 256);
				return FD_E_READONLY;
			}
		}
		/* Work out where the track should be. */
                b = fdd->fdd_disk_header + 0x34;
       		trkoff = 256; 
	        for (n = 0; n < trkno; n++)
                {
                        trkoff += 256 * (1 + b[n]);
                }
		/* Store the length of the new track */
		if (!b[n]) b[n] = (trklen >> 8) - 1;
	}
	else
	{
		img_trklen = fdd->fdd_disk_header[0x32] + 256 * 
			     fdd->fdd_disk_header[0x33];
		/* If no tracks formatted, or just the one track, length can
                 * be what we like */
		if ( (fdd->fdd_disk_header[0x30] == 0) ||
		     (fdd->fdd_disk_header[0x30] == 1 && 
                      fdd->fdd_disk_header[0x31] == 1) )
		{
			if (trklen > img_trklen)
			{
				fdd->fdd_disk_header[0x32] = trklen & 0xFF;
				fdd->fdd_disk_header[0x33] = (trklen >> 8);
				img_trklen = trklen;	
			}
		}
		if (trklen > img_trklen)
		{
			memcpy(fdd->fdd_disk_header, oldhead, 256);
			return FD_E_READONLY;
		}
		trkoff = 256 + (trkno * img_trklen);
	}
	printf("trklen=%x trkno=%d img_trklen=%x trkoff=%x\n", 
		trklen, trkno, img_trklen, trkoff);
/* Seek to the track. Note: We do NOT double-step while formatting, because
 * we can't tell between a DSK with 40 tracks that's finished, and one with
 * 40 tracks that will grow to 80 tracks */
	fseek(fdd->fdd_fp, trkoff, SEEK_SET);
	/* Now generate and write a Track-Info buffer */
	memset(fdd->fdd_track_header, 0, sizeof(fdd->fdd_track_header));

	strcpy((char *)fdd->fdd_track_header, "Track-Info\r\n");	
	
	fdd->fdd_track_header[0x10] = fd->fd_cylinder;
	fdd->fdd_track_header[0x11] = head;
	fdd->fdd_track_header[0x14] = track[3];
	fdd->fdd_track_header[0x15] = sectors;
	fdd->fdd_track_header[0x16] = track[2];
	fdd->fdd_track_header[0x17] = filler;
	for (n = 0; n < sectors; n++)
	{
		fdd->fdd_track_header[0x18 + 8*n] = track[4*n];
		fdd->fdd_track_header[0x19 + 8*n] = track[4*n+1];
		fdd->fdd_track_header[0x1A + 8*n] = track[4*n+2];
		fdd->fdd_track_header[0x1B + 8*n] = track[4*n+3];
		if (ext)
		{
			seclen = 128 << track[4 * n + 3];
			fdd->fdd_track_header[0x1E + 8 * n] = seclen & 0xFF;
			fdd->fdd_track_header[0x1F + 8 * n] = seclen >> 8;
		}
	}
	if (fwrite(fdd->fdd_track_header, 1, 256, fdd->fdd_fp) < 256)
	{
		memcpy(fdd->fdd_disk_header, oldhead, 256);
		return FD_E_READONLY;
	}
	fdd->fdd_dirty = 1;

	/* Track header written. Write sectors */
	for (n = 0; n < sectors; n++)
	{
		int m;
		seclen = 128 << track[4 * n + 3];
		for (m = 0; m < seclen; m++)
		{
			if (fputc(filler, fdd->fdd_fp) == EOF) 
			{
				memcpy(fdd->fdd_disk_header, oldhead, 256);
				return FD_E_READONLY;
			}
		}
	}
	if (fd->fd_cylinder >= fdd->fdd_disk_header[0x30])
	{
		fdd->fdd_disk_header[0x30] = fd->fd_cylinder + 1;
	}
	/* Track formatted OK. Now write back the modified DSK header */
	fseek(fdd->fdd_fp, 0, SEEK_SET);
	if (fwrite(fdd->fdd_disk_header, 1, 256, fdd->fdd_fp) < 256)
	{
		memcpy(fdd->fdd_disk_header, oldhead, 256);
		return FD_E_READONLY;
	}
	return FD_E_OK;
}

static int fdd_dirty(FLOPPY_DRIVE *fd)
{
	DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

	return fdd->fdd_dirty ? FD_D_DIRTY : FD_D_CLEAN;
}

/* Eject a DSK - close the image file */
static void fdd_eject(FLOPPY_DRIVE *fd)
{
        DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

	if (fdd->fdd_fp) fclose(fdd->fdd_fp);

	fdd_reset(fd);
}


static FLOPPY_DRIVE_VTABLE fdv_dsk = 
{
	fdd_seek_cylinder,
	fdd_read_id,
	fdd_read_sector,
	fdd_read_track,
	fdd_write_sector,
	fdd_format_track,
	fdd_drive_status,
	fdd_isready,
	fdd_dirty,
	fdd_eject,
	NULL,
	fdd_reset
};

/* Initialise a DSK-based drive */
FDRV_PTR fd_newdsk(void)
{
	FDRV_PTR p = fd_inew(sizeof(DSK_FLOPPY_DRIVE));

	p->fd_vtable = &fdv_dsk;
	fd_reset(p);
	return p;
}

	


/* Create a new DSK file. Not necessary for emulation but well worth having */
fd_err_t fdd_new_dsk(FLOPPY_DRIVE *fd)
{
        FILE *fp;
        int err;
	DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

        fp = fopen(fdd->fdd_filename, "wb");
        if (!fp)
        {
                fdc_dprintf(0, "Cannot open %s\n", fdd->fdd_filename);
                return 0;
        }

	/* XXX Currently only creates the normal sort of DSK */
        memset(fdd->fdd_disk_header, 0, 256);
        strcpy((char *)fdd->fdd_disk_header,
		"MV - CPCEMU Disk-File\r\nDisk-Info\r\n(JOYCE)");
        err = fwrite(fdd->fdd_disk_header, 1 , 256, fp);
        fclose(fp);
        return (err == 256);
}


/* Get / set DSK file associated with this drive.
 *  * Note that doing fdd_setfilename() causes an implicit eject on the 
 *   * previous disc in the drive. */
char *   fdd_getfilename(FDRV_PTR fd)
{
        if (fd->fd_vtable == &fdv_dsk)
        {
                return ((DSK_FLOPPY_DRIVE *)fd)->fdd_filename;
        }
	return "Called fdd_getfilename() on wrong drive type";
}

void     fdd_setfilename(FDRV_PTR fd, const char *s)
{
        if (fd->fd_vtable == &fdv_dsk)
        {
		DSK_FLOPPY_DRIVE *fdd = (DSK_FLOPPY_DRIVE *)fd;

		fd_eject(fd);
		strncpy(fdd->fdd_filename, s, sizeof(fdd->fdd_filename) - 1);
		fdd->fdd_filename[sizeof(fdd->fdd_filename) - 1] = 0;
		fdd->fdd_dirty = 0;
        }
}

