/* types.h
 */


#ifndef TYPES_H
#define TYPES_H

#ifdef z80

#include <ansi.h>

/* Types for Hi-Tech C, z80, 8 bit
 */
typedef unsigned char  uint8;
typedef unsigned int   uint16;
typedef unsigned long  uint32;
typedef signed char    int8;
typedef int            int16;
typedef long           int32;

#else

/*
 *	ANSI C types
 */

#include <stdint.h>

typedef uint8_t	       uint8;
typedef uint16_t       uint16;
typedef uint32_t       uint32;
typedef int8_t	       int8;
typedef int16_t        int16;
typedef int32_t        int32;

#endif

#endif
