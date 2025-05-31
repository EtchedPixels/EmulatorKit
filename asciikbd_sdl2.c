#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "event.h"
#include "asciikbd.h"

struct asciikbd {
	uint8_t key;
	unsigned int ready;
	unsigned int wait;
	uint32_t window;
};

static int asciikbd_event(void *priv, void *evp);

struct asciikbd *asciikbd_create(void)
{
	struct asciikbd *kbd = malloc(sizeof(struct asciikbd));
	if (kbd == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	kbd->ready = 0;
	kbd->key = '@';		/* Helps debug */
	kbd->window = 0;
	kbd->wait = 0;
	add_ui_handler(asciikbd_event, kbd);
	return kbd;
}

void asciikbd_free(struct asciikbd *kbd)
{
	/* Not really used or supported TODO */
}

unsigned int asciikbd_ready(struct asciikbd *kbd)
{
	return kbd->ready;
}

uint8_t asciikbd_read(struct asciikbd *kbd)
{
	return kbd->key;
}

void asciikbd_ack(struct asciikbd *kbd)
{
	kbd->ready = 0;
}

static void asciikbd_insert(struct asciikbd *kbd, uint8_t c)
{
	if (kbd->ready)
		return;
	kbd->key = c;
	if (c < 128)
		kbd->ready = 1;
}

void asciikbd_bind(struct asciikbd *kbd, uint32_t window_id)
{
	kbd->window = window_id;
}

static int asciikbd_event(void *priv, void *evp)
{
	SDL_Event *ev = evp;
	struct asciikbd *kbd = priv;
	const char *p;

	switch (ev->type) {
	case SDL_TEXTINPUT:
		if (kbd->window && kbd->window != ev->text.windowID)
			return 0;
		fflush(stdout);
		p = ev->text.text;
		asciikbd_insert(kbd, *p);
		return 1;
		/* Bletch TEXTINPUT doesn't cover newline etc */
	case SDL_KEYDOWN:
		if (kbd->window && kbd->window != ev->key.windowID)
			return 0;
		kbd->wait = 1;
		return 1;
	case SDL_KEYUP:
		if (kbd->window && kbd->window != ev->key.windowID)
			return 0;
		if (kbd->wait == 0)
			return 1;
		kbd->wait = 0;
		switch (ev->key.keysym.sym) {
		case SDLK_RETURN:
		case SDLK_ESCAPE:
		case SDLK_BACKSPACE:
		case SDLK_TAB:
			asciikbd_insert(kbd, (uint8_t) ev->key.keysym.sym);
			return 1;
		default:;
			if (ev->key.keysym.sym >= 64
			    && ev->key.keysym.sym < 128) {
				if (ev->key.keysym.mod & KMOD_CTRL) {
					asciikbd_insert(kbd, (uint8_t)ev->key.keysym.sym & 0x1F);
					return 1;
				}
			}
		}
	}
	return 0;
}
