#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "asciikbd.h"

struct asciikbd {
	uint8_t key;
	unsigned int ready;
};

struct asciikbd *asciikbd_create(void)
{
	struct asciikbd *kbd = malloc(sizeof(struct asciikbd));
	if (kbd == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	kbd->ready = 0;
	return kbd;
}

void asciikbd_free(struct asciikbd *kbd)
{
	free(kbd);
}

unsigned int asciikbd_ready(struct asciikbd *kbd)
{
	return kbd->ready;
}

uint8_t asciikbd_read(struct asciikbd *kbd)
{
	return kbd->key;
}

void asciikbd_ack(struct asciikbd *kbd)
{
	kbd->ready = 0;
}

void asciikbd_event(void)
{
}

void asciikbd_bind(struct asciikbd *kbd, unsigned window_id)
{
}
