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

#include "6847.h"
#include "6847_render.h"

/*
 * The 6847 set up is odd - there are 8 colours in the encoding plus black
 * which has a life of its own and we keep in slot 8
 *
 * Ignore colour artifacting for the moment
 *
 * These colours seem to be very subjective and system dependent and
 * in reality also shift a bit by mode and PAL/NTSC.
 */

static uint32_t vdp_ctab[9] = {
	0xFF30D200,	/* Green */
	0xFFC1E500,	/* Yellow */
	0xFF4C3AB4,	/* Blue */
	0xFF9A3236,	/* Red */
	0xFFBFC8AD,	/* "Buff" */
	0xFF41AF71,	/* Cyan */
	0xFFC84EF0,	/* Magenta */
	0xFFD47F00,	/* Orange/Brown */
	0xFF263016,	/* Black */
};

struct m6847_renderer {
	struct m6847 *vdp;
	SDL_Renderer *render;
	SDL_Texture *texture;
	SDL_Window *window;
};


void m6847_render(struct m6847_renderer *render)
{
	SDL_Rect sr;

	sr.x = 0;
	sr.y = 0;
	sr.w = 256;
	sr.h = 192;

	SDL_UpdateTexture(render->texture, NULL, m6847_get_raster(render->vdp), 1024);
	SDL_RenderClear(render->render);
	SDL_RenderCopy(render->render, render->texture, NULL, &sr);
	SDL_RenderPresent(render->render);
}

void m6847_renderer_free(struct m6847_renderer *render)
{
	if (render->texture)
		SDL_DestroyTexture(render->texture);
}


struct m6847_renderer *m6847_renderer_create(struct m6847 *vdp)
{
	struct m6847_renderer *render;

	render = malloc(sizeof(struct m6847_renderer));
	if (render == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(render, 0, sizeof(struct m6847_renderer));
	render->vdp = vdp;
	m6847_set_colourmap(vdp, vdp_ctab);
	render->window = SDL_CreateWindow("6847",
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
	SDL_RenderSetLogicalSize(render->render, 256, 192);

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
