/*
 *	TMS9918A driver raster output for SDL2
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "tms9918a.h"
#include "tms9918a_render.h"

extern int sdl_live;

static uint32_t vdp_ctab[16] = {
    0xFF000000,		/* transparent (we render as black) */
    0xFF000000,		/* black */
    0xFF20C020,		/* green */
    0xFF60D060,		/* light green */
    
    0xFF2020D0,		/* blue */
    0xFF4060D0,		/* light blue */
    0xFFA02020,		/* dark red */
    0xFF40C0D0,		/* cyan */
    
    0xFFD02020,		/* red */
    0xFFD06060,		/* light red */
    0xFFC0C020,		/* dark yellow */
    0xFFC0C080,		/* yellow */
    
    0xFF208020,		/* dark green */
    0xFFC040A0,		/* magneta */
    0xFFA0A0A0,		/* grey */
    0xFFD0D0D0		/* white */
};

struct tms9918a_renderer {
    struct tms9918a *vdp;
    SDL_Renderer *render;
    SDL_Texture *texture;
    SDL_Window *window;
};
    

void tms9918a_render(struct tms9918a_renderer *render)
{
    SDL_Rect sr;

    sr.x = 16;
    sr.y = 12;
    sr.w = 480;
    sr.h = 360;

    uint32_t background = render->vdp->colourmap[render->vdp->reg[7] & 0xf];
    uint8_t a = background >> 24;
    uint8_t r = background >> 16;
    uint8_t g = background >> 8;
    uint8_t b = background & 0xFF;
    SDL_SetRenderDrawColor(render->render, r, g, b, a);
    SDL_UpdateTexture(render->texture, NULL, tms9918a_get_raster(render->vdp), 1024);
    SDL_RenderClear(render->render);
    SDL_RenderCopyEx(render->render, render->texture, NULL, &sr, 0, NULL, SDL_FLIP_NONE);
    SDL_RenderPresent(render->render);
}

void tms9918a_renderer_free(struct tms9918a_renderer *render)
{
    if (render->texture)
        SDL_DestroyTexture(render->texture);
    free(render);
}


struct tms9918a_renderer *tms9918a_renderer_create(struct tms9918a *vdp)
{
    struct tms9918a_renderer *render;

    /* We will need a nicer way to do this once we have multiple SDL using
       devices */
    if (sdl_live == 0) {
        sdl_live = 1;
        if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
            fprintf(stderr, "SDL init failed: %s.\n", SDL_GetError());
            exit(1);
        }
        atexit(SDL_Quit);
    }

    render = malloc(sizeof(struct tms9918a_renderer));
    if (render == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    memset(render, 0, sizeof(struct tms9918a_renderer));
    render->vdp = vdp;
    tms9918a_set_colourmap(vdp, vdp_ctab);
    render->window = SDL_CreateWindow("TMS9918A",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        512, 384,
        SDL_WINDOW_RESIZABLE);
    if (render->window == NULL) {
        fprintf(stderr, "Unable to create window: %s.\n", SDL_GetError());
        exit(1);
    }
    render->render = SDL_CreateRenderer(render->window, -1, SDL_RENDERER_ACCELERATED);
    if (render->render == NULL) {
        fprintf(stderr, "Unable to create renderer: %s.\n", SDL_GetError());
        exit(1);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize(render->render, 512, 384);

    render->texture = SDL_CreateTexture(render->render,
                        SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_STREAMING,
                        256, 192);
    if (render->render == NULL) {
        fprintf(stderr, "Unable to create renderer: %s.\n", SDL_GetError());
        exit(1);
    }
    SDL_SetRenderDrawColor(render->render, 0, 0, 0, 255);
    SDL_RenderClear(render->render);
    SDL_RenderPresent(render->render);
    return render;
}
