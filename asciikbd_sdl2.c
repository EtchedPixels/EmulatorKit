#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "asciikbd.h"

struct asciikbd {
    uint8_t key;
    unsigned int ready;
    unsigned int wait;
};

struct asciikbd *asciikbd_create(void)
{
    struct asciikbd *kbd = malloc(sizeof(struct asciikbd));
    if (kbd == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    kbd->ready = 0;
    kbd->key = '@';	/* Helps debug */
    kbd->wait = 0;
    return kbd;
}

void asciikbd_free(struct asciikbd *kbd)
{
    free(kbd);
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
void asciikbd_event(struct asciikbd *kbd)
{
	SDL_Event ev;
	const char *p;
	while (SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_QUIT:
		        exit(1);
		case SDL_TEXTINPUT:
		        fflush(stdout);
                        p = ev.text.text;
                        asciikbd_insert(kbd, *p);
			break;
		/* Bletch TEXTINPUT doesn't cover newline etc */
		case SDL_KEYDOWN:
		        kbd->wait = 1;
		        break;
		case SDL_KEYUP:
		        if (kbd->wait == 0)
		            break;
                        kbd->wait = 0;
		        switch(ev.key.keysym.sym) {
		        case SDLK_RETURN:
                        case SDLK_ESCAPE:
                        case SDLK_BACKSPACE:
                        case SDLK_TAB:
                            asciikbd_insert(kbd, (uint8_t)ev.key.keysym.sym);
                            break;
                        default:;
                        }
                }
    	}
}
