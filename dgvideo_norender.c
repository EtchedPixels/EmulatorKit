/*
 *	DGVideo driver raster null output
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dgvideo.h"
#include "dgvideo_render.h"

struct dgvideo_renderer {
	struct dgvideo *dg;
};


void dgvideo_render(struct dgvideo_renderer *render)
{
}

void dgvideo_renderer_free(struct dgvideo_renderer *render)
{
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
	return render;
}
