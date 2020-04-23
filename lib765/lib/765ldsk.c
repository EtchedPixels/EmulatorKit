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
#include "config.h"
#ifdef HAVE_LIBDSK_H
#include <stdio.h>
#include "libdsk.h"
#include "765i.h"

#define SHORT_TIMEOUT	1000
#define LONGER_TIMEOUT  1333333L

extern fdc_byte fdd_drive_status(FLOPPY_DRIVE *fd);


/* Reset variables: No DSK loaded. Called on eject and on initialisation */
void fdl_reset(FLOPPY_DRIVE *fd)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
        fdl->fdl_filename[0] = 0;	
	fdl->fdl_type = NULL;
	fdl->fdl_compress = NULL;
        fdl->fdl_diskp = NULL;
}


static int fdl_regeom(LIBDSK_FLOPPY_DRIVE *fdl)
{
	dsk_err_t err;

	err = dsk_getgeom(fdl->fdl_diskp, &fdl->fdl_diskg);
	// Note that some errors (which could result from 
	// unformatted discs) are ignored here.
	if (err && 
	    err != DSK_ERR_NOADDR && 
	    err != DSK_ERR_NODATA &&
	    err != DSK_ERR_BADFMT)
	{
		fdc_dprintf(0, "Could not get geometry for %s: %s.\n", 
			fdl->fdl_filename, dsk_strerror(err));
		fdl_reset(&fdl->fdl);
		return err;
	}
        return 0;
}


/* Return 1 if this drive is ready, else 0
 * Attempts to open the DSK and load its DSK header, and must
 * therefore be called before any attempted DSK file access. */
static int fdl_isready(FLOPPY_DRIVE *fd)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	dsk_err_t err;

	if (!fd->fd_motor) return 0;	/* Motor is not running */

	if (fdl->fdl_diskp) return 1;		 /* DSK file is open and OK */	
	if (fdl->fdl_filename[0] == 0) return 0; /* No filename */

	err = dsk_open(&fdl->fdl_diskp, fdl->fdl_filename, fdl->fdl_type, 
			fdl->fdl_compress);

	if (err || !fdl->fdl_diskp)
	{
		fdc_dprintf(0, "Could not open %s: %s.\n", 
			fdl->fdl_filename, dsk_strerror(err));
		fdl_reset(fd);
		return 0;
	}
	return (fdl_regeom(fdl) == 0);
}


static fd_err_t fdl_xlt_error(dsk_err_t err)
{
	switch(err)
	{
		case DSK_ERR_OK:	return FD_E_OK;
		case DSK_ERR_SEEKFAIL:	return FD_E_SEEKFAIL;
		case DSK_ERR_NOADDR:    return FD_E_NOADDR;
		case DSK_ERR_NODATA:    return FD_E_NODATA;
		case DSK_ERR_DATAERR:   return FD_E_DATAERR;
		case DSK_ERR_NOTRDY:    return FD_E_NOTRDY;
		case DSK_ERR_RDONLY:    return FD_E_READONLY;
	}	
	// unknown error
	return FD_E_NOSECTOR;
}

/* Seek to a cylinder. */

static fd_err_t fdl_seek_cylinder(FLOPPY_DRIVE *fd, int cylinder)
{
	int req_cyl = cylinder;
	dsk_err_t err;
        LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;

	fdc_dprintf(4, "fdl_seek_cylinder: cylinder=%d\n",cylinder);

	if (!fdl->fdl_diskp) return FD_E_NOTRDY;

	fdc_dprintf(6, "fdl_seek_cylinder: image open OK\n");

	err = dsk_pseek(fdl->fdl_diskp, &fdl->fdl_diskg, cylinder, 0);
	if (err == DSK_ERR_NOTIMPL || err == DSK_ERR_OK)
	{
		fdc_dprintf(6, "fdl_seek_cylinder: OK\n");
		fd->fd_cylinder = req_cyl;	
		return 0;
	}
	fdc_dprintf(6, "fdl_seek_cylinder: fails, LIBDSK error %d\n", err);
	/* Check if the DSK image goes out to the correct cylinder */
	return fdl_xlt_error(err);
}

/* Read a sector ID from the current track */
static fd_err_t fdl_read_id(FLOPPY_DRIVE *fd, int head, int sector, fdc_byte *buf)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	dsk_err_t err;
	DSK_FORMAT fmt;

	fdc_dprintf(4, "fdl_read_id: head=%d\n", head);
	if (!fdl->fdl_diskp) return FD_E_NOTRDY;
	err = dsk_psecid(fdl->fdl_diskp, &fdl->fdl_diskg, fd->fd_cylinder,
			 head, &fmt);
	if (err == DSK_ERR_NOTIMPL)
	{
		buf[0] = fd->fd_cylinder;
		buf[1] = head;
		buf[2] = sector;
		buf[3] = dsk_get_psh(fdl->fdl_diskg.dg_secsize);	
		return 0;
	}
	if (err) return fdl_xlt_error(err);

	buf[0] = fmt.fmt_cylinder;
	buf[1] = fmt.fmt_head;
	buf[2] = fmt.fmt_sector;
	buf[3] = dsk_get_psh(fmt.fmt_secsize);
	return 0;	
}


/* Read a sector */
static fd_err_t fdl_read_sector(FLOPPY_DRIVE *fd, int xcylinder, int xhead, 
		int head,  int sector, fdc_byte *buf, int len, int *deleted,
		int skip_deleted, int mfm, int multi)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	dsk_err_t err;

	fdc_dprintf(4, "fdl_read_sector: cyl=%d xc=%d xh=%d h=%d s=%d len=%d\n", 
			fd->fd_cylinder, xcylinder, xhead, head, sector, len);
	if (!fdl->fdl_diskp) return FD_E_NOTRDY;

	fdl->fdl_diskg.dg_noskip  = skip_deleted ? 0 : 1;
	fdl->fdl_diskg.dg_fm      = mfm ? 0 : 1;
	fdl->fdl_diskg.dg_nomulti = multi ? 0 : 1;

	err = dsk_xread(fdl->fdl_diskp, &fdl->fdl_diskg, buf,
		fd->fd_cylinder, head, xcylinder, xhead, sector, len, deleted);

	if (err == DSK_ERR_NOTIMPL)
	{
/* lib765 v0.3.2: If 'deleted' is passed but points to zero, treat it as if
 * 'deleted' is not passed at all. */
		if (deleted && *deleted) return FD_E_NOADDR;
		err = dsk_pread(fdl->fdl_diskp, &fdl->fdl_diskg, buf,
			fd->fd_cylinder, head, sector);
	}	
	return fdl_xlt_error(err);
}		

/* Read a track */
static fd_err_t fdl_read_track(FLOPPY_DRIVE *fd, int xcylinder, int xhead,
                int head,  fdc_byte *buf, int *len)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	fd_err_t err; 

	// Use LIBDSK's track-reading abilities
	fdc_dprintf(4, "fdl_read_track: xc=%d xh=%d h=%d\n", 
			xcylinder, xhead, head);
	if (!fdl->fdl_diskp) return FD_E_NOTRDY;

	err = dsk_xtread(fdl->fdl_diskp, &fdl->fdl_diskg, buf,
			fd->fd_cylinder, head, xcylinder, xhead);
	return fdl_xlt_error(err);
}



/* Write a sector */
static fd_err_t fdl_write_sector(FLOPPY_DRIVE *fd, int xcylinder, int xhead, 
		int head,  int sector, fdc_byte *buf, int len, int deleted, 
		int skip_deleted, int mfm, int multi)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	dsk_err_t err;

	fdc_dprintf(4, "fdl_write_sector: xc=%d xh=%d h=%d s=%d\n", 
			xcylinder, xhead, head, sector);
	if (!fdl->fdl_diskp) return FD_E_NOTRDY;

	fdl->fdl_diskg.dg_noskip  = skip_deleted ? 0 : 1;
/* lib765 0.3.3: Oops. Get the FM/MFM flag round the right way. */
	fdl->fdl_diskg.dg_fm      = mfm ? 0 : 1;
	fdl->fdl_diskg.dg_nomulti = multi ? 0 : 1;
	err = dsk_xwrite(fdl->fdl_diskp, &fdl->fdl_diskg, buf,
		fd->fd_cylinder, head, xcylinder, xhead, sector, len, deleted);
	if (err == DSK_ERR_NOTIMPL)
	{
		if (deleted) return FD_E_NOADDR;
	
		err = dsk_pwrite(fdl->fdl_diskp, &fdl->fdl_diskg, buf,
			fd->fd_cylinder, head, sector);
	}	
	return fdl_xlt_error(err);
}		

/* Format a track on a DSK. Can grow the DSK file. */
static fd_err_t fdl_format_track(FLOPPY_DRIVE *fd, int head,
                int sectors, fdc_byte *track, fdc_byte filler)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	int n, os;
	dsk_err_t err;
	DSK_FORMAT *formbuf;
	
	fdc_dprintf(4, "fdl_format_track: cyl=%d h=%d s=%d\n", 
			fd->fd_cylinder, head, sectors);
	if (!fdl->fdl_diskp) return FD_E_NOTRDY;

	formbuf = malloc(sectors * sizeof(DSK_FORMAT));
	if (!formbuf) return FD_E_READONLY;

	for (n = 0; n < sectors; n++)
	{
		formbuf[n].fmt_cylinder = track[n * 4];
		formbuf[n].fmt_head     = track[n * 4 + 1];
		formbuf[n].fmt_sector   = track[n * 4 + 2];
		formbuf[n].fmt_secsize  = 128 << track[n * 4 + 3];
	}
	os = fdl->fdl_diskg.dg_sectors;
	fdl->fdl_diskg.dg_sectors = sectors;
	err = dsk_pformat(fdl->fdl_diskp, &fdl->fdl_diskg, fd->fd_cylinder,
			head, formbuf, filler);
	fdl->fdl_diskg.dg_sectors = os;

	free(formbuf);
	// 
	// If track 0 has been reformatted, try to redetermine the 
	// geometry.
	//
	if (fd->fd_cylinder == 0 && !fd->fd_cylinder) fdl_regeom(fdl);
	if (!err) 
	{
		return 0;
	}	
	return fdl_xlt_error(err);
}		

/* Has this floppy been written to since it was inserted? */
static int fdl_dirty(FLOPPY_DRIVE *fd)
{
	LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;

	if (fdl->fdl_diskp)
	{
		return dsk_dirty(fdl->fdl_diskp);
	}
	else
	{
		return FD_D_UNAVAILABLE;
	}
}

/* Eject a DSK - close the image file */
static void fdl_eject(FLOPPY_DRIVE *fd)
{
        LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;

	if (fdl->fdl_diskp) dsk_close(&fdl->fdl_diskp);

	fdl_reset(fd);
}


static fdc_byte fdl_drive_status(FLOPPY_DRIVE *fd)
{
        LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	fdc_byte st;
	dsk_err_t err;

        if (fdl->fdl_diskp)
	{
		err = dsk_drive_status(fdl->fdl_diskp, &fdl->fdl_diskg, 0, &st);
	}
	else 
	{
		st = 0;
		if (fdl_isready(fd)) st = DSK_ST3_READY;
	}

	/* 5.25" drives don't report read-only when they're not ready */
	if (fd->fd_type == FD_525)
	{
		if ((st & (DSK_ST3_RO | DSK_ST3_READY)) == DSK_ST3_RO)
		{
			st &= ~DSK_ST3_RO;
		}
	}
	else
	/* 3" and 3.5" drives always report read-only when not ready */
	{
		if (!(st & DSK_ST3_READY)) st |= DSK_ST3_RO;
		if (fd->fd_readonly)       st |= DSK_ST3_RO;
	}

	if (fd->fd_cylinder == 0  ) st |= DSK_ST3_TRACK0;  /* Track 0   */

	if (fd->fd_type == FD_35)		/* 3.5" does not give track 0
					         * if motor is off */
	{
		if (! fd->fd_motor ) st &= ~DSK_ST3_TRACK0;
	}
	if (fd->fd_heads > 1) st |= DSK_ST3_DSDRIVE;	/* Double sided */
	if (!fd->fd_motor) st &= ~DSK_ST3_READY;	/* Motor is not running */

	return st;
}


void fdl_set_datarate(FLOPPY_DRIVE *fd, fdc_byte rate)
{
        LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;
	switch (rate & 3)
	{
		case 0: fdl->fdl_diskg.dg_datarate = RATE_HD; break;
		case 1: fdl->fdl_diskg.dg_datarate = RATE_DD; break;
		case 2: fdl->fdl_diskg.dg_datarate = RATE_SD; break;
		case 3: fdl->fdl_diskg.dg_datarate = RATE_ED; break;
	}
}



static FLOPPY_DRIVE_VTABLE fdv_libdsk = 
{
	fdl_seek_cylinder,
	fdl_read_id,
	fdl_read_sector,
	fdl_read_track,
	fdl_write_sector,
	fdl_format_track,
	fdl_drive_status,
	fdl_isready,
	fdl_dirty,
	fdl_eject,
	fdl_set_datarate,
	NULL,
	fdl_reset
};

/* Initialise a DSK-based drive */
FDRV_PTR fd_newldsk(void)
{
	FDRV_PTR fd = fd_inew(sizeof(LIBDSK_FLOPPY_DRIVE));

	fd->fd_vtable = &fdv_libdsk;
	fdl_reset(fd);
	return fd;
	}


/* Create a new DSK file. Not necessary for emulation but well worth having  */
fd_err_t fdl_new_dsk(LIBDSK_FLOPPY_DRIVE *fdl)
{
	dsk_err_t err;

	if (fdl->fdl_filename[0] == 0) return 0; /* No filename */
	if (!fdl->fdl_type == 0) return 0; /* No type */

	err = dsk_creat(&fdl->fdl_diskp, fdl->fdl_filename, fdl->fdl_type,
			 fdl->fdl_compress);
	if (err) return fdl_xlt_error(err);
	dsk_close(&fdl->fdl_diskp);
	return 0;
}

/* Get / set DSK file associated with this drive.
 * Note that doing fdl_setfilename() causes an implicit eject on the 
 * previous disc in the drive. */
char *   fdl_getfilename(FDRV_PTR fd)
{
        if (fd->fd_vtable == &fdv_libdsk)
        {
                return ((LIBDSK_FLOPPY_DRIVE *)fd)->fdl_filename;
        }
        return "Called fdl_getfilename() on wrong drive type";
}

void     fdl_setfilename(FDRV_PTR fd, const char *s)
{
        if (fd->fd_vtable == &fdv_libdsk)
        {
                LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;

		fd_eject(fd);
                strncpy(fdl->fdl_filename, s, sizeof(fdl->fdl_filename) - 1);
                fdl->fdl_filename[sizeof(fdl->fdl_filename) - 1] = 0;
        }
}

const char *   fdl_gettype(FDRV_PTR fd)
{
        if (fd->fd_vtable == &fdv_libdsk)
        {
                return ((LIBDSK_FLOPPY_DRIVE *)fd)->fdl_type;
        }
        return "Called fdl_gettype() on wrong drive type";
}

void     fdl_settype(FDRV_PTR fd, const char *s)
{
        if (fd->fd_vtable == &fdv_libdsk)
        {
                LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;

                fdl->fdl_type = s;
        }
}

const char *   fdl_getcomp(FDRV_PTR fd)
{
        if (fd->fd_vtable == &fdv_libdsk)
        {
                return ((LIBDSK_FLOPPY_DRIVE *)fd)->fdl_compress;
        }
        return "Called fdl_getcomp() on wrong drive type";
}

void     fdl_setcomp(FDRV_PTR fd, const char *s)
{
        if (fd->fd_vtable == &fdv_libdsk)
        {
                LIBDSK_FLOPPY_DRIVE *fdl = (LIBDSK_FLOPPY_DRIVE *)fd;

                fdl->fdl_compress = s;
	}
}

#endif	// def HAVE_LIBDSK_H
