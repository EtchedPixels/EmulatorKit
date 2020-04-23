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

/*
 * Default implementations of the FDC functions. If the pointer to the drive
 * or the function pointer is null, it returns results as if the drive 
 * were not present.
 */

/* Seek to a cylinder */
fd_err_t fd_seek_cylinder(FDRV_PTR fd, int cylinder)
{
	if (fd && (fd->fd_vtable->fdv_seek_cylinder))
	{
		return (*fd->fd_vtable->fdv_seek_cylinder)(fd, cylinder);
	}
	return FD_E_NOTRDY;
}


/* Read the ID of the next sector to pass under the head. "sector" is 
 * suggested since most emulated drives don't actually emulate the idea
 * of a head being over one sector at a time */
fd_err_t  fd_read_id(FDRV_PTR fd, int head, int sector, fdc_byte *buf)
{
	if (fd && (fd->fd_vtable->fdv_read_id))
	{
		return (*fd->fd_vtable->fdv_read_id)(fd, head, sector, buf);
	}
	return FD_E_NOTRDY;
}

/* Read a sector. xcylinder and xhead are the expected values for the 
 * sector header; head is the actual head to use. */
fd_err_t  fd_read_sector(FDRV_PTR fd, int xcylinder, 
		int xhead, int head, int sector, fdc_byte *buf, int len, 
		int *deleted, int skip_deleted, int mfm, int multi)
{
	if (fd && (fd->fd_vtable->fdv_read_sector))
	{
		return (*fd->fd_vtable->fdv_read_sector)(fd, xcylinder, 
			xhead, head, sector, buf, len, deleted, 
			skip_deleted, mfm, multi);
	}
	return FD_E_NOTRDY;
}

/* Read a track. xcylinder and xhead are the expected values for the 
 * sector header; head is the actual head to use. */
fd_err_t  fd_read_track(FDRV_PTR fd, int xcylinder,
                int xhead, int head, fdc_byte *buf, int *len)
{
        if (fd && (fd->fd_vtable->fdv_read_track))
        {
                return (*fd->fd_vtable->fdv_read_track)(fd, xcylinder,
                        xhead, head, buf, len);
        }
        return FD_E_NOTRDY;
}



/* Write a sector. xcylinder and xhead are the expected values for the 
 * sector header; head is the actual head to use. */
fd_err_t  fd_write_sector(FDRV_PTR fd, int xcylinder,
                int xhead, int head, int sector, fdc_byte *buf, int len, 
		int deleted, int skip_deleted, int mfm, int multi)
{
        if (fd && (fd->fd_vtable->fdv_write_sector))
        {
                return (*fd->fd_vtable->fdv_write_sector)(fd, xcylinder, 
                        xhead, head, sector, buf, len, deleted, 
			skip_deleted, mfm, multi);
        }
        return FD_E_NOTRDY;
}

/* Format a track */
fd_err_t  fd_format_track (struct floppy_drive *fd, int head,
                int sectors, fdc_byte *track, fdc_byte filler)
{
	if (fd && (fd->fd_vtable->fdv_format_track))
	{
		return (*fd->fd_vtable->fdv_format_track)(fd, head, sectors, track, filler);
	}
	return FD_E_NOTRDY;
}


/* Get the drive status (as given in bits 7-3 of DD_DRIVE_STATUS) */
fdc_byte fd_drive_status(FDRV_PTR fd)
{
	if (fd && (fd->fd_vtable->fdv_drive_status))
	{
		fdc_byte result;
	     	result = (*fd->fd_vtable->fdv_drive_status)(fd);
		return result;
	}
	return 0;	/* Drive not present */
}


/* Is the drive ready? */
fdc_byte fd_isready(FDRV_PTR fd)
{
	if (fd && (fd->fd_vtable->fdv_isready)) return (*fd->fd_vtable->fdv_isready)(fd);
	return 0;
}



/* Is the drive ready? */
fdc_byte fd_changed(FDRV_PTR fd)
{
	if (fd && (fd->fd_vtable->fdv_changed)) 
			return (*fd->fd_vtable->fdv_changed)(fd);
	else if (fd)	return fd->fd_changed;
	return 0;
}

int fd_dirty(FDRV_PTR fd)
{
	if (fd && (fd->fd_vtable->fdv_dirty))
			return (*fd->fd_vtable->fdv_dirty)(fd);
	else return FD_D_UNAVAILABLE;
}

/* Eject under computer's control */
void fd_eject(FDRV_PTR fd)
{
	if (fd && (fd->fd_vtable->fdv_eject)) 
		(*fd->fd_vtable->fdv_eject)(fd);
	if (fd) fd->fd_changed = 1;
	
}

/* Reset the drive */
void fd_reset(FDRV_PTR fd)
{
	if (fd && (fd->fd_vtable->fdv_reset)) 
		(*fd->fd_vtable->fdv_reset)(fd);
}

/* Set data rate */
void fd_set_datarate(FDRV_PTR fd, fdc_byte rate)
{
	if (fd && (fd->fd_vtable->fdv_set_datarate)) 
		(*fd->fd_vtable->fdv_set_datarate)(fd, rate);
}

/* On the PCW9256, drives 2 and 3 always return ready. Drive 2 at least
 * passes all its other commands to drive 1. */

static fd_err_t n9256_seek_cylinder(FDRV_PTR fd, int cylinder)
{
        NC9_FLOPPY_DRIVE *nc9 = (NC9_FLOPPY_DRIVE *)fd;
        if (nc9->nc9_fdd) return fd_seek_cylinder(nc9->nc9_fdd, cylinder);

	return FD_E_NOTRDY;
}


static fdc_byte n9256_drive_status(FDRV_PTR fd)
{
	fdc_byte b = 0;
	
	NC9_FLOPPY_DRIVE *nc9 = (NC9_FLOPPY_DRIVE *)fd;
	if (nc9->nc9_fdd) b = fd_drive_status(nc9->nc9_fdd);

	return 0x20 | b;	/* Drive is always ready */
}


static FLOPPY_DRIVE_VTABLE dummy_vtbl;	/* all NULLs */
static FLOPPY_DRIVE_VTABLE d9256_vtbl =	/* nearly all NULLs */
{
	n9256_seek_cylinder,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
        n9256_drive_status,
	NULL,
	NULL,
	NULL,
	NULL
};

/* Initialise a FLOPPY_DRIVE structure */
FDRV_PTR fd_inew(size_t size)
{
	FDRV_PTR fd;

	if (size < sizeof(FLOPPY_DRIVE)) return NULL;
	fd = malloc(size);
	if (!fd) return NULL;

	fd->fd_type      = FD_NONE;
	fd->fd_heads     = 0;
	fd->fd_cylinders = 0;
	fd->fd_motor     = 0;
	fd->fd_cylinder  = 0;
	fd->fd_readonly  = 0;	
	fd->fd_vtable    = &dummy_vtbl;
	return fd;
}

FDRV_PTR fd_new(void)
{
	return fd_inew(sizeof(FLOPPY_DRIVE));
}

/* Initialise a 9256 dummy drive */
FDRV_PTR fd_newnc9(FDRV_PTR fd)
{
	FDRV_PTR p = fd_inew(sizeof(NC9_FLOPPY_DRIVE));

	fd_settype(p, FD_NC9256);
	((NC9_FLOPPY_DRIVE *)p)->nc9_fdd = fd;
//
// These are the only commands which CP/M executes on a 9256 dummy drive,
// and so these are the only ones I'm going to pass through to the 
// underlying drive.
//
	p->fd_vtable    = &d9256_vtbl;
	return p;
}


void     fd_destroy(FDRV_PTR *fd)
{
	if (!(*fd)) return;

	fd_eject(*fd);	
	if ((*fd)->fd_vtable->fdv_destroy)
	{
		(*(*fd)->fd_vtable->fdv_destroy)(*fd);
	}
	free(*fd);
	*fd = NULL;
}


int fd_gettype    (FDRV_PTR fd) { return fd->fd_type; } 
int fd_getheads   (FDRV_PTR fd) { return fd->fd_heads; }
int fd_getcyls    (FDRV_PTR fd) { return fd->fd_cylinders; }
int fd_getreadonly(FDRV_PTR fd) { return fd->fd_readonly; }

void fd_settype    (FDRV_PTR fd, int type)  { fd->fd_type  = type;  }
void fd_setheads   (FDRV_PTR fd, int heads) { fd->fd_heads = heads; }
void fd_setcyls    (FDRV_PTR fd, int cyls)  { fd->fd_cylinders  = cyls;  }
void fd_setreadonly(FDRV_PTR fd, int ro)    { fd->fd_readonly = ro; }

int fd_getmotor    (FDRV_PTR fd) { return fd->fd_motor; }
int fd_getcurcyl   (FDRV_PTR fd) { return fd->fd_cylinder; }


FDRV_PTR fd9_getproxy(FDRV_PTR self)
{
	if (self->fd_vtable == &d9256_vtbl)
	{
		return ((NC9_FLOPPY_DRIVE *)self)->nc9_fdd;
	}
	return NULL;
}

void     fd9_setproxy(FDRV_PTR self, FDRV_PTR proxy)
{
	if (self->fd_vtable == &d9256_vtbl)
	{
		((NC9_FLOPPY_DRIVE *)self)->nc9_fdd = proxy;
	}

}
