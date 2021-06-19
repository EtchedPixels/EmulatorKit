#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define WITH_SDL

#include "system.h"
#include "zxkey.h"

extern struct zxkey *zxkey;

void ui_event(void)
{
	SDL_Event ev;
	if (SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_QUIT:
			emulator_done = 1;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			zxkey_SDL2event(zxkey, &ev);
			break;
		}
	}
}

