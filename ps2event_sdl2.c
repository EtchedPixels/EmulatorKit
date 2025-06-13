/*
 *	Event glue to the PS/2 driver code
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "event.h"
#define PS2_INTERNAL
#include "ps2.h"
#include "ps2keymap.h"

static uint16_t ps2map(SDL_Scancode code)
{
	struct keymapping *k = keytable;
	while (k->code != SDL_SCANCODE_UNKNOWN) {
		if (k->code == code)
			return k->ps2;
		k++;
	}
	return 0;
}

static void make_ps2_code(SDL_Event *ev, struct ps2 *ps2)
{
	uint16_t code = ev->key.keysym.scancode;
	if (code > 255)
		return;
	code = ps2map(code);
	if (ev->type == SDL_KEYUP)
		ps2_queue_byte(ps2, 0xF0);
	if (code >> 8)
		ps2_queue_byte(ps2, code >> 8);
	ps2_queue_byte(ps2, code);
}

static int ps2_sdlevent(void *ps, void *evp)
{
	SDL_Event *ev = evp;
	struct ps2 *ps2 = ps;

	switch(ev->type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (ps2->window && ev->key.windowID != ps2->window)
				return 0;
			make_ps2_code(ev, ps2);
			return 1;
	}
	return 0;
}

void ps2_add_events(struct ps2 *ps2, uint32_t window)
{
	ps2->window = window;
	add_ui_handler(ps2_sdlevent, ps2);
}
