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
#include "vtfont.h"		/* Quick hack to start off */
#include "serialdevice.h"
#include "event.h"
#include "asciikbd.h"
#include "vtcon.h"

#define CWIDTH	8
#define CHEIGHT 16

struct vtcon {
    struct serial_device dev;
    struct asciikbd *kbd;
    unsigned type;
    uint8_t video[2048];
    unsigned state;
    uint8_t s1, s2;
    unsigned y, x;
    SDL_Window *window;
    SDL_Renderer *render;
    SDL_Texture *texture;
    uint32_t bitmap[80 * CWIDTH * 24 * CHEIGHT];
    const char *name;
};

/* FIXME: We should have some kind of ui_event timer chain for this ! */

static void vtchar(struct vtcon *v, unsigned y, unsigned x, uint8_t c)
{
    const uint8_t *fp = vtfont + 8 * c;	/* We make a 16 pixel char from 8 */
    uint32_t *pixp = v->bitmap + x * CWIDTH + 80 * y * CHEIGHT * CWIDTH;
    unsigned rows, pixels;

    for (rows = 0; rows < CHEIGHT / 2; rows ++) {
        uint8_t bits = *fp;
        if (y == v->y && x == v->x)
            bits ^= 0xFF;
        for (pixels = 0; pixels < CWIDTH; pixels++) {
            if (bits & 0x80)
                *pixp++ = 0xFFFFBB0A;
            else
                *pixp++ = 0xFF0A0A0A;
            bits <<= 1;
        }
        /* To next line */
        pixp += 79 * CWIDTH;

        bits = *fp++;
        if (y == v->y && x == v->x)
            bits ^= 0xFF;
        for (pixels = 0; pixels < CWIDTH; pixels++) {
            if (bits & 0x80)
                *pixp++ = 0xFFCCA20A;
            else
                *pixp++ = 0xFF060606;
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
    memset(v->video + 80 * 23, ' ', 80);
}

static void vtbackscroll(struct vtcon *v)
{
    memmove(v->video + 80, v->video, 2048 - 80);
    memset(v->video, ' ', 80);
}

static unsigned vtcon_ready(struct serial_device *dev)
{
    struct vtcon *v = dev->private;
    if (v->kbd && asciikbd_ready(v->kbd))
        return 3;
    return 2;
}

static int vtcon_refresh(void *dev, void *evp)
{
    struct vtcon *v = dev;
    SDL_Event *ev = evp;

    if (ev->type == SDL_WINDOWEVENT) {
        if (SDL_GetWindowID(v->window) == ev->window.windowID) {
            switch(ev->window.event) {
                case SDL_WINDOWEVENT_SHOWN:
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    vtraster(v);
            }
        }
    }
    return 0;
}

static void vtcon_init(struct vtcon *v)
{
    v->kbd = asciikbd_create();
    v->window = SDL_CreateWindow(v->name, 
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        80 * CWIDTH, 24 * CHEIGHT,
        SDL_WINDOW_RESIZABLE);
    if (v->window == NULL) {
        fprintf(stderr, "vt: unable to open window: %s\n", SDL_GetError());
        exit(1);
    }
    v->render = SDL_CreateRenderer(v->window, -1, 0);
    if (v->render == NULL) {
        fprintf(stderr, "vt: unable to create renderer: %s\n", SDL_GetError());
        exit(1);
    }
    v->texture = SDL_CreateTexture(v->render, SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    80 * CWIDTH, 24 * CHEIGHT);
    if (v->texture == NULL) {
        fprintf(stderr, "vt: unable to create texture: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetRenderDrawColor(v->render, 0, 0, 0, 255);
    SDL_RenderClear(v->render);
    SDL_RenderPresent(v->render);
    SDL_RenderSetLogicalSize(v->render, 80 * CWIDTH, 24 * CHEIGHT);
    asciikbd_bind(v->kbd, SDL_GetWindowID(v->window));
    add_ui_handler(vtcon_refresh, v);
}

static void vtput(struct vtcon *v, uint8_t c)
{
    v->video[80 * v->y + v->x] = c;
}

static void vtcon_put_dumb(struct serial_device *dev, uint8_t c)
{
    struct vtcon *v = dev->private;
    if (v->window == NULL)
        vtcon_init(v);
    if (c == 13) {
        v->x = 0;
        return;
    }
    if (c == 10) {
        v->y++;
        if (v->y == 24) {
            vtscroll(v);
            v->y = 23;
        }
        vtraster(v);
        return;
    }
    if (c == 8) {
        if (v->x)
            v->x--;
        return;
    }
            
    v->x++;
    if (v->x == 80) {
        v->x = 0;
        v->y++;
        if (v->y == 24) {
            vtscroll(v);
            v->y = 23;
        }
    }
    vtraster(v);
}

static void vt52_clearacross(struct vtcon *v)
{
    memset(v->video + 80 * v->y + v->x, ' ', 80 - v->x);
}

static void vt52_cleareop(struct vtcon *v)
{
    memset(v->video + 80 * v->y + v->x, ' ', 80 - v->x);
    if (v->y < 23)
        memset(v->video + 80 * v->y + 1, ' ', 80 * (23 - v->y));
}

static void vtcon_put_vt52(struct serial_device *dev, uint8_t c)
{
    struct vtcon *v = dev->private;
    if (v->window == NULL)
        vtcon_init(v);
    switch(v->state) {
    case 0:	/* Ground state */
        if (c > 31) {
            vtput(v, c);
            v->x++;
            if (v->x == 80) {
                v->x = 0;
                if (v->y == 23)
                    vtscroll(v);
                else
                    v->y++;
            }
            vtraster(v);
            return;
        }
        /* Control codes */
        switch(c) {
        case 7:	/* beep (well more gronk) */
            break;
        case 8:
            if (v->x)
                v->x--;
            else if (v->y)
                v->y--;
            break;
        case 9:
            do {
                vtput(v, c);
                v->x++;
                if (v->x == 80) {
                    v->x = 0;
                    if (v->y == 23)
                        vtscroll(v);
                    else
                        v->y++;
                }
            } while(v->x & 7);
            break;
        case 10:
            if (v->y == 23)
                vtscroll(v);
            else
                v->y++;
            break;
        case 13:
            v->x = 0;
            break;
        case 0x1B:
            v->state = 1;
            break;
        }
        vtraster(v);
        break;
    case 1:	/* Escape */
        switch(c) {
            case 'A':
                if (v->y)
                    v->y--;
                break;
            case 'B':
                if (v->y < 23)
                    v->y++;
                break;
            case 'C':
                if (v->x < 79)
                    v->x++;
                break;
            case 'D':
                if (v->x)
                    v->x--;
                break;
            case 'E':
                memset(v->video, ' ', 2048);
                break;
            case 'H':
                v->x = 0;
                v->y = 0;
                break;
            case 'I':
                if (v->y)
                    v->y--;
                else { 
                    vtbackscroll(v);
                    memset(v->video, ' ', 80);
                }
                break;
            case 'J':
                vt52_cleareop(v);
                break;
            case 'K':
                vt52_clearacross(v);
                break;
            case 'Y':
                v->state = 2;
                break;
            default:
                v->state = 0;
                return;
        }
        vtraster(v);
        break;
    case 2:	/* Escape Y */
        v->s1 = c;
        v->state++;
        break;
    case 3:	/* Escape Y ch */
        if (v->s1 >= ' ' && v->s1 < ' ' + 24)
            v->y = v->s1 - ' ';
        if (c >= ' ' && c < ' ' + 80)
            v->x = c - ' ';
        vtraster(v);
        break;
    }
}

static uint8_t vtcon_get(struct serial_device *dev)
{
    struct vtcon *v = dev->private;
    uint8_t r = 0xFF;
    if (v->kbd) {
        r = asciikbd_read(v->kbd);
        asciikbd_ack(v->kbd);
    }
    return r;
}

struct serial_device *vt_create(const char *name, unsigned type)
{
    struct vtcon *dev = malloc(sizeof(struct vtcon));
    dev->dev.private = dev;
    dev->dev.name = "Terminal";
    dev->dev.get = vtcon_get;
    switch(type) {
    case CON_DUMB:
        dev->dev.put = vtcon_put_dumb;
        break;
    case CON_VT52:
        dev->dev.put = vtcon_put_vt52;
        break;
    }
    dev->dev.ready = vtcon_ready;
    dev->x = 0;
    dev->y = 0;
    memset(dev->video, ' ', sizeof(dev->video));
    dev->window = NULL;
    dev->name = name;
    return &dev->dev;
}
