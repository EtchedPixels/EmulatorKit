#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "dgvideo.h"
#include "dgvideo_render.h"

struct dgvideo_renderer {
	struct dgvideo *dg;
	SDL_Renderer *render;
	SDL_Texture *texture;
	SDL_Window *window;
};

void dgvideo_render(struct dgvideo_renderer *render)
{
	SDL_Rect sr;

	sr.x = 0;
	sr.y = 0;
	sr.w = 256;
	sr.h = 128;

	SDL_UpdateTexture(render->texture, NULL, dgvideo_get_raster(render->dg), 1024);
	SDL_RenderClear(render->render);
	SDL_RenderCopy(render->render, render->texture, NULL, &sr);
	SDL_RenderPresent(render->render);
}

void dgvideo_renderer_free(struct dgvideo_renderer *render)
{
	if (render->texture)
		SDL_DestroyTexture(render->texture);
	free(render);
}

struct dgvideo_renderer *dgvideo_renderer_create(struct dgvideo *dg)
{
	struct dgvideo_renderer *render;

	render = malloc(sizeof(struct dgvideo_renderer));
	if (render == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(render, 0, sizeof(struct dgvideo_renderer));
	render->dg = dg;
	render->window = SDL_CreateWindow("DGVideo",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		768, 384,
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
	SDL_RenderSetLogicalSize(render->render, 256, 128);

	render->texture = SDL_CreateTexture(render->render,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				256, 128);
	if (render->render == NULL) {
		fprintf(stderr, "Unable to create renderer: %s.\n", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(render->render, 0, 0, 0, 255);
	SDL_RenderClear(render->render);
	SDL_RenderPresent(render->render);
	return render;
}
