/*
 *	Tarbell single density FDC glue to the WD1771
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "wd17xx.h"
#include "tarbell_fdc.h"

static uint8_t lcmd;	/* FIXME sort out true DRQ emulation */

static uint8_t waitport(struct wd17xx *fdc)
{
	/* TODO: figure the finer details out here */
	uint8_t st;

	/* Bad fudge for the halt cpu properties */
	/* We also don't expose the true DRQ in 177x so need to mess
	   about */
	while(1) {
		st = 0;
		/* Real hardware waits until DRQ on transfers */
		if (wd17xx_intrq(fdc))
			st |= 0x01;
		if ((wd17xx_status_noclear(fdc) & 2) && lcmd >= 0x80)
			st |= 0x80;
		fprintf(stderr, "waitport: %02X\n", st);
		if (st)
			break;
		wd17xx_tick(fdc, 1);
	}
	st ^= 0x01;	/* Testing */
	fprintf(stderr, "tbc: wait port %02X\n", st);
	return st;
}

uint8_t tbfdc_read(struct wd17xx *fdc, unsigned addr)
{
	switch(addr & 7) {
	case 0:
		/* Fudge */
		return wd17xx_status(fdc) ^ 0x80;
	case 1:
		return wd17xx_read_track(fdc);
	case 2:
		return wd17xx_read_sector(fdc);
	case 3:
		return wd17xx_read_data(fdc);
	case 4:
		return waitport(fdc);
	default:
		return 0xFF;
	}
}
void tbfdc_write(struct wd17xx *fdc, unsigned addr, uint8_t val)
{
	switch(addr & 7) {
	case 0:
		lcmd = val;
		wd17xx_command(fdc, val);
		break;
	case 1:
		wd17xx_write_track(fdc, val);
		break;
	case 2:
		wd17xx_write_sector(fdc, val);
		break;
	case 3:
		wd17xx_write_data(fdc, val);
		break;
	case 4:
		/* Drive selects etc - F2 is drive 0 as drive num inverted */
		if (val & 2)
			wd17xx_set_drive(fdc, (~val >> 4) & 0x03);
		break;
	default:;
	}
}

/* Create a default setting tarbell fdc */

struct wd17xx *tbfdc_create(void)
{
	/* Really a 1771 but that's fine, just don't try DD */
	struct wd17xx *fdc = wd17xx_create(1772);
	wd17xx_set_density(fdc, DEN_SD);
	wd17xx_set_media_density(fdc, 0, DEN_SD);
	wd17xx_set_media_density(fdc, 1, DEN_SD);
	wd17xx_set_media_density(fdc, 2, DEN_SD);
	wd17xx_set_media_density(fdc, 3, DEN_SD);
	return fdc;
}
