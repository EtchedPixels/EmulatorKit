#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "ramf.h"

/*
 *	RAMF Battery Backed RAM Disk
 */

struct ramf {
	int fd;

	uint8_t *addr;
	uint8_t port[2][2];
	uint16_t count[2];

	unsigned int trace;
};

static uint8_t *ramaddr(struct ramf *ramf, uint8_t high)
{
	uint32_t offset = high ? 4096 * 1024 : 0;
	offset += (ramf->port[high][0] & 0x1F) << 17;
	offset += ramf->port[high][1] << 9;
	offset += ramf->count[high]++;
	return ramf->addr + offset;
}

void ramf_write(struct ramf *ramf, uint8_t addr, uint8_t val)
{
	uint8_t high = (addr & 4) ? 1 : 0;
	if (ramf->trace)
		fprintf(stderr, "RAMF write %d = %d\n", addr, val);
	addr &= 3;
	if (addr == 0)
		*ramaddr(ramf, high) = val;
	else if (addr == 3)
		return;
	else {
		ramf->port[high][addr & 1] = val;
		ramf->count[high] = 0;
	}
}

uint8_t ramf_read(struct ramf *ramf, uint8_t addr)
{
	uint8_t high = (addr & 4) ? 1 : 0;
	if (ramf->trace)
		fprintf(stderr, "RAMF read %d\n", addr);
	addr &= 3;
	if (addr == 0)
		return *ramaddr(ramf, high);
	if (addr == 3)
		return 0;	/* or 1 for write protected */
	return ramf->port[high][addr];
}

struct ramf *ramf_create(const char *path)
{
	struct ramf *ramf = malloc(sizeof(struct ramf));
	if (ramf == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(ramf, 0, sizeof(struct ramf));

	ramf->fd = open(path, O_RDWR|O_CREAT, 0600);
	if(ramf->fd == -1) {
		perror(path);
		free(ramf);
		return NULL;
	}
	ramf->addr = mmap(NULL, 8192 * 1024, PROT_READ|PROT_WRITE,
		MAP_SHARED, ramf->fd, 0L);
	if (ramf->addr == MAP_FAILED) {
		perror("mmap");
		close(ramf->fd);
		return NULL;
	}
	return ramf;
}

void ramf_free(struct ramf *ramf)
{
	if (ramf->addr)
		munmap(ramf->addr, 8192 * 1024);
	if (ramf->fd)
		close(ramf->fd);
	free(ramf);
}

void ramf_trace(struct ramf *ramf, unsigned int onoff)
{
	ramf->trace = onoff;
}
