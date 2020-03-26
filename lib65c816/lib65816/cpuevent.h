#ifndef LIB65816_CPUEVENT_H
#define LIB65816_CPUEVENT_H

/* CPUtrel 2 Emulator Release 1p2
 * Copyright (c) 2007 Samuel A. Falvo II
 * See LICENSE file for more information.
 */

#include <lib65816/cpu.h>

typedef struct CPUEvent CPUEvent;
typedef void CPUHandler( word32 timestamp );

struct CPUEvent
{
    CPUEvent *      next;
    CPUEvent *      previous;
    long            counter;
    CPUHandler *    handler;
};

void CPUEvent_elapse( word32 delta );
void CPUEvent_dispatch( void );
void CPUEvent_initialize( void );
void CPUEvent_schedule( CPUEvent *, word32, CPUHandler * );

#endif

