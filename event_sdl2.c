#include <stdio.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#include "event.h"

struct sdl_handler
{
	int (*handler)(void *priv, void *ev);
	void *private;
};

#define MAX_HANDLER	64

static struct sdl_handler sdl_handler[MAX_HANDLER];
static unsigned next_handler;

void add_ui_handler(int (*handler)(void *priv, void *ev), void *private)
{
	if (next_handler == MAX_HANDLER) {
		fprintf(stderr, "event: too many event handlers.\n");
		exit(1);
	}
	sdl_handler[next_handler].handler = handler;
	sdl_handler[next_handler].private = private;
	next_handler++;
}

void remove_ui_handler(int (*handler)(void *priv, void *ev), void *private)
{
	fprintf(stderr, "event: event removal not yet supported.\n");
	exit(1);
}

static void handler(SDL_Event *ev)
{
	struct sdl_handler *f = sdl_handler;
	unsigned n = 0;
	while (n++ < next_handler) {
		if (f->handler(f->private, ev))
			break;
		f++;
	}
}

unsigned ui_event(void)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_QUIT:
			return 1;
		}
		handler(&ev);
	}
	return 0;
}

void ui_init(void)
{
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		fprintf(stderr, "SDL init failed: %s.\n", SDL_GetError());
		exit(1);
	}
}

