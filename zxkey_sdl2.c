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

#include "keymatrix.h"
#include "zxkey.h"

struct zxkey {
 struct keymatrix *matrix;
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

/* The ZXKey feeds the low 8bits of the address into the decode and returns
   data based upon the keys active. It's all pulled up so invert everything */

uint8_t zxkey_scan(struct zxkey *zx, uint16_t addr)
{
    return ~keymatrix_input(zx->matrix, ~(addr >> 8));
}

void zxkey_reset(struct zxkey *zx)
{
    keymatrix_reset(zx->matrix);
}

struct zxkey *zxkey_create(void)
{
    struct zxkey *zx = malloc(sizeof(struct zxkey));
    if (zx == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    zx->matrix = keymatrix_create(5, 8, zxkeys);
    return zx;
}

void zxkey_trace(struct zxkey *zx, int onoff)
{
    keymatrix_trace(zx->matrix, onoff);
}

bool zxkey_SDL2event(struct zxkey *zx, SDL_Event *ev)
{
   return keymatrix_SDL2event(zx->matrix, ev);
}
