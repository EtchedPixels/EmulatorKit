/*
 *	Fairly primitve 6847 emulator.
 *
 *	We don't emulate any of the weird mixed modes or timing hacks, nor
 *	the 6847T1 (which only works with the 6883/5 SAM) and has a different
 *	model for video fetching.
 *
 *	TODO: add a border and border colours
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "6847.h"
#include "6847font.h"

struct m6847 {
	int trace;
	uint32_t background;
	uint32_t foreground;
	uint32_t *colourmap;
	uint32_t rasterbuffer[256 * 192];
};

/* Pixels per rendered pixel */
static uint8_t xpandtab[8] = {
	4, 2, 1, 2, 1, 2, 1, 1
};

/* Lines per rendered line */
static uint8_t ypandtab[8] = {
	3, 3, 3, 2, 2, 1, 1, 1
};

#define m6847_mode(x)		(config & (M6847_GM0|M6847_GM1|M6847_GM2))

/* One bit per pixel magnified according to mode */
static void m6847_rg_raster(struct m6847 *vdg, uint8_t config)
{
	uint32_t *p = vdg->rasterbuffer;
	uint16_t base = 0, oldbase;
//	unsigned int rg = config & M6847_GM0;
	unsigned int xpand = xpandtab[m6847_mode(config) & 0x07];
	unsigned int ypand = ypandtab[m6847_mode(config) & 0x07];
	unsigned int i, j, x, y = 0;

	while(y < 192) {
		oldbase = base;
		x = 0;
		while(x < 256) {
			uint8_t data = m6847_video_read(vdg, base++, NULL);
			for (i = 0; i < 8; i++) {
				for (j = 0; j < xpand; j++) {
					if (data & 0x80)
						*p++ = vdg->foreground;
					else
						*p++ = vdg->background;
					x++;
				}
				data <<= 1;
			}
		}
		y++;
		if (y % ypand)
			base = oldbase;
	}
}

/* Two bits per pixel: CG is identical to RG except that instead of each
   input pixel producing one output pixel in fg/bg it produces two output
   pixels both in one of 4 colours */

static void m6847_cg_raster(struct m6847 *vdg, uint8_t config)
{
	uint32_t *p = vdg->rasterbuffer;
	uint16_t base = 0, oldbase;
//	unsigned int rg = config & M6847_GM0;
	unsigned int xpand = xpandtab[m6847_mode(config) & 0x07];
	unsigned int ypand = ypandtab[m6847_mode(config) & 0x07];
	unsigned int i, j, x, y = 0;
	uint32_t colour;

	while(y < 192) {
		oldbase = base;
		x = 0;
		while(x < 256) {
			uint8_t data = m6847_video_read(vdg, base++, NULL);
			for (i = 0; i <= 3; i++) {
				for (j = 0; j < xpand; j++) {
					if (config & M6847_CSS)
						colour = vdg->colourmap[((data & 0xC0) >> 6) + 4];
					else
						colour = vdg->colourmap[(data & 0xC0) >> 6];
					*p++ = colour;
					*p++ = colour;
					x+= 2;
				}
				data <<= 2;
			}
		}
		y++;
		if (y % ypand)
			base = oldbase;
	}
}

static uint8_t sgmap[4] = {
	0x00,
	0x0F,
	0xF0,
	0xFF
};

static uint8_t m6847_semigraphics4(uint8_t sym, unsigned int slice)
{
	if (slice < 6)
		sym >>= 2;
	return sgmap[sym & 3];
}

static uint8_t m6847_semigraphics6(uint8_t sym, unsigned int slice)
{
	if (slice < 4)
		sym >>= 4;
	else if (slice < 8)
		sym >>= 2;
	return sgmap[sym & 3];
}

/*
 *	Written this way so that if we ever need to we can switch to
 *	progressive scan line rendering.
 */
static void m6847_text_raster(struct m6847 *vdg, uint8_t config)
{
	uint32_t *p = vdg->rasterbuffer;
	uint16_t base = 0;
	uint32_t textfg = vdg->foreground;
	uint32_t background = vdg->background;
	uint32_t foreground;
	unsigned int y, x, i;

	for (y = 0; y < 192; y++) {
		unsigned int row = y % 12;
		for (x = 0; x < 32; x++) {
			uint8_t sym = m6847_video_read(vdg, base++, &config);
			uint8_t data;
			if (config & M6847_AS) {
				if (config & M6847_INTEXT) {
					foreground = vdg->colourmap[sym >> 6];
					data = m6847_semigraphics6(sym, row);
				} else {
					if (config & M6847_CSS)
						foreground = vdg->colourmap[4 + ((sym >> 4) & 0x07)];
					else
						foreground = vdg->colourmap[(sym >> 4) & 0x07];
					data = m6847_semigraphics4(sym, row);
				}
			} else {
				foreground = textfg;
				if (config & M6847_INTEXT)
					data = m6847_font_rom(vdg, sym, row);
				else
					data = font[sym & 0x3F][row];
				if (config & M6847_INV)
					data ^= 0xFF;
			}
			for (i = 0; i < 8; i++) {
				if (data & 0x80)
					*p++ = foreground;
				else
					*p++ = background;
				data <<= 1;
			}
		}
		/* Scan each row 12 times */
		if (row != 11)
			base -= 32;
	}
}

/*
 *	Precompute foreground and background colour. Arguably we
 *	should do this through the scan as the video changes, but
 *	that would imply doing clock perfect emulation of something which
 *	isn't our goal at this time.
 */
static void m6847_calc_colours(struct m6847 *vdg, uint8_t config)
{
	if (config & M6847_CSS) {
		vdg->foreground = vdg->colourmap[1];	/* Orange */
		vdg->foreground = vdg->colourmap[7];	/* Dark orange */
	} else {
		vdg->foreground = vdg->colourmap[0];	/* Green */
		vdg->background = vdg->colourmap[8];	/* Black */
	}
#if 0
	/* FIXME: text we do bit level inverts to handle the fact some systems
	   toggle the INV bit as they render. Graphics needs thought */
	if (config & M6847_INV) {
		uint32_t c = vdg->background;
		vdg->background = vdg->foreground;
		vdg->foreground = c;
	}
#endif
}

void m6847_rasterize(struct m6847 *vdg)
{
	uint8_t config = m6847_get_config(vdg);

	m6847_calc_colours(vdg, config);
	if (config & M6847_AG) {
		if (config & M6847_GM0)
			m6847_rg_raster(vdg, config);
		else
			m6847_cg_raster(vdg, config);
	} else
		m6847_text_raster(vdg, config);
}

/* Mash the 8 pixel set that roughly correspond to this fetch. This is not
   based on any exact science just getting the right "feel" */
void m6847_sparkle(struct m6847 *vdg, unsigned line, unsigned point)
{
	unsigned imax = 8;
	unsigned i;
	/* In the blanking zone */
	/* TODO: PAL - PAL has extra blanking lines */
	if (line < 70)
		return;
	/* 262 lines of which 192 are video */
	line -= 70;
	/* In hsync space */
	if (point < 71 || point > 199)
		return;
	point -= 71;
	/* The remaining 128 tstates are the 256 pixels */
	point <<= 1;
	/* point is now in pixels */
	if (point & 4)
		imax = 16;
	point &= 0xF8;	/* Byte align */
	for (i = 0; i < imax; i++)
		vdg->rasterbuffer[256 * line + point + i] = 0xFF000000;
}

struct m6847 *m6847_create(unsigned int type)
{
	struct m6847 *vdg = malloc(sizeof(struct m6847));
	if (vdg == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(vdg, 0, sizeof(struct m6847));
	return vdg;
}

void m6847_free(struct m6847 *vdg)
{
	free(vdg);
}

void m6847_trace(struct m6847 *vdg, int onoff)
{
	vdg->trace = onoff;
}

void m6847_set_colourmap(struct m6847 *vdg, uint32_t *colourmap)
{
	vdg->colourmap = colourmap;
}

uint32_t *m6847_get_raster(struct m6847 *vdg)
{
	return vdg->rasterbuffer;
}

void m6847_reset(struct m6847 *vdg)
{
}
