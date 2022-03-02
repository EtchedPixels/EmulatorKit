/*
 *	Minimal emulation of an 82C55A in mode 0 only.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "i82c55a.h"

struct i82c55a {
    uint8_t out_a;
    uint8_t out_b;
    uint8_t out_c;
    uint8_t ctrl;

    int trace;
};

/*
 *	Read from the PPI.
 */
uint8_t i82c55a_read(struct i82c55a *ppi, uint8_t addr)
{
    uint8_t tmp;
    switch (addr & 3) {
    case 0:
        if (ppi->ctrl & 0x10)
            return i82c55a_input(ppi, 0);
        return ppi->out_a;
    case 1:
        if (ppi->ctrl & 0x02)
            return i82c55a_input(ppi, 1);
        return ppi->out_b;
    case 2:
        tmp = i82c55a_input(ppi, 2);
        if (!(ppi->ctrl & 0x01)) {
            tmp &= 0xF0;
            tmp |= ppi->out_c & 0x0F;
        }
        if (!(ppi->ctrl & 0x8)) {
            tmp &= 0x0F;
            tmp |= ppi->out_c & 0xF0;
        }
        return tmp;
    case 3:
        return ppi->ctrl;
    default:
        fprintf(stderr, "Unreachable.\n");
        exit(1);
    }
}

/*
 *	Write to the PPI
 */
void i82c55a_write(struct i82c55a *ppi, uint8_t addr, uint8_t val)
{
    uint8_t tmp;
    addr &= 3;
    switch(addr) {
    case 0:
        ppi->out_a = val;
        if (!(ppi->ctrl & 0x10))
            i82c55a_output(ppi, 0, val);
        break;
    case 1:
        ppi->out_b = val;
        if (!(ppi->ctrl & 0x02))
            i82c55a_output(ppi, 1, val);
        break;
    case 2:
        /* Port C can be half input half output */
        ppi->out_c = val;
        tmp = val;
        if ((ppi->ctrl & 0x09) == 0x09)
            break;
        if (ppi->ctrl & 0x08)
            tmp |= 0xF0;
        if (ppi->ctrl & 0x01)
            tmp |= 0x0F;
        i82c55a_output(ppi, 2, tmp);
        break;
    case 3:
        if (val & 0x80) {	/* Control write */
            ppi->ctrl = val;
            /* Clear anything set to output */
            if (!(val & 0x01))
                ppi->out_c &= 0xF0;
            if (!(val & 0x02))
                ppi->out_b = 0;
            if (!(val & 0x08))
                ppi->out_c &= 0x0F;
            if (!(val & 0x10))
                ppi->out_a = 0;
            break;
        }
        /* Bit operation */
        tmp = (val >> 1) & 0x07;
        ppi->out_c &= ~(1 << tmp);
        ppi->out_c |= (val & 1) << tmp;
        /* Make it into a port C write and recurse */
        i82c55a_write(ppi, 2, ppi->out_c);
        break;
   }
}

void i82c55a_reset(struct i82c55a *ppi)
{
    /* Reset goes entirely input */
    ppi->ctrl = 0x9B;
    /* This puts everything into input mode with pullups */
    i82c55a_output(ppi, 0, 0xFF);
    i82c55a_output(ppi, 1, 0xFF);
    i82c55a_output(ppi, 2, 0xFF);
    /* The output register state does not matter and it will be zeroed
       if we switch anything to output anyway */
}

struct i82c55a *i82c55a_create(void)
{
    struct i82c55a *ppi = malloc(sizeof(struct i82c55a));
    if (ppi == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }
    i82c55a_reset(ppi);
    return ppi;
}

void i82c55a_free(struct i82c55a *ppi)
{
    free(ppi);
}

void i82c55a_trace(struct i82c55a *ppi, int onoff)
{
    ppi->trace = onoff;
}
