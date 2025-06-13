/*
 *	6847 driver raster null output
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "6847.h"
#include "6847_render.h"

struct m6847_renderer {
	struct m6847 *vdp;
};


void m6847_render(struct m6847_renderer *render)
{
}

void m6847_renderer_free(struct m6847_renderer *render)
{
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
	return render;
}
