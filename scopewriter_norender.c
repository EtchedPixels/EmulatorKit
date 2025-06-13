/*
 *	Scopewriter null output
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "scopewriter.h"
#include "scopewriter_render.h"

struct scopewriter_renderer {
	struct scopewriter *sw;
};


void scopewriter_render(struct scopewriter_renderer *render)
{
}

void scopewriter_renderer_free(struct scopewriter_renderer *render)
{
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
	return render;
}
