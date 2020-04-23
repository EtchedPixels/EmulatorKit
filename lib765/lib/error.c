/* 765: Library to emulate the uPD765a floppy controller (aka Intel 8272)

    Copyright (C) 2002  John Elliott <jce@seasip.demon.co.uk>

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
#include <stdarg.h>
#include "765.h"

/* The function to call on errors */
lib765_error_function_t lib765_error_function =
	lib765_default_error_function;

void fdc_dprintf(int debuglevel, char *fmt, ...)
{
	va_list ap;

	/* If we don't have an error function, do nothing */
	if( !lib765_error_function ) return;

	/* Otherwise, call that error function */
	va_start( ap, fmt );
	lib765_error_function( debuglevel, fmt, ap );
	va_end( ap );
}

/* Default error action is just to print a message to stderr */
void
lib765_default_error_function(int debuglevel, char *fmt, va_list ap)
{
	/* Let's say default action is level 1; showing all messages
	 * would be just too horribly disturbing. */
	
	if (debuglevel > 9999) return;	
	fprintf( stderr, "lib765 level:%d error: ", debuglevel );
	vfprintf( stderr, fmt, ap );
	fprintf( stderr, "\n" );
}

void lib765_register_error_function(lib765_error_function_t ef)
{
	lib765_error_function = ef;
}
