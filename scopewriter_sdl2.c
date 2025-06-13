#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "scopewriter.h"
#include "scopewriter_render.h"

struct scopewriter_renderer {
	struct scopewriter *sw;
	SDL_Renderer *render;
	SDL_Texture *texture;
	SDL_Window *window;
};

void scopewriter_render(struct scopewriter_renderer *render)
{
	SDL_Rect sr;

	sr.x = 0;
	sr.y = 0;
	sr.w = 256;
	sr.h = 32;

	SDL_UpdateTexture(render->texture, NULL, scopewriter_get_raster(render->sw), 1024);
	SDL_RenderClear(render->render);
	SDL_RenderCopy(render->render, render->texture, NULL, &sr);
	SDL_RenderPresent(render->render);
}

void scopewriter_renderer_free(struct scopewriter_renderer *render)
{
	if (render->texture)
		SDL_DestroyTexture(render->texture);
	free(render);
}

struct scopewriter_renderer *scopewriter_renderer_create(struct scopewriter *sw)
{
	struct scopewriter_renderer *render;

	render = malloc(sizeof(struct scopewriter_renderer));
	if (render == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(render, 0, sizeof(struct scopewriter_renderer));
	render->sw = sw;
	render->window = SDL_CreateWindow("Scopewriter",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		768, 96,
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
	SDL_RenderSetLogicalSize(render->render, 256, 32);

	render->texture = SDL_CreateTexture(render->render,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				256, 32);
	if (render->render == NULL) {
		fprintf(stderr, "Unable to create renderer: %s.\n", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render->render, 0, 0, 0, 255);
	SDL_RenderClear(render->render);
	SDL_RenderPresent(render->render);
	return render;
}
