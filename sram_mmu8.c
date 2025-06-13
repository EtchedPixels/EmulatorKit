/*
 *	The MMU/RAM card for the 68008 setup (also usable for other things)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sram_mmu8.h"

struct sram_mmu {
	uint8_t ram[512 * 1024];
	uint8_t map[32768];
	uint8_t map_valid[32768];	/* Not present in real hw just a debug aid */
	uint8_t latch;

	unsigned int trace;
};

#define MAP_UNINIT	0xFFFF

void sram_mmu_set_latch(struct sram_mmu *mmu, uint8_t latch)
{
	mmu->latch = latch;
}

uint8_t *sram_mmu_translate(struct sram_mmu *mmu, uint32_t addr, unsigned int wr,
			    unsigned int silent, unsigned int super, unsigned int *berr)
{
	unsigned int  page;
	uint16_t map;

	*berr = 0;

	addr &= 0x7FFFF;
	page = addr >> 13;
	page |= (mmu->latch & 0x7F) << 8;
	if (super)
		page |= (1 << 7);

	if ((mmu->latch & 0x80) == 0) {
		if (wr) {
			/* Remember maps we've written to at least once */
			mmu->map_valid[page] = 1;
			return mmu->map + page;
		}
		return NULL;
	}

	map = mmu->map[page];

	if (!mmu->map_valid[page]) {
		if (!silent)
			fprintf(stderr, "[sram-mmu: access via uninitialized map entry %04X]\n", page);
		return NULL;
	}

	if (map & 0x80) {
		if (mmu->trace && !silent)
			fprintf(stderr, "[sram-mmu: bus error for %06X]\n", addr);
		*berr = 1;
		return NULL;
	}
	if (map & 0x40) {		/* Our logic doesn't fault this case */
		if (wr) {
			if (mmu->trace && !silent)
				fprintf(stderr, "[sram-mmu: write fault for %06X]\n", addr);
			return NULL;
		}
	}
	return mmu->ram + ((map & 0x3F) << 13) + (addr & 0x1FFF);
}

struct sram_mmu *sram_mmu_create(void)
{
	struct sram_mmu *mmu = malloc(sizeof(struct sram_mmu));
	if (mmu == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(mmu, 0, sizeof(struct sram_mmu));
	return mmu;
}

void sram_mmu_free(struct sram_mmu *mmu)
{
	free(mmu);
}

void sram_mmu_trace(struct sram_mmu *mmu, unsigned int trace)
{
	mmu->trace = trace;
}
