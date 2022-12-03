#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define WITH_SDL

#include "system.h"

/* The Nabu has some non-PC keys we map them as

	No : F1
	Yes : F2
	Sym : Alt
	TV: F3
	<== and ==> to KP 4 / 6 as well as the audio next/prev keys if present
*/

extern void nabupc_queue_key(uint8_t c);

/* Only a few special keys send a down message */
static void nabu_encode_down(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
		case SDLK_RIGHT:
			nabupc_queue_key(0xE0);
			break;
		case SDLK_LEFT:
			nabupc_queue_key(0xE1);
			break;
		case SDLK_UP:
			nabupc_queue_key(0xE2);
			break;
		case SDLK_DOWN:
			nabupc_queue_key(0xE3);
			break;
		case SDLK_KP_4:
		case SDLK_AUDIOPREV:
			nabupc_queue_key(0xE4);
			break;
		case SDLK_KP_6:
		case SDLK_AUDIONEXT:
			nabupc_queue_key(0xE5);
			break;
		case SDLK_F1:	/* No */
			nabupc_queue_key(0xE6);
			break;
		case SDLK_F2:	/* Yes */
			nabupc_queue_key(0xE7);
			break;
		case SDLK_LALT:	/* Sym */
		case SDLK_RALT:
			nabupc_queue_key(0xE8);
			break;
		case SDLK_PAUSE:
			nabupc_queue_key(0xE9);
			break;
		case SDLK_F3:	/* TV/NABU */
			nabupc_queue_key(0xEA);
			break;
		default:
			break;
	}
}

/* Mostly ASCII but we need to map some keys to the NabuPC special keys */

static void nabu_encode_key(SDL_Event *ev)
{
	unsigned code = ev->key.keysym.sym;

	if (code < 128)
		nabupc_queue_key(code);
	switch(code)
	{
		case SDLK_RIGHT:
			nabupc_queue_key(0xF0);
			break;
		case SDLK_LEFT:
			nabupc_queue_key(0xF1);
			break;
		case SDLK_UP:
			nabupc_queue_key(0xF2);
			break;
		case SDLK_DOWN:
			nabupc_queue_key(0xF3);
			break;
		case SDLK_KP_4:
		case SDLK_AUDIOPREV:
			nabupc_queue_key(0xF4);
			break;
		case SDLK_KP_6:
		case SDLK_AUDIONEXT:
			nabupc_queue_key(0xF5);
			break;
		case SDLK_F1:	/* No */
			nabupc_queue_key(0xF6);
			break;
		case SDLK_F2:	/* Yes */
			nabupc_queue_key(0xF7);
			break;
		case SDLK_LALT:	/* Sym */
		case SDLK_RALT:
			nabupc_queue_key(0xF8);
			break;
		case SDLK_PAUSE:
			nabupc_queue_key(0xF9);
			break;
		case SDLK_F3:	/* TV/NABU */
			nabupc_queue_key(0xFA);
			break;
		default:
			break;
	}
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
			nabu_encode_down(&ev);
			break;
		case SDL_KEYUP:
			nabu_encode_key(&ev);
			break;
		}
	}
}
