/*
 *	TFT driver raster null output
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tft_dumb.h"
#include "tft_dumb_render.h"

struct tft_renderer {
	struct tft_dumb *tft;
};

void tft_render(struct tft_renderer *render)
{
}

void tft_renderer_free(struct tft_renderer *render)
{
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
	return render;
}
