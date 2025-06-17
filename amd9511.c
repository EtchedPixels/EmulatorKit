/*
 *	Wrap the AM9511 library into our usual format. The library is close
 *	to our needs anyway.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "am9511/am9511.h"

#include "amd9511.h"


struct amd9511 {
	void *context;
	unsigned int trace;
};


uint8_t amd9511_read(struct amd9511 *am, uint8_t addr)
{
	addr &= 1;
	if (addr == 0)
		return am_pop(am->context);
	return am_status(am->context);
}

void amd9511_write(struct amd9511 *am, uint8_t addr, uint8_t val)
{
	addr &= 1;
	if (addr == 0)
		am_push(am->context, val);
	else
		am_command(am->context, val);
}

struct amd9511 *amd9511_create(void)
{
	struct amd9511 *am = malloc(sizeof(struct amd9511));
	if (am == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	am->trace = 0;
	am->context = am_create(0,0 /* parameters are unused */);
	if (am->context == NULL) {
		fprintf(stderr, "Failed to create AMD9511 FPU context.\n");
		exit(1);
	}
	return am;
}

void amd9511_free(struct amd9511 *am)
{
	/* Hack as the library has no method of its own */
	free(am->context);
	free(am);
}

void amd9511_reset(struct amd9511 *am)
{
	am_reset(am->context);
}

void amd9511_trace(struct amd9511 *am, unsigned int trace)
{
	am->trace = trace;
}

/* For now */
unsigned int amd9511_irq_pending(struct amd9511 *am)
{

	return 0;
}
