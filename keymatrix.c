/*
 *	A keyboard matrix input driver for SDL2
 *
 *	The basic principle is that we take the keycode and look it up
 *	in the provided matrix and then generate the fake keymatrix bits.
 *
 *	As far as our code is concerned high selects for testing and high
 *	bits indicate key down. The actual logic for keyboards varies and
 *	just needs the values inverting to suit in the emulation.
 *
 *	We assume the selection is by row and the bits by column. This is
 *	simmply semantics and for a keyboard that works the other way you
 *	can rotate the map 90 degress.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "event.h"
#include "keymatrix.h"

struct keymatrix {
	unsigned int rows;
	unsigned int cols;
	unsigned int size;
	SDL_Keycode *matrix;
	bool *status;
	int trace;
	void (*translate)(SDL_Event *ev);
};

/*
 *	Work out what this key is in the matrix
 */
static int keymatrix_find(struct keymatrix *km, SDL_Keycode keycode)
{
	SDL_Keycode *m = km->matrix;
	unsigned int n = 0;
	while(n < km->size) {
		if (*m++ == keycode)
			return n;
		n++;
	}
	return -1;

}

/*
 *	Update the matrix. We don't care about state change tracking and
 *	the like as all that the matrix interface provides is a current
 *	snapshot.
 */
static bool keymatrix_event(struct keymatrix *km, SDL_Keysym *keysym, bool down)
{
	int n = keymatrix_find(km, keysym->sym);
	/* Not a key we emulate */
	if (n == -1)
		return false;
	if (km->trace)
		fprintf(stderr, "Keysym %02x (%s) was mapped (%d, %d).\n",
			(unsigned int)keysym->sym,
			down ? "Down" : "Up",
			n / km->cols, n % km->cols);
	km->status[n] = down;
	return true;
}


/*
 *	Returns up to 8bits of input according to the current state of
 *	the key matrix. Currently ignores ghosting.
 */
uint8_t keymatrix_input(struct keymatrix *km, uint16_t scanbits)
{
	unsigned int row;
	unsigned int col;
	uint8_t r = 0;
	bool *p = km->status;

	/* Work out what byte code you get back. For the moment ignore ghosting
	   emulation */
	for (row = 0; row < km->rows; row++) {
		if (scanbits & (1 << row)) {
			for (col = 0; col < km->cols; col++) {
				if (*p++ == true) {
					if (km->trace)
						fprintf(stderr, "Key (%d,%d) was down and tested\n",
							row, col);
					r |= (1 << col);
				}
			}
		} else
			p += km->cols;
	}
	return r;
}

/*
 *	We consume the keyboard events only.
 */
bool keymatrix_SDL2event(struct keymatrix *km, SDL_Event *ev)
{
	if (ev->type == SDL_KEYDOWN)
		return keymatrix_event(km, &ev->key.keysym, true);
	if (ev->type == SDL_KEYUP)
		return keymatrix_event(km, &ev->key.keysym, false);
	return false;
}

static int keymatrix_handler(void *dev, void *evp)
{
	SDL_Event *ev = evp;
	struct keymatrix *km = dev;
	if (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) {
		if (km->translate)
			km->translate(ev);
	}
	if (keymatrix_SDL2event(km, ev))
		return 1;
	return 0;
}

void keymatrix_add_events(struct keymatrix *km)
{
	add_ui_handler(keymatrix_handler, km);
}

void keymatrix_free(struct keymatrix *km)
{
	free(km->status);
	free(km);
}

struct keymatrix *keymatrix_create(unsigned int rows, unsigned int cols, SDL_Keycode *matrix)
{
	struct keymatrix *km = malloc(sizeof(struct keymatrix));
	if (km == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(km, 0, sizeof(struct keymatrix));
	km->rows = rows;
	km->cols = cols;
	km->size = rows * cols;
	km->matrix = matrix;
	km->status = calloc(km->size, sizeof(bool));
	km->translate = NULL;
	if (km->status == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	return km;
}

void keymatrix_reset(struct keymatrix *km)
{
}

void keymatrix_trace(struct keymatrix *km, int onoff)
{
	km->trace = onoff;
}

void keymatrix_translator(struct keymatrix *km, void (*translate)(SDL_Event *ev))
{
	km->translate = translate;
}
