/* 765: Library to emulate the uPD765a floppy controller (aka Intel 8272)

    Copyright (C) 2000, 2008  John Elliott <jce@seasip.demon.co.uk>

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

/* The number of bytes in the 32 possible FDC commands */

static int bytes_in_cmd[32] = { 1,  /* 0 = none */
                                1,  /* 1 = none */
                                9,  /* 2 = READ TRACK */
                                3,  /* 3 = SPECIFY */
                                2,  /* 4 = SENSE DRIVE STATUS */
                                9,  /* 5 = WRITE DATA */
                                9,  /* 6 = READ DATA */
                                2,  /* 7 = RECALIBRATE */
                                1,  /* 8 = SENSE INTERRUPT STATUS */
                                9,  /* 9 = WRITE DELETED DATA */
                                2,  /* 10 = READ SECTOR ID */
                                1,  /* 11 = none */
                                9,  /* 12 = READ DELETED DATA */
                                6,  /* 13 = FORMAT A TRACK */
                                1,  /* 14 = none */
                                3,  /* 15 = SEEK */
                                1,  /* 16 = none */
                                9,  /* 17 = SCAN EQUAL */
                                1,  /* 18 = none */
                                1,  /* 19 = none */
                                1,  /* 20 = none */
                                1,  /* 21 = none */
                                1,  /* 22 = none */
                                1,  /* 23 = none */
                                1,  /* 24 = none */
                                9,  /* 25 = SCAN LOW OR EQUAL */
                                1,  /* 26 = none */
                                1,  /* 27 = none */
                                1,  /* 28 = none */
                                1,  /* 29 = none */
                                9,  /* 30 = SCAN HIGH OR EQUAL */
                                1   /* 31 = none */
                              };

/**********************************************************************
 *                        FDC IMPLEMENTATION                          *
 **********************************************************************/


/* Partial reset (done by pulling bit 2 of the DOR low) */
static void fdc_part_reset(FDC_765 *self)
{
	int n;

	self->fdc_mainstat       = 0x80;	/* Ready */
	self->fdc_st0            = 0;
	self->fdc_st1            = 0;
	self->fdc_st2            = 0;
	self->fdc_st3	         = 0;
	self->fdc_curunit        = 0;
	self->fdc_curhead        = 0;
	self->fdc_cmd_id         = -1;
	self->fdc_cmd_len        = 0;
	self->fdc_cmd_pos        = 0;
	self->fdc_exec_len       = 0;
	self->fdc_exec_pos       = 0;
	self->fdc_result_len     = 0;
	self->fdc_result_pos     = 0;
	memset(self->fdc_cmd_buf,   0, sizeof(self->fdc_cmd_buf));
	memset(self->fdc_exec_buf,  0, sizeof(self->fdc_exec_buf));
	memset(self->fdc_result_buf,0, sizeof(self->fdc_result_buf));
/* Reset any changelines */
	for (n = 0; n < 4; n++)	
	{
		if (self->fdc_drive[n]) self->fdc_drive[n]->fd_changed = 0;
	}
}


/* Initialise / reset the FDC */
void fdc_reset(FDC_765 *self)
{
	self->fdc_interrupting   = 0;
	self->fdc_specify[0]     = self->fdc_specify[1] = 0;
	self->fdc_lastidread     = 0;
	self->fdc_terminal_count = 0;
	self->fdc_isr            = NULL;
	self->fdc_isr_countdown  = 0L;
	self->fdc_dor		 = -1;	/* Not using the DOR at all */
	memset(self->fdc_drive,     0, sizeof(self->fdc_drive));
	fdc_part_reset(self);
}

static void fdc_dorcheck(FDC_765 *self)
{
	int n;

	/* v0.1.0: Check if the DOR is forcing us to select a different
	 * drive. */
	if (self->fdc_dor < 0)
		for (n = 0; n < 4; n++) self->fdc_dor_drive[n] = self->fdc_drive[n];
	else	for (n = 0; n < 4; n++) self->fdc_dor_drive[n] = self->fdc_drive[self->fdc_dor & 3];
}



/* Update the ST3 register based on a drive's status */
static void fdc_get_st3(FDC_765 *self)
{	
	FLOPPY_DRIVE *fd = self->fdc_dor_drive[self->fdc_curunit];
	fdc_byte value = 0;

	/* 0.3.4: Check this, it could be null! */
	if (fd->fd_vtable->fdv_drive_status)
	{
		value = (*fd->fd_vtable->fdv_drive_status)(fd);	
	}
	value &= 0xF8;
	value |= (self->fdc_curhead ? 4 : 0);
	value |= (self->fdc_curunit & 3);
	self->fdc_st3 = value;
}

/* Check if a drive is ready */
int fdc_isready(FDC_765 *self, FLOPPY_DRIVE *fd)
{
        int rdy = fd_isready(fd);
 
        if (!rdy) {self->fdc_st3 &= 0xDF; return 0; }
        self->fdc_st3 |= 0x20;
        return 1;
}


/* Convert an error from the fd_* routines to a value in the status registers */
static void fdc_xlt_error(FDC_765 *self, fd_err_t error)
{
	fdc_dprintf(4, "FDC: Error from drive: %d\n", error);
        switch(error)
        {
                case FD_E_NOTRDY: self->fdc_st0 |= 0x48; break;
		case FD_E_SEEKFAIL:
				  self->fdc_st0 |= 0x40;
				  self->fdc_st2 |= 0x02; break;
                case FD_E_NOADDR: self->fdc_st0 |= 0x40;
                                  self->fdc_st2 |= 0x01; break;
                case FD_E_NODATA: self->fdc_st0 |= 0x40;
                                  self->fdc_st1 |= 0x04; break;
		case FD_E_DATAERR:
				  self->fdc_st1 |= 0x20;
				  self->fdc_st2 |= 0x20; break;
                case FD_E_NOSECTOR:
                                  self->fdc_st0 |= 0x40;
                                  self->fdc_st1 |= 0x82; break;
                case FD_E_READONLY: 
				  self->fdc_st0 |= 0x40;
				  self->fdc_st1 |= 0x02; break;
        }

}

/* Fill out 7 result bytes
 * XXX Bytes 2,3,4,5 should change the way the real FDC does it. */
static void fdc_results_7(FDC_765 *self)
{
	self->fdc_result_buf[0] = self->fdc_st0;	/* 3 status registers */
        self->fdc_result_buf[1] = self->fdc_st1;
        self->fdc_result_buf[2] = self->fdc_st2;
        self->fdc_result_buf[3] = self->fdc_cmd_buf[2];	/* C, H, R, N */
        self->fdc_result_buf[4] = self->fdc_cmd_buf[3];
        self->fdc_result_buf[5] = self->fdc_cmd_buf[4];
        self->fdc_result_buf[6] = self->fdc_cmd_buf[5];
	self->fdc_mainstat = 0xD0;	/* Ready to return results */
}


/* End of result phase. Switch FDC back to idle */
static void fdc_end_result_phase(FDC_765 *self)
{
	self->fdc_mainstat = 0x80;
	if (self->fdc_interrupting < 3) self->fdc_interrupting = 0;
	self->fdc_cmd_id = -1;
}

/* Generate a 1-byte result phase containing ST0 */
static void fdc_error(FDC_765 *self)
{
	self->fdc_st0 = (self->fdc_st0 & 0x3F) | 0x80;
	self->fdc_mainstat = 0xD0;	/* Result phase */
	self->fdc_result_len = 1;
	self->fdc_result_pos = 0;
	self->fdc_result_buf[0] = self->fdc_st0;
}


/* Interrupt: Start of result phase */
static void fdc_result_interrupt(FDC_765 *self)
{
	self->fdc_isr_countdown = SHORT_TIMEOUT;
        self->fdc_interrupting  = 1;	/* Result-phase interrupt */
}


/* Interrupt: Start of execution phase */
static void fdc_exec_interrupt(FDC_765 *self)
{
        self->fdc_isr_countdown = SHORT_TIMEOUT;
        self->fdc_interrupting  = 2;    /* Execution-phase interrupt */
}

/* Compare two bytes - for the SCAN commands */
static void fdc_scan_byte(FDC_765 *self, fdc_byte fdcbyte, fdc_byte cpubyte)
{
        int cmd = self->fdc_cmd_buf[0] & 0x1F;
        if ((self->fdc_st2 & 0x0C) != 8) return;	/* Mismatched already */
        
        if ((fdcbyte == cpubyte) || (fdcbyte == 0xFF) || (cpubyte == 0xFF)) 
		return;	/* Bytes match */

        /* They differ */
        if (cmd == 17) /* equal */
        {
	    self->fdc_st2 = (self->fdc_st2 & 0xF3) | 4; /* != */
	}
        if (cmd == 25) /* low or equal */
        {
            if (fdcbyte < cpubyte) self->fdc_st2 = (self->fdc_st2 & 0xF3);
            if (fdcbyte > cpubyte) self->fdc_st2 = (self->fdc_st2 & 0xF3) | 4;
// why?             self->fdc_st2 = (self->fdc_st2 & 0xF3);
        }
        if (cmd == 30) /* high or equal */
        {
            if (fdcbyte < cpubyte) self->fdc_st2 = (self->fdc_st2 & 0xF3) | 4;
	    if (fdcbyte > cpubyte) self->fdc_st2 = (self->fdc_st2 & 0xF3);
 // why?           self->fdc_st2 = (self->fdc_st2 & 0xF3);
        }
}



/* Get the drive & head from the FDC command bytes */
static void fdc_get_drive(FDC_765 *self)
{
	/* Set current drive & head in FDC struct */
	self->fdc_curunit =  self->fdc_cmd_buf[1] & 3;
	self->fdc_curhead = (self->fdc_cmd_buf[1] & 4) >> 2;

	/* Set current drive & head in FDC status regs */
	self->fdc_st0 &= 0xF8;
	self->fdc_st3 &= 0xF8;

	self->fdc_st0 |= (self->fdc_cmd_buf[1] & 7);
        self->fdc_st3 |= (self->fdc_cmd_buf[1] & 7);
}


/* End of Execution Phase in a write command. Write data. */
static void fdc_write_end(FDC_765 *self)
{
        FLOPPY_DRIVE *fd = self->fdc_dor_drive[self->fdc_curunit];
	int len = (128 << self->fdc_cmd_buf[5]);
	fd_err_t rv;

	if (self->fdc_cmd_buf[8] < 255) len = self->fdc_cmd_buf[8];

	rv = fd_write_sector(fd, self->fdc_cmd_buf[2], /* xcyl */
				 self->fdc_cmd_buf[3], /* xhd  */
				 self->fdc_curhead,    /* head */
				 self->fdc_cmd_buf[4], /* sec */
				 self->fdc_exec_buf, len,
				 self->fdc_write_deleted, 
				 self->fdc_cmd_buf[0] & 0x20,
				 self->fdc_cmd_buf[0] & 0x40,
				 self->fdc_cmd_buf[0] & 0x80);
	
	fdc_xlt_error(self, rv);

        fdc_results_7(self);
        fdc_result_interrupt(self);
}




/* End of Execution Phase in a format command. Format the track.   */
static void fdc_format_end(FDC_765 *self)
{
        FLOPPY_DRIVE *fd = self->fdc_dor_drive[self->fdc_curunit];
	fd_err_t rv;


	rv = fd_format_track(fd, self->fdc_curhead, /* head */
			     self->fdc_cmd_buf[3],  /* Sectors/track */
			     self->fdc_exec_buf,    /* Track template */
			     self->fdc_cmd_buf[5]); /* Filler byte */
	
	fdc_xlt_error(self, rv);

        fdc_results_7(self);
        fdc_result_interrupt(self);
}



/* Called when execution phase finishes */
static void fdc_end_execution_phase(FDC_765 *self)
{
        fdc_byte cmd = self->fdc_cmd_buf[0] & 0x1F;

	self->fdc_mainstat = 0xD0;	/* Ready to return results */
	
	self->fdc_result_pos = 0;

        switch(cmd)
        {
                case 17:                          /* SCAN */
                case 25:
                case 30:  fdc_results_7(self);    /* fall through */
		case 12:
                case 6:   self->fdc_result_len = 7; 
			/* Do an fdc_result_interrupt, the command has 
			 * finished. Would normally happen on buffer
		 	 * exhaustion, but if you set the terminal count
			 * then the buffer doesn't get exhausted */
			  fdc_result_interrupt(self);	/* Results ready */
			  break;  			/* READ */

		case 9:
                case 5:   fdc_write_end(self);
                          self->fdc_result_len = 7; break;  /* WRITE */

                case 13:  fdc_format_end(self);
                          self->fdc_result_len = 7; break;  /* FORMAT */
        }
}


/****************************************************************
 *                    FDC COMMAND HANDLERS                      *
 ****************************************************************/


/* READ TRACK */

static void fdc_read_track(FDC_765 *self)
{
	int err;
	FLOPPY_DRIVE *fd;

	self->fdc_st0 = self->fdc_st1 = self->fdc_st2 = 0;
	self->fdc_lastidread = 0;
	
	fdc_get_drive(self);	

	fd = self->fdc_dor_drive[self->fdc_curunit];

	self->fdc_exec_len = MAX_SECTOR_LEN;

        if (!fdc_isready(self, fd)) err = FD_E_NOTRDY;
	else err = fd_read_track(fd, 
		self->fdc_cmd_buf[2], self->fdc_cmd_buf[3],
		self->fdc_curhead,
		self->fdc_exec_buf, 
		&self->fdc_exec_len);

	if (err) fdc_xlt_error(self, err);

	fdc_results_7(self);
	if (err && err != FD_E_DATAERR)
	{
		fdc_end_execution_phase(self);
		fdc_result_interrupt(self);
		return;
	}

        fdc_exec_interrupt(self);
	self->fdc_mainstat = 0xF0;	/* Ready to transfer data */
	self->fdc_exec_pos = 0;
}


/* SPECIFY */
static void fdc_specify(FDC_765 *self)
{
        self->fdc_specify[0] = self->fdc_cmd_buf[1];
        self->fdc_specify[1] = self->fdc_cmd_buf[2];
        fdc_end_result_phase(self);
}

/* SENSE DRIVE STATUS */
static void fdc_sense_drive(FDC_765 *self)
{
	fdc_get_drive(self);
	fdc_get_st3(self);
	self->fdc_result_buf[0] = self->fdc_st3;
	self->fdc_result_len    = 1;
	fdc_end_execution_phase(self);
}


/* READ DATA
 * READ DELETED DATA 
 */

static void fdc_read(FDC_765 *self, int deleted)
{
	int err = 0;
	FLOPPY_DRIVE *fd;
	int sector;
	size_t lensector;
	fdc_byte *buf = self->fdc_exec_buf;

	self->fdc_st0 = self->fdc_st1 = self->fdc_st2 = 0;
	self->fdc_lastidread = 0;
	
	fdc_get_drive(self);	

	self->fdc_exec_len = 0;
	/* 0.4.0: Support for multisector reads. Do it naively by reading
	 * all the sectors in one go. */
	for (sector = self->fdc_cmd_buf[4]; sector <= self->fdc_cmd_buf[6]; 
			sector++)
	{
		fd = self->fdc_dor_drive[self->fdc_curunit];
       		lensector = (128 << self->fdc_cmd_buf[5]);
       		if (self->fdc_cmd_buf[8] < 255) 
			lensector = self->fdc_cmd_buf[8];

		memset(buf, 0, lensector);

		if (!fdc_isready(self, fd)) err = FD_E_NOTRDY;
		else err = fd_read_sector(fd, 
			self->fdc_cmd_buf[2], /* Cylinder expected */
			self->fdc_cmd_buf[3], /* Head expected */
			self->fdc_curhead,    /* Real head */
			self->fdc_cmd_buf[4], /* Sector */
			buf,
			lensector, 
			&deleted,
			self->fdc_cmd_buf[0] & 0x20,
			self->fdc_cmd_buf[0] & 0x40,
			self->fdc_cmd_buf[0] & 0x80);

		if (err) fdc_xlt_error(self, err);
		if (deleted) self->fdc_st2 |= 0x40;
		if (err && err != FD_E_DATAERR)
		{
			break;
		}
		buf += lensector;
		self->fdc_exec_len += lensector;
		++self->fdc_cmd_buf[4];		/* Next sector */
	}
	fdc_results_7(self);
	if (err && err != FD_E_DATAERR)
	{
		fdc_end_execution_phase(self);
		fdc_result_interrupt(self);
		return;
	}

        fdc_exec_interrupt(self);
	self->fdc_mainstat = 0xF0;	/* Ready to transfer data */
	self->fdc_exec_pos = 0;
}

/* WRITE DATA
 * WRITE DELETED DATA 
 *
 * XXX Does not support multisector
 */

static void fdc_write(FDC_765 *self, int deleted)
{
        int err = FD_E_OK;
	FLOPPY_DRIVE *fd;

        self->fdc_st0 = self->fdc_st1 = self->fdc_st2 = 0;
        self->fdc_lastidread = 0;
	self->fdc_write_deleted = deleted;

        fdc_get_drive(self);
	fd = self->fdc_dor_drive[self->fdc_curunit];

        self->fdc_exec_len = (128 << self->fdc_cmd_buf[5]);
        if (self->fdc_cmd_buf[8] < 255)
                self->fdc_exec_len = self->fdc_cmd_buf[8];

        memset(self->fdc_exec_buf, 0, self->fdc_exec_len);

        if (!fdc_isready(self, fd))     err = FD_E_NOTRDY;
	else if (fd && fd->fd_readonly) err = FD_E_READONLY;

	if (err) 
	{
		fdc_xlt_error(self, err);
                self->fdc_mainstat = 0xD0;      /* Ready to return results */
                self->fdc_result_pos = 0;
		fdc_results_7(self);
		self->fdc_result_pos = 0;
		self->fdc_result_len = 7;
		fdc_result_interrupt(self);
	}
	else
	{
		fdc_exec_interrupt(self);
		self->fdc_mainstat = 0xB0;	/* Ready to receive data */
		self->fdc_exec_pos = 0;
	}
}

/* RECALIBRATE */
static void fdc_recalibrate(FDC_765 *self)
{
	FLOPPY_DRIVE *fd;

	self->fdc_st0 = self->fdc_st1 = self->fdc_st2 = 0;
	
	fdc_get_drive(self);
	self->fdc_lastidread = 0;

	fd = self->fdc_dor_drive[self->fdc_curunit];

	fdc_end_result_phase(self);

	self->fdc_isr_countdown = SHORT_TIMEOUT;
	self->fdc_interrupting  = 4;		/* Interrupt: End of seek */
	self->fdc_st2 &= 0xFD;
	self->fdc_st0 |= 0x20;

	/* Seek the drive to track 0 */
        if (!fdc_isready(self, fd))
        {
                self->fdc_st0 |= 0x48;          /* Drive not ready */
        }
        else if ( fd_seek_cylinder(fd, 0) )
        {
                /* Seek failed */
                self->fdc_st2 |= 2;
                self->fdc_st0 |= 0x40;
        }
}

/* SENSE INTERRUPT STATUS */
static void fdc_sense_int(FDC_765 *self)
{
        if (self->fdc_interrupting > 2) 
		/* FDC interrupted, and is ready to return status */
        {
		fdc_byte cyl = 0;
		if (self->fdc_dor_drive[self->fdc_curunit]) 
			cyl = self->fdc_dor_drive[self->fdc_curunit]->fd_cylinder;	

		self->fdc_result_buf[0] = self->fdc_st0;
		self->fdc_result_buf[1] = cyl;
		self->fdc_result_len = 2;
		fdc_dprintf(7, "SENSE INTERRUPT STATUS: Return %02x %02x\n",
					self->fdc_st0, cyl);
        }
        else    /* FDC did not interrupt, error */
        {
                self->fdc_st0 = 0x80;
		self->fdc_result_buf[0] = self->fdc_st0;
                self->fdc_result_len = 1;
		fdc_dprintf(7, "SENSE INTERRUPT STATUS: Return 0x80\n");
        }
	fdc_end_execution_phase(self);

	/* Drop the interrupt line */
        self->fdc_isr_countdown = 0;
        self->fdc_interrupting = 0;
        if (self->fdc_isr)
        {
		(*self->fdc_isr)(self, 0);
        }

}


/* READ SECTOR ID */
static void fdc_read_id(FDC_765 *self)
{
        FLOPPY_DRIVE *fd;
	int ret;

	self->fdc_result_len = 7;
	self->fdc_st0 = self->fdc_st1 = self->fdc_st2 = 0;
	
	fdc_get_drive(self);

	fd = self->fdc_dor_drive[self->fdc_curunit];

        if (!fdc_isready(self, fd)) 
        {
                self->fdc_st0 |= 0x48;          /* Drive not ready */
        }
	else
	{
		ret=(*fd->fd_vtable->fdv_read_id)(fd, self->fdc_curhead,
				self->fdc_lastidread++, self->fdc_cmd_buf + 2);

		if (ret == FD_E_SEEKFAIL)
		{
			self->fdc_st1 |= 1;
			self->fdc_st0 |= 0x40;
		}	
		if (ret == FD_E_NOADDR) 
		{
			self->fdc_st2 |= 1;
			self->fdc_st0 |= 0x40;
		}
	}
	fdc_results_7(self);
	fdc_result_interrupt(self);
	fdc_end_execution_phase(self);
}



/* FORMAT TRACK */

static void fdc_format(FDC_765 *self)
{
        int err = FD_E_OK;
	FLOPPY_DRIVE *fd;

        self->fdc_st0 = self->fdc_st1 = self->fdc_st2 = 0;
        self->fdc_lastidread = 0;

        fdc_get_drive(self);
        fd = self->fdc_dor_drive[self->fdc_curunit];

        memset(self->fdc_exec_buf, 0, MAX_SECTOR_LEN);

        if (!fdc_isready(self, fd))     err = FD_E_NOTRDY;
	else if (fd && fd->fd_readonly) err = FD_E_READONLY;

	if (err) 
	{
		fdc_xlt_error(self, err);
       		self->fdc_mainstat = 0xD0;      /* Ready to return results */
	        self->fdc_result_pos = 0;
		fdc_results_7(self);
		fdc_result_interrupt(self);
	}
	else
	{
		fdc_exec_interrupt(self);
		self->fdc_mainstat = 0xB0;	/* Ready to receive data */
		self->fdc_exec_pos = 0;
		self->fdc_exec_len = 4 * self->fdc_cmd_buf[3];
	}
}

/* SEEK */
static void fdc_seek(FDC_765 *self)
{
	int cylinder = self->fdc_cmd_buf[2];
	FLOPPY_DRIVE *fd;
	
	self->fdc_st0 = self->fdc_st1 = self->fdc_st1 = 0;
	fdc_get_drive(self);
	self->fdc_lastidread = 0;

	fdc_end_result_phase(self);
	self->fdc_isr_countdown = SHORT_TIMEOUT;
	self->fdc_interrupting  = 4;	/* End of seek */
	self->fdc_st2 &= 0xFD;
	self->fdc_st0 |= 0x20;

	fd = self->fdc_dor_drive[self->fdc_curunit];

        if (!fdc_isready(self, fd))
        {
                self->fdc_st0 |= 0x48;          /* Drive not ready */
        }
	else if ( fd_seek_cylinder(fd, cylinder) )
	{
		/* Seek failed */
		self->fdc_st2 |= 2;
		self->fdc_st0 |= 0x40;
	}
}

/* SCAN EQUAL
 * SCAN LOW OR EQUAL
 * SCAN HIGH OR EQUAL
 */

static void fdc_scan(FDC_765 *self)
{
        int err;

	/* Load the sector we were working on */

        self->fdc_st0 = self->fdc_st1 = self->fdc_st2 = 0;
        self->fdc_lastidread = 0;

        fdc_get_drive(self);

        self->fdc_exec_len = (128 << self->fdc_cmd_buf[5]);
        if (self->fdc_cmd_buf[8] < 255)
                self->fdc_exec_len = self->fdc_cmd_buf[8];

        memset(self->fdc_exec_buf, 0, self->fdc_exec_len);

        err = fd_read_sector(self->fdc_dor_drive[self->fdc_curunit],
                self->fdc_cmd_buf[2], self->fdc_cmd_buf[3],
                self->fdc_curhead,
                self->fdc_cmd_buf[4], self->fdc_exec_buf,
                self->fdc_exec_len, 0, 
		self->fdc_cmd_buf[0] & 0x20,
		self->fdc_cmd_buf[0] & 0x40,
		self->fdc_cmd_buf[0] & 0x80);

        if (err) fdc_xlt_error(self, err);

        fdc_results_7(self);
        if (err && err != FD_E_DATAERR)
        {
                fdc_end_execution_phase(self);
                fdc_result_interrupt(self);
                return;
        }
        fdc_exec_interrupt(self);
	self->fdc_st2 |= 8;
        self->fdc_mainstat = 0xB0;      /* Ready to transfer data */
	self->fdc_exec_pos = 0;
}


/* --- Main FDC dispatcher --- */
static void fdc_execute(FDC_765 *self)
{
/* This code to dump out FDC commands as they are received is very useful
 * in debugging
 *
 * */
	int NC;
	fdc_dprintf(5, "FDC: ");
	for (NC = 0; NC < bytes_in_cmd[self->fdc_cmd_buf[0] & 0x1F]; NC++)
		fdc_dprintf(5, "%02x ", self->fdc_cmd_buf[NC]);
	fdc_dprintf(5, "\n");
 /* */
	/* Check if the DOR (ugh!) is being used to force us to a 
	   different drive. */
	fdc_dorcheck(self);

	/* Reset "seek finished" flag */
	self->fdc_st0 &= 0xBF;	 
	switch(self->fdc_cmd_buf[0] & 0x1F)
	{
		case 2: fdc_read_track(self);	break;	/* 2: READ TRACK */
		case 3: fdc_specify(self);	break;	/* 3: SPECIFY */
		case 4:	fdc_sense_drive(self);	break;	/* 4: SENSE DRV STATUS*/
		case 5:	fdc_write(self, 0);	break;	/* 5: WRITE */
		case 6: fdc_read(self, 0);	break;	/* 6: READ */
		case 7:	fdc_recalibrate(self);	break;	/* 7: RECALIBRATE */
		case 8: fdc_sense_int(self);	break;	/* 8: SENSE INT STATUS*/
		case 9: fdc_write(self, 1);	break;	/* 9: WRITE DELETED */
		case 10:fdc_read_id(self);	break;	/*10: READ ID */
		case 12:fdc_read(self, 1);	break;	/*12: READ DELETED */
		case 13:fdc_format(self);	break;	/*13: FORMAT TRACK */
		case 15:fdc_seek(self);		break;	/*15: SEEK */
		case 17:				/*17: SCAN EQUAL */
		case 25:				/*25: SCAN LOW/EQUAL */
		case 30:fdc_scan(self);		break;	/*30: SCAN HIGH/EQUAL*/
		default:
			fdc_dprintf(2, "Unknown FDC command %d\n", 
					self->fdc_cmd_buf[0] & 0x1F);
			fdc_error(self);	break;
	}

}

/* Make the FDC drop its "interrupt" line */
static void fdc_clear_pending_interrupt(FDC_765 *self)
{
        if (self->fdc_interrupting > 0 && self->fdc_interrupting < 4)
        {
                self->fdc_isr_countdown = 0;
                self->fdc_interrupting = 0;
                if (self->fdc_isr)
                {
                        (*self->fdc_isr)(self, 0);
                }
        }
}


/* Write a byte to the FDC's "data" register */
void fdc_write_data(FDC_765 *self, fdc_byte value)
{
	fdc_byte curcmd;

	fdc_clear_pending_interrupt(self);
	if (self->fdc_mainstat & 0x20)	/* In execution phase */
	{
		curcmd = self->fdc_cmd_buf[0] & 0x1F;
		switch(curcmd)
		{
			case 17:
			case 25:	/* SCAN commands */
			case 30:
				fdc_scan_byte(self,
					self->fdc_exec_buf[self->fdc_exec_pos],
					value);
				break;
			default:	/* WRITE commands */
				self->fdc_exec_buf[self->fdc_exec_pos] = value;
				break;	
		}
		++self->fdc_exec_pos;
		--self->fdc_exec_len;
                /* If at end of exec-phase, switch to result phase */
                if (!self->fdc_exec_len)
                {
                	fdc_end_execution_phase(self);
                        fdc_result_interrupt(self);
		}
                /* Interrupt SHORT_TIMEOUT cycles from now to say that the
                 * next byte is ready */
/* [JCE 8-3-2002] Changed > to >= for xjoyce 2.1.0 */
                if (self->fdc_interrupting >= 0 &&
                    self->fdc_interrupting < 3)
                {
                	self->fdc_isr_countdown = SHORT_TIMEOUT;
		}
		return;
	}
	/* [30-Mar-2008] If it thinks it's in a command phase but there are
	 * no more bytes left to receive, something must be wrong. Go back to
	 * being idle.
	 */
	if (self->fdc_cmd_id >= 0 && self->fdc_cmd_len <= 0)
	{
		self->fdc_cmd_id = -1;
		return;
	}
	if (self->fdc_cmd_id < 0)	/* FDC is idle */
	{
		self->fdc_cmd_id  = value;	/* Entering command phase */
		self->fdc_cmd_pos = 0;
		self->fdc_cmd_buf[0] = value;
		self->fdc_cmd_len = bytes_in_cmd[value & 0x1F] - 1;

		if (self->fdc_cmd_len     == 0) fdc_execute(self);
		else if (self->fdc_cmd_len < 0)	fdc_error(self);
		self->fdc_mainstat |= 0x10;	/* In a command */
		return;
	}
	/* FDC is not idle, nor in execution phase; so it must be 
	 * accepting a multibyte command */
	self->fdc_cmd_buf[++self->fdc_cmd_pos] = value;
	--self->fdc_cmd_len;
	if (!self->fdc_cmd_len) fdc_execute(self);
}


/* Read the FDC's main data register */
fdc_byte fdc_read_data (FDC_765 *self)
{
	fdc_dprintf(5, "FDC: Read main data register, value = ");

	fdc_clear_pending_interrupt(self);
       	if (self->fdc_mainstat & 0x80) /* Ready to output data */
	{
		fdc_byte v;
		if (self->fdc_mainstat & 0x20) /* In exec phase */
		{
			/* Output an exec-phase byte */
			v = self->fdc_exec_buf[self->fdc_exec_pos++];
			--self->fdc_exec_len;
			/* If at end of exec-phase, switch to result phase */
			if (!self->fdc_exec_len)
			{
				fdc_end_execution_phase(self);
				fdc_result_interrupt(self);
			}
			/* Interrupt SHORT_TIMEOUT cycles from now to say that
			 * the next byte is ready */
/* [JCE 8-3-2002] Changed > to >= for xjoyce 2.1.0 */
			if (self->fdc_interrupting >= 0 &&
		            self->fdc_interrupting < 3) 
			{
				self->fdc_isr_countdown = SHORT_TIMEOUT;
			}
			fdc_dprintf(7, "fdc_interrupting=%d\n", self->fdc_interrupting);
			fdc_dprintf(5, "%c:%02x\n", self->fdc_isr_countdown ? 'E' : 'e', v);
			return v;
		}
		/* Not in execution phase. So we must be in the result phase */	
		v = self->fdc_result_buf[self->fdc_result_pos++];
		--self->fdc_result_len;
		if (self->fdc_result_len == 0) fdc_end_result_phase(self);
		fdc_dprintf(5, "R:%02x  (%d remaining)\n", v, self->fdc_result_len);
		return v;	
	}
	/* FDC is not ready to return data! */
	fdc_dprintf(5, "N:%02x\n", self->fdc_mainstat | (1 << self->fdc_curunit));
	return self->fdc_mainstat | (1 << self->fdc_curunit);
}


/* Read the FDC's main control register */
fdc_byte fdc_read_ctrl (FDC_765 *self)
{
	fdc_dprintf(5, "FDC: Read main status: %02x\n", self->fdc_mainstat);
	return self->fdc_mainstat;
}


/* Raise / lower the FDC's terminal count register */
void fdc_set_terminal_count(FDC_765 *self, fdc_byte value)
{
	if (self->fdc_terminal_count != value)
		fdc_dprintf(5, "FDC terminal count becomes %d\n", value);
	self->fdc_terminal_count = value;
	if (value && (self->fdc_mainstat & 0x20)) /* In execution phase? */
	{
		fdc_dprintf(5, "FDC: Comand stopped by terminal count\n");
		fdc_end_execution_phase(self);
	}
}

/* Start or stop drive motors */
void fdc_set_motor(FDC_765 *self, fdc_byte running)
{
	int oldmotor[4], newmotor[4];
	int n;

	fdc_dorcheck(self);	
	fdc_dprintf(3, "FDC: Setting motor states to %d %d %d %d\n",
			(running & 1), (running & 2) >> 1, (running & 4) >> 2,
			(running & 8) >> 3);
	/* Save the old motor states */
	for (n = 0; n < 4; n++) 
	    if (self->fdc_drive[n]) 
		 oldmotor[n] = self->fdc_drive[n]->fd_motor;
	    else oldmotor[n] = 0;

	/* Now start/stop the motors as appropriate. Note that these are
	 * the real drives  */
	if (self->fdc_drive[0]) self->fdc_drive[0]->fd_motor = (running&1)?1:0;
        if (self->fdc_drive[1]) self->fdc_drive[1]->fd_motor = (running&2)?1:0;
        if (self->fdc_drive[2]) self->fdc_drive[2]->fd_motor = (running&4)?1:0;
        if (self->fdc_drive[3]) self->fdc_drive[3]->fd_motor = (running&8)?1:0;

	/* Now get the new motor states */
        for (n = 0; n < 4; n++) 
            if (self->fdc_drive[n]) 
                 newmotor[n] = self->fdc_drive[n]->fd_motor;
            else newmotor[n] = 0;

	/* If motor of active drive hasn't changed, return */
	if (newmotor[self->fdc_curunit] == oldmotor[self->fdc_curunit]) return;

	n = newmotor[self->fdc_curunit];
	/* The status of the active drive is changing. Wait for a while, then
	 * interrupt */

	fdc_dprintf(5, "FDC: queued interrupt for drive motor change.\n");
	self->fdc_isr_countdown = LONGER_TIMEOUT;

	if (n) self->fdc_st0 &= 0xF7;	/* Motor -> on ; drive is ready */
	else   self->fdc_st0 |= 8;	/* Motor -> off; drive not ready*/
	fdc_get_st3(self);	/* Recalculate ST3 */

	/* FDC is doing something and the motor has stopped! */
	if ((self->fdc_mainstat & 0xF0) != 0x80 && (n == 0))
	{
		fdc_dprintf(5, "FDC: Motor stopped during command.\n");
		self->fdc_st0 |= 0xC0;
		fdc_end_execution_phase(self);	
	}
}


/* Called once every time round the emulator's main loop. Can interrupt if
 * the FDC has something to say. */
void fdc_tick(FDC_765 *self)
{
	if (!self->fdc_isr_countdown) return;
	--self->fdc_isr_countdown;
	if (!self->fdc_isr_countdown && self->fdc_isr)
	{
		(*self->fdc_isr)(self, 1);
	} 
}


/* Simulate the Digital Output Register in the IBM PC and clones
 * This is not part of the uPD765A itself, but part of the support 
 * circuitry.
 */ 
void fdc_write_dor(FDC_765 *self, int value)
{
/* The DOR bits are as follows:
 * Bits 7-4: Motors on or off 
 * Bit    3: Enable DMA (overrides the DMA-mode bit in commands)
 * Bit    2: ~Reset
 * Bits 1-0: Drive select (overrides the drive selector in commands);
 *          simulated by pointing all 4 drives to the same unit */
	int old_dor = self->fdc_dor;

	self->fdc_dor = value;
	fdc_dorcheck(self);
	if (value < 0) return;	/* DOR disabled */

	/* If this is the first DOR write, act as if all bits have changed */
	if (old_dor < 0) old_dor = ~value;

	/* The "motor" bits have changed */
	if ((value ^ old_dor) & 0xF0) 	
	{
		fdc_set_motor(self, value >> 4);
	}

	/* The "reset" bit has changed */
	if ( (value ^ old_dor) & 4)
	{
/* The PCW16 expects that the FDC, on finishing its reset, will
 * interrupt. Play along with it. */ 
		if (value & 4)	/* Coming out of reset	*/
		{
			self->fdc_st0 = 0xC0 | (self->fdc_st0 & 0x3F); /* failed? */
			self->fdc_mainstat = 0xD0;	/* Result phase */
			self->fdc_result_len = 1;
			self->fdc_result_pos = 0;
			self->fdc_result_buf[0] = self->fdc_st0;
			fdc_result_interrupt(self);	/* 0 bytes of results ready */
		}
		else	fdc_part_reset(self);	/* Entering reset */
	}
}


/* Set data rate */
void fdc_write_drr(FDC_765 *self, fdc_byte value)
{
	int n;
	
	for (n = 0; n < 4; n++)
	{
		if (self->fdc_drive[n])
			fd_set_datarate(self->fdc_drive[n], value);
	}
}

/* Simulate a PC-AT Digital Input Register which just sets bit 7 */
fdc_byte fdc_read_dir(FDC_765 *self)
{
	int drv;

	fdc_dorcheck(self);
// The changeline won't work without the DOR.
	if (self->fdc_dor < 0) 
	{
		fdc_dprintf(6, "fdc_read_dir: changeline=0 (no DOR)\n");
		return 0;
	}	

	drv = self->fdc_dor & 3;

	// No such drive => no changeline
	if (!self->fdc_drive[drv]) 
	{
		fdc_dprintf(6, "fdc_read_dir: changeline=0 (no drive %d)\n", drv);
		return 0;
	}	
	// Motor not running => no changeline
	if (!self->fdc_drive[drv]->fd_motor) 
	{
		fdc_dprintf(6, "fdc_read_dir: changeline=0 (motor off)\n");
		return 0;
	}	

        if (!fd_isready(self->fdc_drive[drv])) 
	{
		fdc_dprintf(6, "fdc_read_dir: changeline=1 (drive not ready)\n");
		return 0x80;
	}
	if (fd_changed(self->fdc_drive[drv]))
	{
		fdc_dprintf(6, "fdc_read_dir: changeline=1\n");
		return 0x80;
	}
	fdc_dprintf(6, "fdc_read_dir: changeline=0\n");
	return 0;
}



FDC_PTR fdc_new(void)
{
	FDC_PTR self = malloc(sizeof(FDC_765));

	if (!self) return NULL;
	fdc_reset(self);
	return self;	
}

void fdc_destroy(FDC_PTR *p)
{
	if (*p) free(*p);
	*p = NULL;
}


/* Reading FDC internal state */
fdc_byte fdc_readst0(FDC_PTR fdc) { return fdc->fdc_st0; }
fdc_byte fdc_readst1(FDC_PTR fdc) { return fdc->fdc_st1; }
fdc_byte fdc_readst2(FDC_PTR fdc) { return fdc->fdc_st2; }
fdc_byte fdc_readst3(FDC_PTR fdc) { return fdc->fdc_st3; }

int fdc_getunit(FDC_PTR fdc) { return fdc->fdc_curunit; }
int fdc_gethead(FDC_PTR fdc) { return fdc->fdc_curhead; }

void fdc_setisr(FDC_PTR self, FDC_ISR isr) 
{
	if (!self) return;
	self->fdc_isr = isr;
}

FDC_ISR fdc_getisr(FDC_PTR self)
{
	if (!self) return NULL;
	return self->fdc_isr;
}

/* The FDC's four drives. You must set these. */
FDRV_PTR fdc_getdrive(FDC_PTR self, int drive)
{
	if (self == NULL || drive < 0 || drive > 3) return NULL;
	return self->fdc_drive[drive];
}

void fdc_setdrive(FDC_PTR self, int drive, FDRV_PTR ptr)
{
	if (self == NULL || drive < 0 || drive > 3) return;
	self->fdc_drive[drive] = ptr;
}

