/*
 *	TMS9918A emulation.
 *
 *	This could benefit from some optimization especially on the sprite side
 *	of things. There are also a lot of other optimizations that could be
 *	done to only recompute changed scan lines, although that need some
 *	trickery with sprite compositing.
 *
 *	We maintain a frame buffer and register as the real hardware sees them.
 *	Our code then rasterizes the framebuffer each frame. We don't do any
 *	clever tricks for unchanged lines/areas or when combining sprites.
 *	Our output is a 256 pixel x 192 pixel 32bit image that we then feed
 *	to SDL2 to scale and GPU render.
 *
 *	The renderer and the emulation are intentionally isolated. The
 *	renderer provides the colour mapping table, and displays the resulting
 *	rasterbuffer. This code is completely output independent.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tms9918a.h"

struct tms9918a {
	uint8_t reg[8];	/* We just ignore invalid bits, you can't read them
			   back so who cares */
	uint8_t status;
	uint8_t framebuffer[16384];	/* The memory behind the VDP */
	uint32_t rasterbuffer[256 * 192]; /* Our output texture */
	uint32_t *colourmap;
	uint8_t colbuf[256 + 64];	/* For off edge collisions */
	unsigned int latch;		/* The toggling latch for low/hi */
	unsigned int read;		/* Mode */
	uint16_t addr;			/* Address */
	uint16_t memmask;		/* Address range */

	int trace;
};

/*
 *	Sprites
 */

/*
 *	Draw a horizontal slice of a sprite into the render buffer. If it
 *	needs magnifying then do the magnification as we render. We use a
 *	simple line buffer to detect collisions
 */
static void tms9918a_render_slice(struct tms9918a *vdp, int y, uint8_t *sprat, uint16_t bits, unsigned int width)
{
	int x = sprat[1];
	uint32_t *pixptr = vdp->rasterbuffer + 256 * y;
	uint8_t *colptr = vdp->colbuf + 32;
	uint32_t foreground = vdp->colourmap[sprat[3] & 0x0F];
	int mag = vdp->reg[1] & 0x01;
	int step = 1;
	int i;

	if (sprat[3] & 0x80)
		x -= 32;
	pixptr += x;
	colptr += x;

	/* Walk across the sprite doing rendering and collisions. Collisions apply
	   to offscreen objects. Colbuf is sized to cover this */
	for (i = x; i < x + width; i++) {
		if (i >= 0 && i <= 255) {
			if (bits & 0x8000U)
				*pixptr++ = foreground;
			else
				pixptr++;
		}
		/* This pixel was already sprite written - collision */
		if (*colptr) {
			vdp->status |= 0x20;
			*colptr++ = 1;
		} else {
			colptr++;
		}
		/* For magnified sprites write each pixel twice */
		step ^= mag;
		if (step == 1)
			bits <<= 1;
	}
}

/*
 *	Calculate the slice of a sprite to render and feed it to the actual
 *	bit renderer.
 */
static void tms9918a_render_sprite(struct tms9918a *vdp, int y, uint8_t *sprat, uint8_t *spdat)
{
	int row = *sprat;
	uint16_t bits;
	unsigned int width = 8;

	/* Figure out the right data row */
	if (row >= 0xE1)
		row -= 0x100;	/* Signed top border */
	row = y - row;
	/* Get the data and expand it if needed */
	if ((vdp->reg[1] & 0x02) == 0) {
		spdat += row;
		width = 8;
		bits = *spdat << 24;
	} else {
		spdat += row;
		bits = *spdat << 8;
		bits |= spdat[16];
		width = 16;
	}
	tms9918a_render_slice(vdp, y, sprat, bits, width);
}

/*
 *	Composite the sprites for a given scan line
 */
static void tms9918a_sprite_line(struct tms9918a *vdp, int y)
{
	uint8_t *sphead[4];
	uint8_t **spqueue = sphead;
	uint8_t *sprat = vdp->framebuffer + ((vdp->reg[5] & 0x7F) << 7);
	uint8_t *spdat = vdp->framebuffer + ((vdp->reg[6] & 0x07) << 11);
	int i;
	unsigned int spheight = vdp->reg[1]& 0x02 ? 16 : 8;
	unsigned int mag = vdp->reg[1] & 0x01;
	unsigned int ns = 0;
	unsigned int spshft = 3;

	if (mag)
		spheight <<= 1;

	/* Clear the collision buffer for the line */
	memset(vdp->colbuf, 0, sizeof(vdp->colbuf));

	/* Walk the sprite table and queue any sprite on this line */
	for(i = 0; i < 32; i++) {
		if (*sprat == 0xD0)
			break;
		if (*sprat <= y && *sprat + spheight > y) {
			ns++;
			/* Too many sprites: only 4 get handled */
			/* Q: do the full 32 get collision detected ? */
			if (ns > 4) {
				vdp->status |= 0x40 | i;	/* Too many sprites */
				break;
			}
			*spqueue++ = sprat;
		}
		sprat += 4;
	}
	/* We need to render the ones we got in reverse order to get right
	   pixel priority */
	while(spqueue > sphead) {
		sprat = *--spqueue;
		tms9918a_render_sprite(vdp, y, sprat, spdat + (sprat[2] << spshft));
	}
}

/*
 *	Add sprites to the raster image
 *
 *	BUG?: Do we need to do a pure collision sweep for the lines above
 *	and below the picture ?
 */
static void tms9918a_raster_sprites(struct tms9918a *vdp)
{
	unsigned int i;
	for (i = 0; i < 192; i++)
		tms9918a_sprite_line(vdp, i);
}

/*
 *	G1 - colour data from a character tied colour map
 */
static void tms9918a_raster_pattern_g1(struct tms9918a *vdp, uint8_t code, uint8_t *pattern, uint8_t *colour, uint32_t *out)
{
	unsigned int x,y;
	uint32_t foreground, background;
	uint8_t bits;

	pattern += code << 3;
	colour += code >> 3;
	foreground = vdp->colourmap[*colour >> 4];
	background = vdp->colourmap[*colour & 0x0F];

	for (y = 0; y < 8; y++) {
		bits = *pattern++;
		for (x = 0; x < 8; x++) {
			if (bits & 0x80)
				*out++ = foreground;
			else
				*out++ = background;
			bits <<= 1;
		}
		out += 248;
	}
}

/*
 *	768 characters, 256 byte pattern table, colur table holds fg/bg
 *	colour for each group of 8 symbols
 */
static void tms9918a_rasterize_g1(struct tms9918a *vdp)
{
	unsigned int x,y;
	uint8_t *p = vdp->framebuffer + ((vdp->reg[2] & 0x0F) << 10);
	uint8_t *pattern = vdp->framebuffer + ((vdp->reg[4] & 0x07) << 11);
	uint8_t *colour = vdp->framebuffer + (vdp->reg[3] << 6);
	uint32_t *fp = vdp->rasterbuffer;

	for (y = 0; y < 8; y++) {
		for (x = 0; x < 32; x++) {
			tms9918a_raster_pattern_g1(vdp, *p++, pattern, colour, fp);
			fp += 8;
		}
		fp += 7 * 256;
	}

	for (; y < 16; y++) {
		for (x = 0; x < 32; x++) {
			tms9918a_raster_pattern_g1(vdp, *p++, pattern, colour, fp);
			fp += 8;
		}
		fp += 7 * 256;
	}

	for (; y < 24; y++) {
		for (x = 0; x < 32; x++) {
			tms9918a_raster_pattern_g1(vdp, *p++, pattern, colour, fp);
			fp += 8;
		}
		fp += 7 * 256;
	}
	tms9918a_raster_sprites(vdp);
}

/*
 *	In G2 mode we have two colours per row of the pattern
 */
static void tms9918a_raster_pattern_g2(struct tms9918a *vdp, uint8_t code, uint8_t *pattern, uint8_t *colour, uint32_t *out)
{
	unsigned int x,y;
	uint32_t foreground, background;
	uint8_t bits;

	pattern += code << 3;
	colour += code << 3;

	for (y = 0; y < 8; y++) {
		bits = *pattern++;
		foreground = vdp->colourmap[*colour >> 4];
		background = vdp->colourmap[*colour++ & 0x0F];
		for (x = 0; x < 8; x++) {
			if (bits & 0x80)
				*out++ = foreground;
			else
				*out++ = background;
			bits <<= 1;
		}
		out += 248;
	}
}

/*
 *	768 characters, 768 patterns, two colours per character row
 *	Patterns and colour must be on 0x2000 boundaries
 */
static void tms9918a_rasterize_g2(struct tms9918a *vdp)
{
	unsigned int x,y;
	uint8_t *p = vdp->framebuffer + ((vdp->reg[2] & 0x0F) << 10);
	uint8_t *pattern = vdp->framebuffer + ((vdp->reg[4] & 0x04) << 11);
	uint8_t *colour = vdp->framebuffer + ((vdp->reg[3] & 0x80) << 6);
	uint32_t *fp = vdp->rasterbuffer;

	uint8_t *pattern0 = pattern;
	uint8_t *colour0 = colour;

	for (y = 0; y < 8; y++) {
		for (x = 0; x < 32; x++) {
			tms9918a_raster_pattern_g2(vdp, *p++, pattern, colour, fp);
			fp += 8;
		}
		fp += 7 * 256;
	}

	if (vdp->reg[4] & 0x01)
		pattern += 0x0800;
	if (vdp->reg[3] & 0x20)
		colour += 0x0800;

	for (; y < 16; y++) {
		for (x = 0; x < 32; x++) {
			tms9918a_raster_pattern_g2(vdp, *p++, pattern, colour, fp);
			fp += 8;
		}
		fp += 7 * 256;
	}

	/* Oddly these don't appear to be incremental but each chunk is relative
	   to base. I guess it makes more sense in logic to mask in the bits */
	if (vdp->reg[4] & 0x02)
		pattern = pattern0 + 0x1000;
	if (vdp->reg[3] & 0x40)
		colour = colour0 + 0x1000;

	for (; y < 24; y++) {
		for (x = 0; x < 32; x++) {
			tms9918a_raster_pattern_g2(vdp, *p++, pattern, colour, fp);
			fp += 8;
		}
		fp += 7 * 256;
	}
	tms9918a_raster_sprites(vdp);
}

/* Rasterize a 4 x 4 pixel block */
static void quad(uint32_t *p, uint32_t code)
{
	*p++ = code;
	*p++ = code;
	*p++ = code;
	*p = code;
	p += 253;
	*p++ = code;
	*p++ = code;
	*p++ = code;
	*p = code;
	p += 253;
	*p++ = code;
	*p++ = code;
	*p++ = code;
	*p = code;
	p += 253;
	*p++ = code;
	*p++ = code;
	*p++ = code;
	*p++ = code;
}

static void tms9918a_raster_multi(struct tms9918a *vdp, uint8_t code, uint8_t *pattern, uint32_t *out)
{
	uint8_t px;
	pattern += code << 3;
	px = *pattern++;
	quad(out, vdp->colourmap[px >> 4]);
	quad(out + 4, vdp->colourmap[px & 15]);
	out += 4 * 256;
	px = *pattern;
	quad(out, vdp->colourmap[px >> 4]);
	quad(out + 4, vdp->colourmap[px & 15]);
}

/* Aka semi-graphics - this mode is almost never used. Each character
   is now a 2 byte pattern describing four squares in 16 colour (15 + bg).
   The row low bits provides the upper 2bits of the pattern code so that
   they are interleaved and all used */
static void tms9918a_rasterize_mc(struct tms9918a *vdp)
{
	unsigned int x,y;
	uint8_t *p = vdp->framebuffer + ((vdp->reg[2] & 0x0F) << 10);
	uint8_t *pattern = vdp->framebuffer + ((vdp->reg[4] & 0x07) << 11);
	uint32_t *fp = vdp->rasterbuffer;

	for (y = 0; y < 24; y++) {
		for (x = 0; x < 32; x++) {
			tms9918a_raster_multi(vdp, *p++, pattern + ((y & 3) << 1), fp);
			fp += 8;
		}
		fp += 7 * 256;
	}
	tms9918a_raster_sprites(vdp);
}

/*
 *	Rasterise a text symbol
 */
static void tms9918a_raster_pattern6(struct tms9918a *vdp, uint8_t code, uint8_t *pattern, uint32_t *out)
{
	unsigned int x,y;
	uint8_t bits;
	uint32_t background = vdp->colourmap[vdp->reg[7] & 0x0F];
	uint32_t foreground = vdp->colourmap[vdp->reg[7] >> 4];

	pattern += code << 3;

	/* 8 rows, left 6 columns (highest bits) used */
	for (y = 0; y < 8; y++) {
		bits = *pattern++;
		for (x = 0; x < 6; x++) {
			if (bits & 0x80)
				*out++ = foreground;
			else
				*out++ = background;
			bits <<= 1;
		}
		out += 250;	/* 256 bytes per row even when working in 240 pixel */
	}
}

/*
 *	960 characters using 6bits of each pattern. No sprites, no colour
 *	tables. 8 pixels of border left and right
 */
static void tms9918a_rasterize_text(struct tms9918a *vdp)
{
	uint8_t *p = vdp->framebuffer + ((vdp->reg[2] & 0x0F) << 10);
	uint8_t *pattern = vdp->framebuffer + ((vdp->reg[4] & 0x07) << 11);
	uint32_t *fp = vdp->rasterbuffer;
	unsigned int x, y;
	uint32_t background = vdp->colourmap[vdp->reg[7] & 0x0F];

	/* Everything really happens in screen thirds but for this mode it
	   does not actually matter */
	for (y = 0; y < 24; y++) {
		/* Weird 6bit wide mode */
		for (x = 0; x < 8; x++) {
			fp[256] = background;
			fp[512] = background;
			fp[768] = background;
			fp[1024] = background;
			fp[1280] = background;
			fp[1536] = background;
			fp[1792] = background;
			*fp++ = background;
		}
		for (x = 0 ; x < 40; x++) {
			tms9918a_raster_pattern6(vdp, *p++, pattern, fp);
			fp += 6;
		}
		for (x = 0; x < 8; x++) {
			fp[256] = background;
			fp[512] = background;
			fp[768] = background;
			fp[1024] = background;
			fp[1280] = background;
			fp[1536] = background;
			fp[1792] = background;
			*fp++ = background;
		}
		/* Our rows are 256 pixels but for text we use the middle 240 */
		fp += 7 * 256;
	}
	/* No sprites in text mode */
}

/*
 *	Rasterize the frame buffer for the current settings. Generates a
 *	32bit frame buffer image in 256x192 pixels ready for SDL2 or similar
 *	to scale and render onto the actual framebuffer. Call this every
 *	vblank frame.
 */
void tms9918a_rasterize(struct tms9918a *vdp)
{
	unsigned int mode = (vdp->reg[1] >> 2) & 0x06;
	mode |= (vdp->reg[0] & 0x02) >> 1;

	if ((vdp->reg[1] & 0x40) == 0)
		memset(vdp->rasterbuffer, 0, sizeof(vdp->rasterbuffer));
	else {
		switch(mode) {
		case 0:
			tms9918a_rasterize_g1(vdp);
			break;
		case 1:
			tms9918a_rasterize_g2(vdp);
			break;
		case 2:
			tms9918a_rasterize_mc(vdp);
			break;
		case 4:
			tms9918a_rasterize_text(vdp);
			break;
		default:
			/* There are things that happen for the invalid cases but address
			   them later maybe */
			memset(vdp->rasterbuffer, 0, sizeof(vdp->rasterbuffer));
		}
	}
	if (vdp->trace)
		fprintf(stderr, "vdp: frame done.\n");
	vdp->status |= 0x80;
}

static uint8_t tms9918a_status(struct tms9918a *vdp)
{
	uint8_t r = vdp->status;
	vdp->status = 0;
	return r;
}

/*
 *	This is the whole VDP memory interface. It's a thing of beauty
 *	Everything else is rendering time (on the real one even the loading
 *	of the real readback data and slotting in the writes).
 */

void tms9918a_write(struct tms9918a *vdp, uint8_t addr, uint8_t val)
{
	switch(addr & 1) {
	case 0:
		if (vdp->trace)
			fprintf(stderr, "vdp: write fb %04x<-%02X\n", vdp->addr, val);
		vdp->framebuffer[vdp->addr] = val;
		vdp->addr++;
		vdp->addr &= vdp->memmask;
		/* A data write clears the latch, this means you can write the low
		   byte of the address and data repeatedly and usefully */
		vdp->latch = 0;
		break;
	case 1:
		if (vdp->latch == 0) {
			/* The set up affects the low address bits immediately. it does
			   not seem to change the direction */
			vdp->addr &= 0xFF00;
			vdp->addr |= val;
			vdp->latch = 1;
			return;
		}
		vdp->latch = 0;
		switch(val & 0xC0) {
			/* Memory read set up */
			case 0x00:
				vdp->addr &= 0xFF;
				vdp->addr |= val << 8;
				vdp->read = 1;
				vdp->addr &= vdp->memmask;
				/* Strictly speaking this makes a request, the result turns
				   up in a bit. We might want to model a downcounter to trap
				   errors in this */
				if (vdp->trace)
					fprintf(stderr, "vdp: set up to read %04x\n", vdp->addr);
				break;
			/* Memory write set up */
			case 0x40:
				vdp->addr &= 0xFF;
				vdp->addr |= val << 8;
				vdp->addr &= vdp->memmask;
				vdp->read = 0;
				if (vdp->trace)
					fprintf(stderr, "vdp: set up to write %04x\n", vdp->addr);
				break;
			/* Write to a register. Not clear if the low part of the address
			   and latched data are one but they seem to be */
			case 0x80:
				vdp->reg[val & 7] = vdp->addr & 0xFF;
				if (vdp->trace)
					fprintf(stderr, "vdp: write reg %02X <- %02x\n", val, vdp->addr & 0xFF);
				break;
			/* Unused on the VDP1 */
			case 0xC0:
				break;
		}
	}
}

uint8_t tms9918a_read(struct tms9918a *vdp, uint8_t addr)
{
	uint8_t r;
	switch(addr & 1) {
	case 0:
		r = vdp->framebuffer[vdp->addr++ & vdp->memmask];
		if (vdp->trace)
			fprintf(stderr, "vdp: read data %02x\n", r);
		break;
	case 1:
		r = tms9918a_status(vdp);
		/* Nasty if we have data latched it just went poof! */
		if (vdp->latch && vdp->trace)
			fprintf(stderr, "vdp: status read cleared latch.\n");
		if (vdp->trace)
			fprintf(stderr, "vdp: read status %02x\n", r);
		break;
	}
	vdp->latch = 0;
	return r;
}

void tms9918a_reset(struct tms9918a *vdp)
{
	vdp->reg[0] = 0;
	vdp->reg[1] = 0;
	vdp->latch = 0;
	vdp->read = 0;
	vdp->memmask = 0x3FFF;	/* 16K */
}

struct tms9918a *tms9918a_create(void)
{
	struct tms9918a *vdp = malloc(sizeof(struct tms9918a));
	if (vdp == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	tms9918a_reset(vdp);
	return vdp;
}

void tms9918a_trace(struct tms9918a *vdp, int onoff)
{
	vdp->trace = onoff;
}

int tms9918a_irq_pending(struct tms9918a *vdp)
{
	if (vdp->reg[1] & 0x20)
		return vdp->status & 0x80;
	return 0;
}

uint32_t *tms9918a_get_raster(struct tms9918a *vdp)
{
	return vdp->rasterbuffer;
}

void tms9918a_set_colourmap(struct tms9918a *vdp, uint32_t *ctab)
{
	vdp->colourmap = ctab;
}

uint32_t tms9918a_get_background(struct tms9918a *vdp)
{
	return vdp->colourmap[vdp->reg[7] & 0xF];
}
