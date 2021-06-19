/*
 *	ZX Keyboard Adapter Emulation
 *
 *	Dummy for non GUI
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zxkey.h"

struct zxkey {
    unsigned int dummy;
};

/* The ZXKey feeds the low 8bits of the address into the decode and returns
   data based upon the keys active. It's all pulled up so invert everything */

uint8_t zxkey_scan(struct zxkey *zx, uint16_t addr)
{
    return 0xFF;
}

void zxkey_reset(struct zxkey *zx)
{
}

struct zxkey *zxkey_create(void)
{
    struct zxkey *zx = malloc(sizeof(struct zxkey));
    if (zx == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    return zx;
}

void zxkey_trace(struct zxkey *zx, int onoff)
{
}

