/*
 *	TFT driver raster output for SDL2
 *
 *	There are lots of variants of this device that have subtle differences
 *	including NTSC and PAL versions.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "tft_dumb.h"
#include "tft_dumb_render.h"

struct tft_renderer {
	struct tft_dumb *tft;
	SDL_Renderer *render;
	SDL_Texture *texture;
	SDL_Window *window;
};

void tft_render(struct tft_renderer *render)
{
	SDL_Rect sr;

	sr.x = 0;
	sr.y = 0;
	sr.w = render->tft->width;
	sr.h = render->tft->height;

	SDL_UpdateTexture(render->texture, NULL,
		render->tft->rasterbuffer, sr.w * sizeof(uint32_t));
	SDL_RenderClear(render->render);
	SDL_RenderCopy(render->render, render->texture, NULL, &sr);
	SDL_RenderPresent(render->render);
}

void tft_renderer_free(struct tft_renderer *render)
{
	if (render->texture)
		SDL_DestroyTexture(render->texture);
}


struct tft_renderer *tft_renderer_create(struct tft_dumb *tft)
{
	struct tft_renderer *render;

	render = malloc(sizeof(struct tft_renderer));
	if (render == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(render, 0, sizeof(struct tft_renderer));
	render->tft = tft;
	render->window = SDL_CreateWindow("TFT",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		tft->width, tft->height,
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
	SDL_RenderSetLogicalSize(render->render, tft->width, tft->height);

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
