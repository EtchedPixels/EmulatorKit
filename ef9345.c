// license:GPL-2.0+
// copyright-holders:Daniel Coulom,Sandro Ronco
/*********************************************************************

    ef9345.c

    Thomson EF9345 video controller emulator code

    This code is based on Daniel Coulom's implementation in DCVG5k
    and DCAlice released by Daniel Coulom under GPL license

    TS9347 variant support added by Jean-François DEL NERO

    Turned into C, hacked about a bit and de-mamed - Alan Cox

*********************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ef9345.h"

#define MODE24x40   0
#define MODEVAR40   1
#define MODE8x80    2
#define MODE12x80   3
#define MODE16x40   4

static void set_video_mode(struct ef9345 *ef);

//**************************************************************************
//  HELPERS
//**************************************************************************

// calculate the internal RAM offset
//
//	Reworked from the original as the Mame one didn't handle
//	the upper bits
//
//	R6 is encoded as	D1 D'1 D0 YYYYY
//	R4 is decoded as	- - D'0 YYYYY
//
//
static uint16_t indexram(struct ef9345 *ef, uint8_t r)
{
	uint8_t x = ef->reg[r];
	uint8_t y = ef->reg[r - 1];
	uint16_t off;
	if (y < 8)
		y &= 1;
	/* Y 7:6 should be Z3:Z2 bank ? */
	off = (x&0x3f) | ((x & 0x40) << 6) | ((x & 0x80) << 4);
	off |= ((y & 0x1f) << 6) | ((y & 0x20) << 8);
	if (r == 7)
		off += (y & 0x80) << 6;
	else
		off += (ef->reg[6] & 0x40) << 7;
	return off;
}

// calculate the internal ROM offset
static uint16_t indexrom(struct ef9345 *ef, uint8_t r)
{
	uint8_t x = ef->reg[r];
	uint8_t y = ef->reg[r - 1];
	if (y < 8)
		y &= 1;
	return((x&0x3f)|((x&0x40)<<6)|((x&0x80)<<4)|((y&0x1f)<<6));
}

// increment x
static void inc_x(struct ef9345 *ef, uint8_t r)
{
	uint8_t i = (ef->reg[r] & 0x3f) + 1;
	if (i > 39)
	{
		i -= 40;
		ef->m_state |= 0x40;
	}
	ef->reg[r] = (ef->reg[r] & 0xc0) | i;
}

// increment y
static void inc_y(struct ef9345 *ef, uint8_t r)
{
	uint8_t i = (ef->reg[r] & 0x1f) + 1;
	if (i > 31)
		i -= 24;
	ef->reg[r] = (ef->reg[r] & 0xe0) | i;
}

void vram_writeb(struct ef9345 *ef, uint16_t addr, uint8_t val)
{
	ef->m_videoram[addr & ef->vram_mask] = val;
}

uint8_t vram_readb(struct ef9345 *ef, uint16_t addr)
{
	return ef->m_videoram[addr & ef->vram_mask];
}


//**************************************************************************
//  live device
//**************************************************************************

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

// initialize the ef9345 accented chars
static void init_accented_chars(struct ef9345 *ef)
{
	uint16_t i, j;
	for(j = 0; j < 0x10; j++)
		for(i = 0; i < 0x200; i++)
			ef->m_acc_char[(j << 9) + i] = ef->m_charset[0x0600 + i];

	for(j = 0; j < 0x200; j += 0x40)
		for(i = 0; i < 4; i++)
		{
			ef->m_acc_char[0x0200 + j + i +  4] |= 0x1c; //tilde
			ef->m_acc_char[0x0400 + j + i +  4] |= 0x10; //acute
			ef->m_acc_char[0x0400 + j + i +  8] |= 0x08; //acute
			ef->m_acc_char[0x0600 + j + i +  4] |= 0x04; //grave
			ef->m_acc_char[0x0600 + j + i +  8] |= 0x08; //grave

			ef->m_acc_char[0x0a00 + j + i +  4] |= 0x1c; //tilde
			ef->m_acc_char[0x0c00 + j + i +  4] |= 0x10; //acute
			ef->m_acc_char[0x0c00 + j + i +  8] |= 0x08; //acute
			ef->m_acc_char[0x0e00 + j + i +  4] |= 0x04; //grave
			ef->m_acc_char[0x0e00 + j + i +  8] |= 0x08; //grave

			ef->m_acc_char[0x1200 + j + i +  4] |= 0x08; //point
			ef->m_acc_char[0x1400 + j + i +  4] |= 0x14; //trema
			ef->m_acc_char[0x1600 + j + i + 32] |= 0x08; //cedilla
			ef->m_acc_char[0x1600 + j + i + 36] |= 0x04; //cedilla

			ef->m_acc_char[0x1a00 + j + i +  4] |= 0x08; //point
			ef->m_acc_char[0x1c00 + j + i +  4] |= 0x14; //trema
			ef->m_acc_char[0x1e00 + j + i + 32] |= 0x08; //cedilla
			ef->m_acc_char[0x1e00 + j + i + 36] |= 0x04; //cedilla
		}
}

void ef9345_init(struct ef9345 *ef)
{
//	m_busy_timer = timer_alloc(FUNC(ef9345_device::clear_busy_flag), this);
//	m_blink_timer = timer_alloc(FUNC(ef9345_device::blink_tick), this);
//	m_blink_timer->adjust(attotime::from_msec(500), 0, attotime::from_msec(500));

	init_accented_chars(ef);
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------
void ef9345_reset(struct ef9345 *ef)
{
	ef->m_tgs = ef->m_mat = ef->m_pat =ef->m_dor = ef->m_ror = 0;
	ef->m_state = 0;
	ef->m_bf = 0;
	ef->m_block = 0;
	ef->m_blink = 0;
	ef->m_latchc0 = 0;
	ef->m_latchm = 0;
	ef->m_latchi = 0;
	ef->m_latchu = 0;
	ef->m_char_mode = MODE24x40;

	memset(ef->m_last_dial, 0, sizeof(ef->m_last_dial));
	memset(ef->reg, 0, sizeof(ef->reg));
	memset(ef->m_border, 0, sizeof(ef->m_border));
	memset(ef->m_border, 0, sizeof(ef->m_ram_base));

//	m_screen_out.fill(ef, 0);

	set_video_mode(ef);
}

// set busy flag and timer to clear it
static void set_busy_flag(struct ef9345 *ef, int period)
{
//FIXME	ef->m_bf = 1;
	ef->busy_ticks = period;	/* in ns */
}

// draw a char in 40 char line mode
static void draw_char_40(struct ef9345 *ef, uint8_t *c, uint16_t x, uint16_t y)
{
	const uint32_t *palette = ef->m_palette;
	const int scan_xsize = 8;
	const int scan_ysize = 10;

	for(int i = 0; i < scan_ysize; i++)
		for(int j = 0; j < scan_xsize; j++)
			ef->raster[(y * 10 + i)][(x * 8 + j)] = palette[c[8 * i + j] & 0x07];
}

// draw a char in 80 char line mode
static void draw_char_80(struct ef9345 *ef, uint8_t *c, uint16_t x, uint16_t y)
{
	const uint32_t *palette = ef->m_palette;
	const int scan_xsize = 6;
	const int scan_ysize = 10;

	for(int i = 0; i < scan_ysize; i++)
		for(int j = 0; j < scan_xsize; j++)
			ef->raster[(y * 10 + i)][(x * 6 + j)] = palette[c[6 * i + j] & 0x07];
}


// set the ef9345 mode
static void set_video_mode(struct ef9345 *ef)
{
	if (ef->m_variant == TS9347)
	{
		// Only TGS 7 & 6 used for the char mode with the TS9347
		ef->m_char_mode = ((ef->m_tgs & 0xc0) >> 6);
	} else {
		// PAT 7, TGS 7 & 6
		ef->m_char_mode = ((ef->m_pat & 0x80) >> 5) | ((ef->m_tgs & 0xc0) >> 6);
	}

//	uint16_t new_width = (ef->m_char_mode == MODE12x80 || ef->m_char_mode == MODE8x80) ? 492 : 336;
	/* TODO render size switch */

	//border color
	memset(ef->m_border, ef->m_mat & 0x07, sizeof(ef->m_border));

	//set the base for the m_videoram charset
	ef->m_ram_base[0] = ((ef->m_dor & 0x07) << 11);
	ef->m_ram_base[1] = ef->m_ram_base[0];
	ef->m_ram_base[2] = ((ef->m_dor & 0x30) << 8);
	ef->m_ram_base[3] = ef->m_ram_base[2] + 0x0800;

	//address of the current memory block
	// ROR bits are permuted 7/5/6 for Z3 Z1 Z2 forming 8x 2K blocks
	ef->m_block = 0;
	if (ef->m_ror & 0x40)
		ef->m_block += 0x0800;
	if (ef->m_ror & 0x20)
		ef->m_block += 0x1000;
	if (ef->m_ror & 0x40)
		ef->m_block += 0x2000;
//	ef->m_block = 0x0800 * ((((ef->m_ror & 0x80/*WAS F0 */) >> 4) | ((ef->m_ror & 0x40) >> 5) | ((ef->m_ror & 0x20) >> 3)) & 0x0c);
}


// read a char in charset or in m_videoram
static uint8_t read_char(struct ef9345 *ef, uint8_t index, uint16_t addr)
{
	if (index < 0x04)
		return ef->m_charset[0x0800*index + addr];
	else if (index < 0x08)
		return ef->m_acc_char[0x0800*(index&3) + addr];
	else if (index < 0x0c)
		return vram_readb(ef, ef->m_ram_base[index-8] + addr);
	else
		return vram_readb(ef, addr);
}

// calculate the dial position of the char
static uint8_t get_dial(struct ef9345 *ef, uint8_t x, uint8_t attrib)
{
	if (x > 0 && ef->m_last_dial[x-1] == 1)         //top right
		ef->m_last_dial[x] = 2;
	else if (x > 0 && ef->m_last_dial[x-1] == 5)    //half right
		ef->m_last_dial[x] = 10;
	else if (ef->m_last_dial[x] == 1)               //bottom left
		ef->m_last_dial[x] = 4;
	else if (ef->m_last_dial[x] == 2)               //bottom right
		ef->m_last_dial[x] = 8;
	else if (ef->m_last_dial[x] == 3)               //lower half
		ef->m_last_dial[x] = 12;
	else if (attrib == 1)                       //Left half
		ef->m_last_dial[x] = 5;
	else if (attrib == 2)                       //half high
		ef->m_last_dial[x] = 3;
	else if (attrib == 3)                       //top left
		ef->m_last_dial[x] = 1;
	else                                        //none
		ef->m_last_dial[x] = 0;

	return ef->m_last_dial[x];
}

// zoom the char
static void zoom(struct ef9345 *ef, uint8_t *pix, uint16_t n)
{
	uint8_t i, j;
	if ((n & 0x0a) == 0)
		for(i = 0; i < 80; i += 8) // 1, 4, 5
			for(j = 7; j > 0; j--)
				pix[i + j] = pix[i + j / 2];

	if ((n & 0x05) == 0)
		for(i = 0; i < 80; i += 8) // 2, 8, 10
			for(j =0 ; j < 7; j++)
				pix[i + j] = pix[i + 4 + j / 2];

	if ((n & 0x0c) == 0)
		for(i = 0; i < 8; i++) // 1, 2, 3
			for(j = 9; j > 0; j--)
				pix[i + 8 * j] = pix[i + 8 * (j / 2)];

	if ((n & 0x03) == 0)
		for(i = 0; i < 8; i++) // 4, 8, 12
			for(j = 0; j < 9; j++)
				pix[i + 8 * j] = pix[i + 40 + 8 * (j / 2)];
}


// calculate the address of the char x,y
static uint16_t indexblock(struct ef9345 *ef, uint16_t x, uint16_t y)
{
	uint16_t i = x, j;
	j = (y == 0) ? ((ef->m_tgs & 0x20) >> 5) : ((ef->m_ror & 0x1f) + y - 1);
	j = (j > 31) ? (j - 24) : j;

	//right side of a double width character
	if ((ef->m_tgs & 0x80) == 0 && x > 0)
	{
		if (ef->m_last_dial[x - 1] == 1) i--;
		if (ef->m_last_dial[x - 1] == 4) i--;
		if (ef->m_last_dial[x - 1] == 5) i--;
	}

	return 0x40 * j + i;
}

// draw bichrome character (40 columns)
static void bichrome40(struct ef9345 *ef, uint8_t type, uint16_t address, uint8_t dial, uint16_t iblock, uint16_t x, uint16_t y, uint8_t c0, uint8_t c1, uint8_t insert, uint8_t flash, uint8_t conceal, uint8_t negative, uint8_t underline)
{
	uint16_t i;
	uint8_t pix[80];

	if (ef->m_variant == TS9347)
	{
		c0 = 0;
	}

	if (flash && ef->m_pat & 0x40 && ef->m_blink)
		c1 = c0;                    //flash
	if (conceal && (ef->m_pat & 0x08))
		c1 = c0;                    //conceal
	if (negative)                   //negative
	{
		i = c1;
		c1 = c0;
		c0 = i;
	}

	if ((ef->m_pat & 0x30) == 0x30)
		insert = 0;                 //active area mark
	if (insert == 0)
		c1 += 8;                    //foreground color
	if ((ef->m_pat & 0x30) == 0x00)
		insert = 1;                 //insert mode
	if (insert == 0)
		c0 += 8;                    //background color

	//draw the cursor
	i = (ef->reg[6] & 0x1f);
	if (i < 8)
		i &= 1;

	if (iblock == 0x40 * i + (ef->reg[7] & 0x3f))   //cursor position
	{
		switch(ef->m_mat & 0x70)
		{
		case 0x40:                  //00 = fixed complemented
			c0 = (23 - c0) & 15;
			c1 = (23 - c1) & 15;
			break;
		case 0x50:                  //01 = fixed underlined
			underline = 1;
			break;
		case 0x60:                  //10 = flash complemented
			if (ef->m_blink)
			{
				c0 = (23 - c0) & 15;
				c1 = (23 - c1) & 15;
			}
			break;
		case 0x70:                  //11 = flash underlined
			if (ef->m_blink)
				underline = 1;
			break;
		}
	}

	// generate the pixel table
	for(i = 0; i < 40; i+=4)
	{
		uint8_t ch = read_char(ef, type, address + i);

		for (uint8_t b=0; b<8; b++)
			pix[i*2 + b] = (ch & (1<<b)) ? c1 : c0;
	}

	//draw the underline
	if (underline)
		memset(&pix[72], c1, 8);

	if (dial > 0)
		zoom(ef, pix, dial);

	//doubles the height of the char
	if (ef->m_mat & 0x80)
		zoom(ef, pix, (y & 0x01) ? 0x0c : 0x03);

	draw_char_40(ef, pix, x + 1 , y + 1);
}

// draw quadrichrome character (40 columns)
static void quadrichrome40(struct ef9345 *ef, uint8_t c, uint8_t b, uint8_t a, uint16_t x, uint16_t y)
{
	//C0-6= character code
	//B0= insert             not yet implemented !!!
	//B1= low resolution
	//B2= subset index (low resolution only)
	//B3-5 = set number
	//A0-6 = 4 color palette

	uint8_t i, j, n, col[8], pix[80];
	uint8_t lowresolution = (b & 0x02) >> 1, ramx, ramy, ramblock;
	uint16_t ramindex;

	if (ef->m_variant == TS9347)
	{
		// No quadrichrome support into the TS9347
		return;
	}

	//quadrichrome don't suppor double size
	ef->m_last_dial[x] = 0;

	//initialize the color table
	for(j = 1, n = 0, i = 0; i < 8; i++)
	{
		col[i] = 7;

		if (a & j)
			col[n++] = i;

		j <<= 1;
	}

	//find block number in ram
	ramblock = 0;
	if (b & 0x20)   ramblock |= 4;      //B5
	if (b & 0x08)   ramblock |= 2;      //B3
	if (b & 0x10)   ramblock |= 1;      //B4

	//find character address in ram
	ramx = c & 0x03;
	ramy =(c & 0x7f) >> 2;
	ramindex = 0x0800 * ramblock + 0x40 * ramy + ramx;
	if (lowresolution) ramindex += 5 * (b & 0x04);

	//fill pixel table
	for(i = 0, j = 0; i < 10; i++)
	{
		uint8_t ch = read_char(ef, 0x0c, ramindex + 4 * (i >> lowresolution));
		pix[j] = pix[j + 1] = col[(ch & 0x03) >> 0]; j += 2;
		pix[j] = pix[j + 1] = col[(ch & 0x0c) >> 2]; j += 2;
		pix[j] = pix[j + 1] = col[(ch & 0x30) >> 4]; j += 2;
		pix[j] = pix[j + 1] = col[(ch & 0xc0) >> 6]; j += 2;
	}

	draw_char_40(ef, pix, x + 1, y + 1);
}

// draw bichrome character (80 columns)
static void bichrome80(struct ef9345 *ef, uint8_t c, uint8_t a, uint16_t x, uint16_t y, uint8_t cursor)
{
	uint8_t c0, c1, pix[60];
	uint16_t i, j, d;

	c1 = (a & 1) ? (ef->m_dor >> 4) & 7 : ef->m_dor & 7;    //foreground color = DOR
	c0 =  ef->m_mat & 7;                                //background color = MAT

	switch(c & 0x80)
	{
	case 0: //alphanumeric G0 set
		//A0: D = color set
		//A1: U = underline
		//A2: F = flash
		//A3: N = negative
		//C0-6: character code

		if ((a & 4) && (ef->m_pat & 0x40) && (ef->m_blink))
			c1 = c0;    //flash
		if (a & 8)      //negative
		{
			i = c1;
			c1 = c0;
			c0 = i;
		}

		if ((cursor == 0x40) || ((cursor == 0x60) && ef->m_blink))
		{
			i = c1;
			c1 = c0;
			c0 = i;
		}

		d = ((c & 0x7f) >> 2) * 0x40 + (c & 0x03);  //char position

		for(i=0, j=0; i < 10; i++)
		{
			uint8_t ch = read_char(ef, 0, d + 4 * i);
			for (uint8_t b=0; b<6; b++)
				pix[j++] = (ch & (1<<b)) ? c1 : c0;
		}

		//draw the underline
		if ((a & 2) || (cursor == 0x50) || ((cursor == 0x70) && ef->m_blink))
			memset(&pix[54], c1, 6);

		break;
	default: //dedicated mosaic set
		//A0: D = color set
		//A1-3: 3 blocks de 6 pixels
		//C0-6: 7 blocks de 6 pixels
		pix[ 0] = (c & 0x01) ? c1 : c0;
		pix[ 3] = (c & 0x02) ? c1 : c0;
		pix[12] = (c & 0x04) ? c1 : c0;
		pix[15] = (c & 0x08) ? c1 : c0;
		pix[24] = (c & 0x10) ? c1 : c0;
		pix[27] = (c & 0x20) ? c1 : c0;
		pix[36] = (c & 0x40) ? c1 : c0;
		pix[39] = (a & 0x02) ? c1 : c0;
		pix[48] = (a & 0x04) ? c1 : c0;
		pix[51] = (a & 0x08) ? c1 : c0;

		for(i = 0; i < 60; i += 12)
		{
			pix[i + 6] = pix[i];
			pix[i + 9] = pix[i + 3];
		}

		for(i = 0; i < 60; i += 3)
			pix[i + 2] = pix[i + 1] = pix[i];

		break;
	}

	draw_char_80(ef, pix, x, y);
}

// generate 16 bits 40 columns char
static void makechar_16x40(struct ef9345 *ef, uint16_t x, uint16_t y)
{
	uint8_t a, b, c0, c1, i, f, m, n, u, type, dial;
	uint16_t address, iblock;

	iblock = (ef->m_mat & 0x80 && y > 1) ? indexblock(ef, x, y / 2) : indexblock(ef, x, y);
	a = vram_readb(ef, ef->m_block + iblock);
	b = vram_readb(ef, ef->m_block + iblock + 0x0800);

	dial = get_dial(ef, x, (a & 0x80) ? 0 : (((a & 0x20) >> 5) | ((a & 0x10) >> 3)));

	//type and address of the char
	type = ((b & 0x80) >> 4) | ((a & 0x80) >> 6);
	address = ((b & 0x7f) >> 2) * 0x40 + (b & 0x03);

	//reset attributes latch
	if (x == 0)
	{
		ef->m_latchm = ef->m_latchi = ef->m_latchu = ef->m_latchc0 = 0;
	}

	//delimiter
	if ((b & 0xe0) == 0x80)
	{
		type = 0;
		address = ((127) >> 2) * 0x40 + (127 & 0x03); // Force character 127 (negative space) of first type.

		ef->m_latchm = b & 1;
		ef->m_latchi = (b & 2) >> 1;
		ef->m_latchu = (b & 4) >> 2;
	}

	if (a & 0x80)
	{
		ef->m_latchc0 = (a & 0x70) >> 4;
	}

	//char attributes
	c0 = ef->m_latchc0;                         //background
	c1 = a & 0x07;                          //foreground
	i = ef->m_latchi;                           //insert mode
	f  = (a & 0x08) >> 3;                   //flash
	m = ef->m_latchm;                           //conceal
	n  = (a & 0x80) ? 0: ((a & 0x40) >> 6); //negative
	u = ef->m_latchu;                           //underline

	bichrome40(ef, type, address, dial, iblock, x, y, c0, c1, i, f, m, n, u);
}

// generate 24 bits 40 columns char
static void makechar_24x40(struct ef9345 *ef, uint16_t x, uint16_t y)
{
	uint8_t a, b, c, c0, c1, i, f, m, n, u, type, dial;
	uint16_t address, iblock;

	iblock = (ef->m_mat & 0x80 && y > 1) ? indexblock(ef, x, y / 2) : indexblock(ef, x, y);
	c = vram_readb(ef, ef->m_block + iblock);
	b = vram_readb(ef, ef->m_block + iblock + 0x0800);
	a = vram_readb(ef, ef->m_block + iblock + 0x1000);

	if ((b & 0xc0) == 0xc0)
	{
		quadrichrome40(ef, c, b, a, x, y);
		return;
	}

	dial = get_dial(ef, x, (b & 0x02) + ((b & 0x08) >> 3));

	//type and address of the char
	address = ((c & 0x7f) >> 2) * 0x40 + (c & 0x03);
	type = (b & 0xf0) >> 4;

	//char attributes
	c0 = a & 0x07;                  //background
	c1 = (a & 0x70) >> 4;           //foreground
	i = b & 0x01;                   //insert
	f = (a & 0x08) >> 3;            //flash
	m = (b & 0x04) >> 2;            //conceal
	n = ((a & 0x80) >> 7);          //negative
	u = (((b & 0x60) == 0) || ((b & 0xc0) == 0x40)) ? ((b & 0x10) >> 4) : 0; //underline

	bichrome40(ef, type, address, dial, iblock, x, y, c0, c1, i, f, m, n, u);
}

// generate 12 bits 80 columns char
static void makechar_12x80(struct ef9345 *ef, uint16_t x, uint16_t y)
{
	uint16_t iblock = indexblock(ef, x, y);
	//draw the cursor
	uint8_t cursor = 0;
	uint8_t b = ef->reg[7] & 0x80;

	uint8_t i = (ef->reg[6] & 0x1f);
	if (i < 8)
		i &= 1;

	if (iblock == 0x40 * i + (ef->reg[7] & 0x3f))   //cursor position
		cursor = ef->m_mat & 0x70;

	bichrome80(ef, vram_readb(ef, ef->m_block + iblock), (vram_readb(ef, ef->m_block + iblock + 0x1000) >> 4) & 0x0f, 2 * x + 1, y + 1, b ? 0 : cursor);
	bichrome80(ef, vram_readb(ef, ef->m_block + iblock + 0x0800), vram_readb(ef, ef->m_block + iblock + 0x1000) & 0x0f, 2 * x + 2, y + 1, b ? cursor : 0);
}

static void draw_border(struct ef9345 *ef, uint16_t line)
{
	if (ef->m_char_mode == MODE12x80 || ef->m_char_mode == MODE8x80)
		for(int i = 0; i < 82; i++)
			draw_char_80(ef, ef->m_border, i, line);
	else
		for(int i = 0; i < 42; i++)
			draw_char_40(ef, ef->m_border, i, line);
}

static void makechar(struct ef9345 *ef, uint16_t x, uint16_t y)
{
	switch (ef->m_char_mode)
	{
		case MODE24x40:
			makechar_24x40(ef, x, y);
			break;
		case MODEVAR40:
			if (ef->m_variant == TS9347)
			{ // TS9347 char mode definition is different.
				makechar_16x40(ef, x, y);
				break;
			}
			// fallthrough
		case MODE8x80:
			fprintf(stderr, "Unemulated EF9345 mode: %02x\n", ef->m_char_mode);
			break;
		case MODE12x80:
			makechar_12x80(ef, x, y);
			break;
		case MODE16x40:
			if (ef->m_variant == TS9347)
				fprintf(stderr, "Unemulated EF9345 mode: %02x\n", ef->m_char_mode);
			else
				makechar_16x40(ef, x, y);
			break;
		default:
			fprintf(stderr, "Unknown EF9345 mode: %02x\n", ef->m_char_mode);
			break;
	}
}

// Execute EF9345 command
void ef9345_exec(struct ef9345 *ef, uint8_t cmd)
{
	ef->m_state = 0;
	if ((ef->reg[5] & 0x3f) == 39)
		ef->m_state |= 0x10; //S4(LXa) set
	if ((ef->reg[7] & 0x3f) == 39)
		ef->m_state |= 0x20; //S5(LXm) set

	uint16_t a = indexram(ef, 7);

	switch(cmd)
	{
		case 0x00:  //KRF: R1,R2,R3->ram
		case 0x01:  //KRF: R1,R2,R3->ram + increment
			set_busy_flag(ef, 4000);
			vram_writeb(ef, a, ef->reg[1]);
			vram_writeb(ef, a + 0x0800, ef->reg[2]);
			vram_writeb(ef, a + 0x1000, ef->reg[3]);
			if (cmd & 1)
				inc_x(ef, 7);
			break;
		case 0x02:  //KRG: R1,R2->ram
		case 0x03:  //KRG: R1,R2->ram + increment
			set_busy_flag(ef, 5500);
			vram_writeb(ef, a, ef->reg[1]);
			vram_writeb(ef, a + 0x0800, ef->reg[2]);
			if (cmd & 1)
				inc_x(ef, 7);
			break;
		case 0x08:  //KRF: ram->R1,R2,R3
		case 0x09:  //KRF: ram->R1,R2,R3 + increment
			set_busy_flag(ef, 7500);
			ef->reg[1] = vram_readb(ef, a);
			ef->reg[2] = vram_readb(ef, a + 0x0800);
			ef->reg[3] = vram_readb(ef, a + 0x1000);
			if (cmd & 1)
				inc_x(ef, 7);
			break;
		case 0x0a:  //KRG: ram->R1,R2
		case 0x0b:  //KRG: ram->R1,R2 + increment
			set_busy_flag(ef, 7500);
			ef->reg[1] = vram_readb(ef, a);
			ef->reg[2] = vram_readb(ef, a + 0x0800);
			if (cmd & 1)
				inc_x(ef, 7);
			break;
		case 0x30:  //OCT: R1->RAM, main pointer
		case 0x31:  //OCT: R1->RAM, main pointer + inc
			set_busy_flag(ef, 4000);
			vram_writeb(ef, indexram(ef, 7), ef->reg[1]);

			if (cmd & 1) {
				inc_x(ef, 7);
				if ((ef->reg[7] & 0x3f) == 0)
					inc_y(ef, 6);
			}
			break;
		case 0x34:  //OCT: R1->RAM, aux pointer
		case 0x35:  //OCT: R1->RAM, aux pointer + inc
			set_busy_flag(ef, 4000);
			vram_writeb(ef, indexram(ef, 5), ef->reg[1]);

			if (cmd&1)
				inc_x(ef, 5);
			break;
		case 0x38:  //OCT: RAM->R1, main pointer
		case 0x39:  //OCT: RAM->R1, main pointer + inc
			set_busy_flag(ef, 4500);
			ef->reg[1] = vram_readb(ef, indexram(ef, 7));

			if (cmd&1)
			{
				inc_x(ef, 7);

				if ((ef->reg[7] & 0x3f) == 0)
					inc_y(ef, 6);
			}
			break;
		case 0x3c:  //OCT: RAM->R1, aux pointer
		case 0x3d:  //OCT: RAM->R1, aux pointer + inc
			set_busy_flag(ef, 4500);
			ef->reg[1] = vram_readb(ef, indexram(ef, 5));

			if (cmd & 1)
				inc_x(ef, 5);
			break;
		case 0x50:  //KRL: 80 uint8_t - 12 bits write
		case 0x51:  //KRL: 80 uint8_t - 12 bits write + inc
			if (ef->trace)
				fprintf(stderr, "KRL X %d Y %d C '%c' A %02x.\n",
					ef->reg[7], ef->reg[6], ef->reg[1], ef->reg[3]);
			set_busy_flag(ef, 12500);
			if (ef->trace)
				fprintf(stderr, "KRL to %x\n", a);
			vram_writeb(ef, a, ef->reg[1]);
			switch((a / 0x0800) & 1)
			{
				case 0:
				{
					uint8_t tmp_data = vram_readb(ef, a + 0x1000);
					vram_writeb(ef, a + 0x1000, (tmp_data & 0x0f) | (ef->reg[3] & 0xf0));
					break;
				}
				case 1:
				{
					uint8_t tmp_data = vram_readb(ef, a + 0x0800);
					vram_writeb(ef, a + 0x0800, (tmp_data & 0xf0) | (ef->reg[3] & 0x0f));
					break;
				}
			}
			if (cmd&1)
			{
				if ((ef->reg[7] & 0x80) == 0x00) { ef->reg[7] |= 0x80; return; }
				ef->reg[7] &= ~0x80;
				inc_x(ef, 7);
			}
			break;
		case 0x58:  //KRL: 80 uint8_t - 12 bits read
		case 0x59:  //KRL: 80 uint8_t - 12 bits read + inc
			set_busy_flag(ef, 11500);
			ef->reg[1] = vram_readb(ef, a);
			switch((a / 0x0800) & 1)
			{
				case 0:
					ef->reg[3] = vram_readb(ef, a + 0x1000);
					break;
				case 1:
					ef->reg[3] = vram_readb(ef, a + 0x0800);
					break;
			}
			if (cmd&1)
			{
				if ((ef->reg[7] & 0x80) == 0x00)
				{
					ef->reg[7] |= 0x80;
					break;
				}
				ef->reg[7] &= 0x80;
				inc_x(ef, 7);
			}
			break;
		case 0x80:  //IND: R1->ROM (impossible ?)
			break;
		case 0x81:  //IND: R1->TGS
		case 0x82:  //IND: R1->MAT
		case 0x83:  //IND: R1->PAT
		case 0x84:  //IND: R1->DOR
		case 0x87:  //IND: R1->ROR
			set_busy_flag(ef, 2000);
			fprintf(stderr, "INR %d to %02X\n", cmd & 7, ef->reg[1]);
			switch(cmd&7)
			{
				case 1:     ef->m_tgs = ef->reg[1]; break;
				case 2:     ef->m_mat = ef->reg[1]; break;
				case 3:     ef->m_pat = ef->reg[1]; break;
				case 4:     ef->m_dor = ef->reg[1]; break;
				case 7:     ef->m_ror = ef->reg[1]; break;
			}
			set_video_mode(ef);
			ef->m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			break;
		case 0x88:  //IND: ROM->R1
		case 0x89:  //IND: TGS->R1
		case 0x8a:  //IND: MAT->R1
		case 0x8b:  //IND: PAT->R1
		case 0x8c:  //IND: DOR->R1
		case 0x8f:  //IND: ROR->R1

			set_busy_flag(ef, 3500);
			switch(cmd&7)
			{
				case 0:     ef->reg[1] = ef->m_charset[indexrom(ef, 7) & 0x1fff]; break;
				case 1:     ef->reg[1] = ef->m_tgs; break;
				case 2:     ef->reg[1] = ef->m_mat; break;
				case 3:     ef->reg[1] = ef->m_pat; break;
				case 4:     ef->reg[1] = ef->m_dor; break;
				case 7:     ef->reg[1] = ef->m_ror; break;
			}
			ef->m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			break;
		case 0x90:  //NOP: no operation
		case 0x91:  //NOP: no operation
		case 0x95:  //VRM: vertical sync mask reset
		case 0x99:  //VSM: vertical sync mask set
			break;
		case 0xb0:  //INY: increment Y
			set_busy_flag(ef, 2000);
			inc_y(ef, 6);
			ef->m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			break;
		case 0xd5:  //MVB: move buffer MP->AP stop
		case 0xd6:  //MVB: move buffer MP->AP nostop
		case 0xd9:  //MVB: move buffer AP->MP stop
		case 0xda:  //MVB: move buffer AP->MP nostop
		case 0xe5:  //MVD: move double buffer MP->AP stop
		case 0xe6:  //MVD: move double buffer MP->AP nostop
		case 0xe9:  //MVD: move double buffer AP->MP stop
		case 0xea:  //MVD: move double buffer AP->MP nostop
		case 0xf5:  //MVT: move triple buffer MP->AP stop
		case 0xf6:  //MVT: move triple buffer MP->AP nostop
		case 0xf9:  //MVT: move triple buffer AP->MP stop
		case 0xfa:  //MVT: move triple buffer AP->MP nostop
		{
			uint16_t i, a1, a2;
			uint8_t n = (cmd>>4) - 0x0c;
			uint8_t r1 = (cmd&0x04) ? 7 : 5;
			uint8_t r2 = (cmd&0x04) ? 5 : 7;
			int busy = 2000;

			for(i = 0; i < 1280; i++)
			{
				a1 = indexram(ef, r1); a2 = indexram(ef, r2);
				vram_writeb(ef, a2, vram_readb(ef, a1));

				if (n > 1) vram_writeb(ef, a2 + 0x0800, vram_readb(ef, a1 + 0x0800));
				if (n > 2) vram_writeb(ef, a2 + 0x1000, vram_readb(ef, a1 + 0x1000));

				inc_x(ef, r1);
				inc_x(ef, r2);
				if ((ef->reg[5] & 0x3f) == 0 && (cmd&1))
					break;

				if ((ef->reg[7] & 0x3f) == 0)
				{
					if (cmd&1)
						break;
					else
					inc_y(ef, 6);
				}

				busy += 4000 * n;
			}
			ef->m_state &= 0x8f;  //reset S4(LXa), S5(LXm), S6(Al)
			set_busy_flag(ef, busy);
		}
		break;
		case 0x05:  //CLF: Clear page 24 bits
		case 0x07:  //CLG: Clear page 16 bits
		case 0x40:  //KRC: R1 -> ram
		case 0x41:  //KRC: R1 -> ram + inc
		case 0x48:  //KRC: 80 characters - 8 bits
		case 0x49:  //KRC: 80 characters - 8 bits
		default:
			fprintf(stderr, "Unemulated EF9345 cmd: %02x\n", cmd);
	}
}


/**************************************************************
            EF9345 interface
**************************************************************/

void ef9345_update_scanline(struct ef9345 *ef, uint16_t scanline)
{
	uint16_t i;

	if (scanline == 250)
		ef->m_state &= 0xfb;

	set_busy_flag(ef, 104000);

	if (ef->m_char_mode == MODE12x80 || ef->m_char_mode == MODE8x80)
	{
		draw_char_80(ef, ef->m_border, 0, (scanline / 10) + 1);
		draw_char_80(ef, ef->m_border, 81, (scanline / 10) + 1);
	}
	else
	{
		draw_char_40(ef, ef->m_border, 0, (scanline / 10) + 1);
		draw_char_40(ef, ef->m_border, 41, (scanline / 10) + 1);
	}

	if (scanline < 10)
	{
		ef->m_state |= 0x04;
		draw_border(ef, 0);
		if (ef->m_pat & 1)
			for(i = 0; i < 40; i++)
				makechar(ef, i, (scanline / 10));
		else
			for(i = 0; i < 42; i++)
				draw_char_40(ef, ef->m_border, i, 1);
	}
	else if (scanline < 120)
	{
		if (ef->m_pat & 2)
			for(i = 0; i < 40; i++)
				makechar(ef, i, (scanline / 10));
		else
			draw_border(ef, scanline / 10);
	}
	else if (scanline < 250)
	{
		if (ef->m_variant == TS9347)
		{
			for(i = 0; i < 40; i++)
				makechar(ef, i, (scanline / 10));
		}
		else
		{
			if (ef->m_pat & 4) // Lower bulk enable
				for(i = 0; i < 40; i++)
					makechar(ef, i, (scanline / 10));
			else
				draw_border(ef, scanline / 10);

			if (scanline == 240)
				draw_border(ef, 26);
		}
	}
}

/* Fudge until we switch to progressively rendering the display */
void ef9345_rasterize(struct ef9345 *ef)
{
	unsigned i;
	/* No real renderer attached */
	if (ef->m_palette == NULL)
		return;
	for (i = 0; i < 250; i++)
		ef9345_update_scanline(ef, i);
}

uint8_t ef9345_read(struct ef9345 *ef, uint8_t offset)
{
	uint8_t r;
	if (offset & 7)
		r = ef->reg[offset & 7];
	else {
		if (ef->m_bf)
			ef->m_state |= 0x80;
		else
			ef->m_state &= 0x7f;
		r = ef->m_state;
	}
	if (ef->trace)
		fprintf(stderr, "ef9345_read: %02X %02X\n", offset, r);
	return r;
}

void ef9345_write(struct ef9345 *ef, uint8_t offset, uint8_t data)
{
	if (ef->trace)
		fprintf(stderr, "ef9345_write: %02X %02X\n", offset, data);
	ef->reg[offset & 7] = data;

	if (offset & 8)
		ef9345_exec(ef, ef->reg[0]);
}

struct ef9345 *ef9345_create(unsigned variant, uint8_t *vram, uint8_t *vrom, uint16_t vram_mask)
{
	struct ef9345 *ef = malloc(sizeof(struct ef9345));
	if (ef == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(ef, 0, sizeof(*ef));
	ef->m_variant = variant;
	ef->m_charset = vrom;
	ef->m_videoram = vram;
	ef->vram_mask = vram_mask;
	ef9345_init(ef);
	ef9345_reset(ef);
	return ef;
}

void ef9345_free(struct ef9345 *ef)
{
	free(ef);
}

void ef9345_trace(struct ef9345 *ef, unsigned onoff)
{
	ef->trace = !!onoff;
}

void ef9345_set_colourmap(struct ef9345 *ef, uint32_t *cmap)
{
	ef->m_palette = cmap;
}

uint32_t *ef9345_get_raster(struct ef9345 *ef)
{
	return (uint32_t *)ef->raster;
}

/* We will wire the renderer up to this eventually */
void ef9345_cycles(struct ef9345 *ef, unsigned long usec)
{
	if (ef->busy_ticks > usec)
		ef->busy_ticks -= usec;
	else {
		ef->busy_ticks = 0;
		ef->m_bf = 0;
	}
	ef->flash += usec;
	if (ef->flash > 500000) {
		ef->m_blink = !ef->m_blink;
		ef->flash -= 500000;
	}
}
