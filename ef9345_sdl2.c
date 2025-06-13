/*
 *	6847 driver raster output for SDL2
 *
 *	There are lots of variants of this device that have subtle differences
 *	including NTSC and PAL versions.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "ef9345.h"
#include "ef9345_render.h"

/* RGB colours : only 8 used as we ignore the I hack */
static uint32_t ef9345_ctab[16] = {
	0xFF000000,
	0xFFFF0000,
	0xFF00FF00,
	0xFFFFFF00,
	0xFF0000FF,
	0xFF00FFFF,
	0xFFFFFF00,
	0xFFFFFFFF
};

struct ef9345_renderer {
	struct ef9345 *ef9345;
	SDL_Renderer *render;
	SDL_Texture *texture;
	SDL_Window *window;
};


void ef9345_render(struct ef9345_renderer *render)
{
	SDL_Rect sr;

	sr.x = 0;
	sr.y = 0;
	sr.w = 492;	/* 336 in 40 col mode */
	sr.h = 280;	/* Less for NTSC */

	SDL_UpdateTexture(render->texture, NULL, ef9345_get_raster(render->ef9345), 492 * 4);
	SDL_RenderClear(render->render);
	SDL_RenderCopy(render->render, render->texture, NULL, &sr);
	SDL_RenderPresent(render->render);
}

void ef8345_renderer_free(struct ef9345_renderer *render)
{
	if (render->texture)
		SDL_DestroyTexture(render->texture);
}


struct ef9345_renderer *ef9345_renderer_create(struct ef9345 *ef9345)
{
	struct ef9345_renderer *render;

	render = malloc(sizeof(struct ef9345_renderer));
	if (render == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(render, 0, sizeof(struct ef9345_renderer));
	render->ef9345 = ef9345;
	ef9345_set_colourmap(ef9345, ef9345_ctab);
	render->window = SDL_CreateWindow("EF9345",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		492 * 2, 560,
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
	SDL_RenderSetLogicalSize(render->render, 492, 280);

	render->texture = SDL_CreateTexture(render->render,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				492, 280);
	if (render->render == NULL) {
		fprintf(stderr, "Unable to create renderer: %s.\n", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render->render, 0, 0, 0, 255);
	SDL_RenderClear(render->render);
	SDL_RenderPresent(render->render);
	return render;
}
