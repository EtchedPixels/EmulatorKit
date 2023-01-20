#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define WITH_SDL

#include "system.h"
#include "zxkey.h"
#include "ps2.h"

int sdl_live;

extern struct zxkey *zxkey;
extern struct ps2 *ps2;

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

static void make_ps2_code(SDL_Event *ev)
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

void ui_event(void)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_QUIT:
			emulator_done = 1;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (zxkey)
				zxkey_SDL2event(zxkey, &ev);
			if (ps2)
				make_ps2_code(&ev);
			break;
		}
	}
}

