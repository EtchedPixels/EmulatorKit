/* w5100.c: Wiznet W5100 emulation

   Chopped up by Alan Cox out of the W5100 emulation layer for FUSE:

   Emulates a minimal subset of the Wiznet W5100 TCP/IP controller.

   Copyright (c) 2011 Philip Kendall

   $Id: w5100.h 4775 2012-11-26 23:03:36Z sbaldovi $

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

typedef struct nic_w5100_t nic_w5100_t;

nic_w5100_t* nic_w5100_alloc( void );
void nic_w5100_free( nic_w5100_t *self );

void nic_w5100_reset( nic_w5100_t *self );

uint8_t nic_w5100_read( nic_w5100_t *self, uint16_t reg);
void nic_w5100_write( nic_w5100_t *self, uint16_t reg, uint8_t b );
void w5100_process(nic_w5100_t *self);
