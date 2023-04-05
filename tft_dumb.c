/*
 *	Emulate variants of the bazillion co-ordinate addressed TFT
 *	devices
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tft_dumb.h"

void tft_write(struct tft_dumb *tft, unsigned cd, uint8_t data)
{
    if (cd) {
        tft->step = 0;
        tft->cmd = data;
        return;
    }
    tft->step++;
    if (tft->cmd == tft->row_port) {
        if (tft->step == 1)
            tft->row = data << 8;
        else
            tft->row |= data;
        return;
    }
    if (tft->cmd == tft->col_port) {
        if (tft->step == 1)
            tft->col = data << 8;
        else
            tft->col |= data;
        return;
    }
    if (tft->cmd == tft->data_port) {
        tft->data <<= 8;
        tft->data |= data;
        if (tft->step == tft->bytesperpixel) {
            tft->data |= 0xFF000000;
            /* Assume 24bpp for now */
            if (tft->row < tft->height && tft->col < tft->width)
                tft->rasterbuffer[tft->row * tft->width + tft->col] = tft->data;
            tft->col++;
            if (tft->col == tft->width) {
                tft->row++;
                tft->col = 0;
                if (tft->row == tft->height)
                    tft->row = 0;
            }
            tft->data = 0;
            tft->step = 0;
        }
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
    /* Ignore type for now - will add as we expand */
    tft->height = 240;
    tft->width = 320;
    tft->bytesperpixel = 3;
    tft->row_port = 1;
    tft->col_port = 2;
    tft->data_port = 0;

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
