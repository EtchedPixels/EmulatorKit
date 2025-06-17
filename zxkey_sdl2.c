/*
 *	ZX Keyboard Adapter Emulation
 *
 *	This is a basic implementation of the keymatrix driver.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "event.h"
#include "keymatrix.h"
#include "zxkey.h"

struct zxkey {
	unsigned type;
	struct keymatrix *matrix;
	uint8_t captureff;
};

static SDL_Keycode zxkeys[] = {
	/* Rows low to high, then columns low to high */
	/* ROW 0 (EFFF) */
	SDLK_1, SDLK_q, SDLK_0, SDLK_a, SDLK_p, SDLK_LSHIFT, SDLK_RETURN, SDLK_SPACE,
	/* ROW 1 (F7FF */
	SDLK_2, SDLK_w, SDLK_9, SDLK_s, SDLK_o, SDLK_z, SDLK_l, SDLK_RSHIFT,
	/* ROW 2 (FBFF) */
	SDLK_3, SDLK_e, SDLK_8, SDLK_d, SDLK_i, SDLK_x, SDLK_k, SDLK_m,
	/* ROW 3 (FDFF */
	SDLK_4, SDLK_r, SDLK_7, SDLK_f, SDLK_u, SDLK_c, SDLK_j, SDLK_n,
	/* ROW 4 (FEFF) */
	SDLK_5, SDLK_t, SDLK_6, SDLK_g, SDLK_y, SDLK_v, SDLK_h, SDLK_b
};

static SDL_Keycode speckeys[] = {
	/* 8 x 5 as the spectrum and ZX81 are arranged */
	SDLK_LSHIFT, SDLK_z, SDLK_x, SDLK_c, SDLK_v,	/* A8 */
	SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g,		/* A9 */
	SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t,		/* A10 */
	SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5,		/* A11 */
	SDLK_0, SDLK_9, SDLK_8, SDLK_7, SDLK_6,		/* A12 */
	SDLK_p, SDLK_o, SDLK_i, SDLK_u, SDLK_y,		/* A13 */
	SDLK_RETURN, SDLK_l, SDLK_k, SDLK_j, SDLK_h,	/* A14 */
	SDLK_SPACE, SDLK_RSHIFT, SDLK_m, SDLK_n, SDLK_b	/* A15 */
};

/* The ZXKey feeds the low 8bits of the address into the decode and returns
   data based upon the keys active. It's all pulled up so invert everything */

uint8_t zxkey_scan(struct zxkey *zx, uint16_t addr)
{
	if (zx->type == 2) {
		if ((addr & 0xFF) == 0xFF)
			return zx->captureff;
		/* FIXME: bits for American v US zx81 etc */
		return 0x00 | ~keymatrix_input(zx->matrix, ~(addr >> 8));
	}
	return ~keymatrix_input(zx->matrix, ~(addr >> 8));
}

void zxkey_write(struct zxkey *zx, uint8_t data)
{
	if (zx->type == 2)
		zx->captureff = data;
}

void zxkey_reset(struct zxkey *zx)
{
	keymatrix_reset(zx->matrix);
}

static int zxkey_SDL2event(void *dev, void *evp)
{
	struct zxkey *zx = dev;
	return keymatrix_SDL2event(zx->matrix, evp);
}

struct zxkey *zxkey_create(unsigned type)
{
	struct zxkey *zx = malloc(sizeof(struct zxkey));
	if (zx == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	if (type == 1)
		zx->matrix = keymatrix_create(5, 8, zxkeys);
	else
		zx->matrix = keymatrix_create(8, 5, speckeys);
	zx->type = type;

	add_ui_handler(zxkey_SDL2event, zx);
	return zx;
}

void zxkey_trace(struct zxkey *zx, int onoff)
{
	keymatrix_trace(zx->matrix, onoff);
}
