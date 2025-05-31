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
    uint32_t window;
};

#define MAX_KBD	32	/* Suitably silly */

static struct asciikbd kbdtab[MAX_KBD];
static unsigned kbd_next;

struct asciikbd *asciikbd_create(void)
{
    struct asciikbd *kbd;
    if (kbd_next == MAX_KBD) {
        fprintf(stderr, "Too many keyboards.\n");
        exit(1);
    }
    kbd = kbdtab + kbd_next++;
    kbd->ready = 0;
    kbd->key = '@';	/* Helps debug */
    kbd->wait = 0;
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

static struct asciikbd *asciikbd_find(uint32_t window_id)
{
	struct asciikbd *p = kbdtab;
	unsigned n = 0;
	while(n++ < kbd_next) {
		if (p->window == window_id || p->window == 0)
			return p;
		p++;
	}
	return NULL;
}

void asciikbd_event(void)
{
	SDL_Event ev;
	struct asciikbd *kbd;
	const char *p;

	while (SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_QUIT:
		        exit(1);
		case SDL_TEXTINPUT:
                	kbd = asciikbd_find(ev.text.windowID);
			if (kbd == NULL)
				break;
		        fflush(stdout);
                        p = ev.text.text;
                        asciikbd_insert(kbd, *p);
			break;
		/* Bletch TEXTINPUT doesn't cover newline etc */
		case SDL_KEYDOWN:
                	kbd = asciikbd_find(ev.key.windowID);
			if (kbd == NULL)
				break;
		        kbd->wait = 1;
		        break;
		case SDL_KEYUP:
                	kbd = asciikbd_find(ev.key.windowID);
			if (kbd == NULL)
				break;
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
                            if (ev.key.keysym.sym >= 64 && ev.key.keysym.sym < 128) {
                                if (ev.key.keysym.mod & KMOD_CTRL) {
                                    asciikbd_insert(kbd, (uint8_t)
                                        ev.key.keysym.sym & 0x1F);
                                    break;
                                }
                            }
                        }
                }
    	}
}
