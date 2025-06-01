/*
 *	Plug console interface into a terminal
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <SDL2/SDL.h>
#include "nasfont.h"		/* Quick hack to start off */
#include "serialdevice.h"
#include "event.h"
#include "asciikbd.h"
#include "vtcon.h"

#define CWIDTH	8
#define CHEIGHT 16

struct vtcon {
    struct serial_device dev;
    struct asciikbd *kbd;
    uint8_t video[2048];
    unsigned y, x;
    SDL_Window *window;
    SDL_Renderer *render;
    SDL_Texture *texture;
    uint32_t bitmap[80 * CWIDTH * 24 * CHEIGHT];
};

/* FIXME: We should have some kind of ui_event timer chain for this ! */

static void vtchar(struct vtcon *v, unsigned y, unsigned x, uint8_t c)
{
    uint8_t *fp = nascom_font_raw + CHEIGHT * c;
    uint32_t *pixp = v->bitmap + x * CWIDTH + 80 * y * CHEIGHT * CWIDTH;
    unsigned rows, pixels;

    for (rows = 0; rows < CHEIGHT; rows ++) {
        uint8_t bits = *fp++;
        for (pixels = 0; pixels < CWIDTH; pixels++) {
            if (bits & 0x80)
                *pixp++ = 0xFF10D010;
            else
                *pixp++ = 0xFF080808;
            bits <<= 1;
        }
        /* To next line */
        pixp += 79 * CWIDTH;
    }
}

static void vtraster(struct vtcon *v)
{
    SDL_Rect rect;
    const uint8_t *p = v->video;
    unsigned y, x;
    for (y = 0; y < 24; y++)
        for (x = 0; x < 80; x++)
            vtchar(v, y, x, *p++);
    
    rect.x = rect.y = 0;
    rect.w = 80 * CWIDTH;
    rect.h = 24 * CHEIGHT;

    SDL_UpdateTexture(v->texture, NULL, v->bitmap, 80 * CWIDTH * 4);
    SDL_RenderClear(v->render);
    SDL_RenderCopy(v->render, v->texture, NULL, &rect);
    SDL_RenderPresent(v->render);
}

static void vtscroll(struct vtcon *v)
{
    memmove(v->video, v->video + 80, 2048 - 80);
}

static unsigned vtcon_ready(struct serial_device *dev)
{
    struct vtcon *v = dev->private;
    if (asciikbd_ready(v->kbd))
        return 3;
    return 2;
}

static void vtcon_put(struct serial_device *dev, uint8_t c)
{
    struct vtcon *v = dev->private;
    /* Very dumb tty for now */
    if (c == 13) {
        v->x = 0;
        return;
    }
    if (c == 10) {
        v->y++;
        if (v->y == 25) {
            vtscroll(v);
            v->y = 24;
        }
        vtraster(v);
        return;
    }
    if (c == 8) {
        if (v->x)
            v->x--;
        return;
    }
            
    v->video[80 * v->y + v->x] = c;
    v->x++;
    if (v->x == 80) {
        v->x = 0;
        v->y++;
        if (v->y == 25) {
            vtscroll(v);
            v->y = 24;
        }
    }
    vtraster(v);
}

static uint8_t vtcon_get(struct serial_device *dev)
{
    struct vtcon *v = dev->private;
    uint8_t r = asciikbd_read(v->kbd);
    asciikbd_ack(v->kbd);
    return r;
}

struct serial_device *create_vt(const char *name)
{
    struct vtcon *dev = malloc(sizeof(struct vtcon));
    dev->dev.private = dev;
    dev->dev.name = "Terminal";
    dev->dev.get = vtcon_get;
    dev->dev.put = vtcon_put;
    dev->dev.ready = vtcon_ready;
    dev->kbd = asciikbd_create();
    dev->x = 0;
    dev->y = 0;
    memset(dev->video, '@', sizeof(dev->video));
    dev->window = SDL_CreateWindow(name, 
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        80 * CWIDTH, 24 * CHEIGHT,
        SDL_WINDOW_RESIZABLE);
    if (dev->window == NULL) {
        fprintf(stderr, "vt: unable to open window: %s\n", SDL_GetError());
        exit(1);
    }
    dev->render = SDL_CreateRenderer(dev->window, -1, 0);
    if (dev->render == NULL) {
        fprintf(stderr, "vt: unable to create renderer: %s\n", SDL_GetError());
        exit(1);
    }
    dev->texture = SDL_CreateTexture(dev->render, SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    80 * CWIDTH, 24 * CHEIGHT);
    if (dev->texture == NULL) {
        fprintf(stderr, "vt: unable to create texture: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetRenderDrawColor(dev->render, 0, 0, 0, 255);
    SDL_RenderClear(dev->render);
    SDL_RenderPresent(dev->render);
    SDL_RenderSetLogicalSize(dev->render, 80 * CWIDTH, 24 * CHEIGHT);
    vtraster(dev);
    asciikbd_bind(dev->kbd, SDL_GetWindowID(dev->window));
    return &dev->dev;
}
