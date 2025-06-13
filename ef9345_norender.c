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

#include "ef9345.h"
#include "ef9345_render.h"

struct ef9345_renderer {
	unsigned dummy;
};

struct ef9345_renderer dummy;

void ef9345_render(struct ef9345_renderer *render)
{
}

void ef8345_renderer_free(struct ef9345_renderer *render)
{
}


struct ef9345_renderer *ef9345_renderer_create(struct ef9345 *ef9345)
{
	return &dummy;
}
