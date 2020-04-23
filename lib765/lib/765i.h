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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "config.h"
#include "765.h"



typedef struct floppy_drive_vtable
{
	fd_err_t  (*fdv_seek_cylinder)(FDRV_PTR fd, int cylinder);
	fd_err_t  (*fdv_read_id)      (FDRV_PTR fd, int head, 
                                 int sector, fdc_byte *buf);
	fd_err_t  (*fdv_read_sector  )(FDRV_PTR fd, int xcylinder, 
		int xhead, int head, int sector, fdc_byte *buf, int len, 
		int *deleted, int skip_deleted, int mfm, int multi);
	fd_err_t  (*fdv_read_track   )(FDRV_PTR fd, int xcylinder,
		int xhead, int head, fdc_byte *buf, int *len);
	fd_err_t  (*fdv_write_sector )(FDRV_PTR fd, int xcylinder,
		int xhead, int head, int sector, fdc_byte *buf, int len,
		int deleted, int skip_deleted, int mfm, int multi);
	fd_err_t (*fdv_format_track )(FDRV_PTR fd, int head, 
		int sectors, fdc_byte *buf, fdc_byte filler);
	fdc_byte (*fdv_drive_status )(FDRV_PTR fd);
	int      (*fdv_isready)(FDRV_PTR fd);
	int	 (*fdv_dirty  )(FDRV_PTR fd);
	void     (*fdv_eject  )(FDRV_PTR fd);
	void	 (*fdv_set_datarate)(FDRV_PTR fd, fdc_byte rate);
	void     (*fdv_reset  )(FDRV_PTR fd);
	void     (*fdv_destroy)(FDRV_PTR fd);
	int	 (*fdv_changed)(FDRV_PTR fd);
} FLOPPY_DRIVE_VTABLE;


typedef struct floppy_drive
{
/* PRIVATE variables 
 * The following points to the drive's method table. You should not need
 * to use this; instead, use the fd_*() wrapper functions below. */
	FLOPPY_DRIVE_VTABLE * fd_vtable;	
/* PUBLIC members */
	/* You should set the first three of these immediately after calling 
         * fd_init() or fdd_init() on a drive.*/
	int fd_type;	  /* 0 for none, 1 for 3", 2 for 3.5", 3 for 5.25" */
	int fd_heads;	  /* No. of heads in the drive: 1 or 2 */
	int fd_cylinders; /* No. of cylinders the drive can access: 
			   * eg: a nominally 40-track drive can usually go up
                           * to 42 tracks with a bit of "persuasion" */
	int fd_readonly;  /* Is the drive (or the disc therein) set to R/O? */
	int fd_changed;	  /* Default changeline implementation. This will be 
			   * set to 1 when drive is ejected, 0 at controller
			   * partial reset. If you can write a better 
			   * implementation of the changeline, override 
			   * fdv_changed(). */

/* READONLY variables */
	int fd_motor;	  /* Is the motor for this drive running? */
	int fd_cylinder;  /* Current cylinder. Note that if the drive is
			   * double-stepping, this is the "real" cylinder - 
			   * so it could = 24 and be reading cylinder 12 
                           * of a 40-track DSK file. */
} FLOPPY_DRIVE;

/* Subclass of FLOPPY_DRIVE: a drive which emulates discs using the CPCEMU 
 * .DSK format */

typedef struct dsk_floppy_drive
{
/* PUBLIC variables: */
	FLOPPY_DRIVE fdd;		/* Base class */
	char  fdd_filename[PATH_MAX];	/* Filename to .DSK file. Before 
					 * changing this call fd_eject() on
                                         * the drive */
/* PRIVATE variables: */
	FILE *fdd_fp;			/* File of the .DSK file */
	fdc_byte fdd_disk_header[256];	/* .DSK header */
	fdc_byte fdd_track_header[256];	/* .DSK track header */
	int fdd_dirty;			/* Has this disk been written to? */
} DSK_FLOPPY_DRIVE;

#ifdef DSK_ERR_OK	/* LIBDSK headers included */
typedef struct libdsk_floppy_drive
{
/* PUBLIC variables: */
	FLOPPY_DRIVE fdl;		/* Base class */
	char  fdl_filename[PATH_MAX];	/* Filename to .DSK file. Before 
					 * changing this call fd_eject() on
                                         * the drive */
	const char  *fdl_type;		/* LIBDSK drive type, NULL for auto */
	const char  *fdl_compress;	/* LIBDSK compression, NULL for auto */
/* PRIVATE variables: */
	DSK_PDRIVER  fdl_diskp;	
	DSK_GEOMETRY fdl_diskg;		/* Autoprobed geometry */
} LIBDSK_FLOPPY_DRIVE;
#endif	/* ifdef DSK_ERR_OK */

typedef struct nc9_floppy_drive
{
	FLOPPY_DRIVE fdd;		/* Base class */
	FLOPPY_DRIVE *nc9_fdd;		/* Pointer to the 9256's B drive */
} NC9_FLOPPY_DRIVE;


FDRV_PTR fd_inew(size_t size);


/* This class represents the controller itself. When you instantiate one, call
 * fdc_reset() on it before doing anything else with it. */

typedef struct fdc_765
{
    /* PRIVATE variables */
	int fdc_interrupting;	/* 0 => Not interrupting
				 * 1 => Entering result phase of 
				 *      Read/Write/Format/Scan
				 * 2 => Ready for data transfer 
				 *      (execution phase) 
				 * 4 => End of Seek/Recalibrate command */
	/* The results from the SPECIFY command */
	int fdc_specify[2];
	/* The last sector for which a DD READ ID request was made */
	int fdc_lastidread; 

	/* Current WRITE command is for deleted data? */
	int fdc_write_deleted;

	/* Command phase buffer */
	int fdc_cmd_id;   	/* Current command */
	int fdc_cmd_len;  	/* No. of bytes remaining to transfer */
	int fdc_cmd_pos;	/* Next buffer position to write */
	fdc_byte fdc_cmd_buf[20];	/* The command as a byte string */

	/* Execution phase buffer */
	/* [0.4.2] If we are doing multisector reads, this needs to hold a 
	 * whole track's worth of sectors: let's say 16k. (A normal HD disc 
	 * holds 9k / track) */
	fdc_byte fdc_exec_buf[16384];
	int  fdc_exec_len;	/* No. of bytes remaining to transfer */
	int  fdc_exec_pos;	/* Position in buffer */

	/* Results phase buffer */
	fdc_byte fdc_result_buf[20];
	int fdc_result_len;	/* No. of bytes remaining to transfer */	
	int fdc_result_pos;	/* Position in buffer */

	int fdc_terminal_count;	/* Set to abort a transfer */	
	int fdc_isr_countdown;	/* Countdown to interrupt */

	int fdc_dor;		/* Are we using that horrible kludge, the
				 * Digital Output Register, rather than
				 * proper drive select lines? */
	/* Drive pointers after the DOR has had its wicked way */
	FLOPPY_DRIVE *fdc_dor_drive[4];

	/* READONLY variables - these can be used in status displays */
        /* The four uPD765A status registers */
        int fdc_st0, fdc_st1, fdc_st2, fdc_st3;
        /* The main status register */
        int fdc_mainstat;
	int fdc_curunit, fdc_curhead;   /* Currently active unit & head */

	/* Public variables */
	void (*fdc_isr)(struct fdc_765 *self, int status);
		/* EXT: Called when interrupt line is raised or lowered.
		 *     You must provide this if the FDC is to interrupt. */
	FLOPPY_DRIVE *fdc_drive[4];
		/* The FDC's four drives. You must set these pointers */

} FDC_765;



