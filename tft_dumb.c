/*
 *	Emulate variants of the bazillion co-ordinate addressed TFT
 *	devices
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tft_dumb.h"

static void expand_rgb(struct tft_dumb *tft)
{
	uint8_t r = tft->data >> 11;
	uint8_t g = tft->data >> 5 & 0x3F;
	uint8_t b = tft->data & 0x1F;

	/* Turn into 8bit R G B */
	r <<= 3;
	g <<= 2;
	b <<= 3;

	tft->data = (r << 16) | (g << 8) | b;
}

static void tft_data(struct tft_dumb *tft, uint8_t data)
{
	tft->data <<= 8;
	tft->data |= data;
	if (tft->step == tft->bytesperpixel) {
		if (tft->bytesperpixel == 2)
			expand_rgb(tft);
		tft->data |= 0xFF000000;
		if (tft->ypos < tft->height && tft->xpos < tft->width)
			tft->rasterbuffer[tft->ypos * tft->width + tft->xpos] = tft->data;
		tft->xpos++;
		if (tft->xpos == tft->right) {
			tft->xpos = tft->left;
			tft->ypos++;
			if (tft->ypos == tft->bottom)
				tft->ypos = tft->top;
		}
		tft->data = 0;
		tft->step = 0;
	}
}

void tft_write(struct tft_dumb *tft, unsigned cd, uint8_t data)
{
	if (cd) {
		tft->step = 0;
		tft->cmd = data;
		return;
	}
	tft->step++;
	if (tft->cmd == tft->row_port) {
		switch(tft->step++) {
		case 0:
			tft->top = data << 8;
			break;
		case 1:
			tft->top |= data;
			break;
		case 2:
			tft->bottom = data << 8;
			break;
		case 3:
			tft->bottom |= data;
			break;
		}
		return;
	}
	if (tft->cmd == tft->col_port) {
		switch(tft->step++) {
		case 0:
			tft->left = data << 8;
			break;
		case 1:
			tft->left |= data;
			break;
		case 2:
			tft->right = data << 8;
			break;
		case 3:
			tft->right |= data;
			break;
		}
		return;
	}
	if (tft->cmd == tft->start_port) {
		tft->xpos = tft->left;
		tft->ypos = tft->top;
		tft_data(tft, data);
		tft->cmd = tft->data_port;
		return;
	}
	if (tft->cmd == tft->data_port) {
		tft_data(tft, data);
		return;
	}
}

void tft_rasterize(struct tft_dumb *tft)
{
	/* Nothing to do */
}

struct tft_dumb *tft_create(unsigned type)
{
	struct tft_dumb *tft = malloc(sizeof(struct tft_dumb));
	if (tft == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(tft, 0, sizeof(struct tft_dumb));
	/* Armwavingly emulate a tiny subset of the ILI9341 to start with */
	tft->height = 240;
	tft->width = 320;
	tft->bytesperpixel = 2;
	tft->row_port = 0x2A;
	tft->col_port = 0x2B;
	tft->start_port = 0x2C;
	tft->data_port = 0x3C;

	tft->rasterbuffer = calloc(tft->height * sizeof(uint32_t), tft->width);
	if (tft->rasterbuffer == NULL) {
		fprintf(stderr, "Out of memory for raster buffer.\n");
		exit(1);
	}
	memset(tft->rasterbuffer, 0xFF, tft->height * tft->width * sizeof(uint32_t));
	return tft;
}

void tft_free(struct tft_dumb *tft)
{
	free(tft->rasterbuffer);
	free(tft);
}

void tft_trace(struct tft_dumb *tft, unsigned onoff)
{
	tft->trace = onoff;
}

void tft_reset(struct tft_dumb *tft)
{
	/* TODO */
}
