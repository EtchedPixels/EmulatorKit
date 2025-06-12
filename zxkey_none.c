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
	unsigned int type;
	uint8_t captureff;
};

/* The ZXKey feeds the low 8bits of the address into the decode and returns
   data based upon the keys active. It's all pulled up so invert everything */

uint8_t zxkey_scan(struct zxkey *zx, uint16_t addr)
{
	if (zx->type == 2 && ((addr & 0xFF) == 0xFF))
		return zx->captureff;
	return 0xFF;
}

void zxkey_write(struct zxkey *zx, uint8_t data)
{
	if (zx->type == 2)
		zx->captureff = data;
}

void zxkey_reset(struct zxkey *zx)
{
}

struct zxkey *zxkey_create(unsigned type)
{
	struct zxkey *zx = malloc(sizeof(struct zxkey));
	if (zx == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	zx->type = type;
	return zx;
}

void zxkey_trace(struct zxkey *zx, int onoff)
{
}

