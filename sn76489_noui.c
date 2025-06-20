#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "sn76489.h"

struct sn76489 {
	unsigned dummy;
};

struct sn76489 *sn76489_create(void)
{
	struct sn76489 *sn = malloc(sizeof(struct sn76489));
	if (sn == NULL) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	return sn;
}

uint8_t sn76489_ready(struct sn76489 *sn)
{
	return 1;
}

void sn76489_write(struct sn76489 *sn, uint8_t val)
{
}

void sn76489_destroy(struct sn76489 *sn)
{
	free(sn);
}
