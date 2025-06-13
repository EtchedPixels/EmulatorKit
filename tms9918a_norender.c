/*
 *	TMS9918A driver raster null output
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tms9918a.h"
#include "tms9918a_render.h"

static uint32_t vdp_ctab[16] = {
	0x000000FF,	/* transparent (we render as black) */
	0x000000FF,	/* black */
	0x20C020FF,	/* green */
	0x60D060FF,	/* light green */

	0x2020D0FF,	/* blue */
	0x4060D0FF,	/* light blue */
	0xA02020FF,	/* dark red */
	0x40C0D0FF,	/* cyan */

	0xD02020FF,	/* red */
	0xD06060FF,	/* light red */
	0xC0C020FF,	/* dark yellow */
	0xC0C080FF,	/* yellow */

	0x208020FF,	/* dark green */
	0xC040A0FF,	/* magneta */
	0xA0A0A0FF,	/* grey */
	0xD0D0D0FF	/* white */
};

struct tms9918a_renderer {
	struct tms9918a *vdp;
};


void tms9918a_render(struct tms9918a_renderer *render)
{
}

void tms9918a_renderer_free(struct tms9918a_renderer *render)
{
	free(render);
}


struct tms9918a_renderer *tms9918a_renderer_create(struct tms9918a *vdp)
{
	struct tms9918a_renderer *render;

	render = malloc(sizeof(struct tms9918a_renderer));
	if (render == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(render, 0, sizeof(struct tms9918a_renderer));
	render->vdp = vdp;
	tms9918a_set_colourmap(vdp, vdp_ctab);
	return render;
}
