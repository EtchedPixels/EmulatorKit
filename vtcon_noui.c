/*
 *	Plug console interface into a terminal
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "serialdevice.h"
#include "vtcon.h"

static unsigned vtcon_ready(struct serial_device *dev)
{
	return 2;
}

static void vtcon_put(struct serial_device *dev, uint8_t c)
{
}

static uint8_t vtcon_get(struct serial_device *dev)
{
	return 0xFF;
}

struct serial_device *vt_create(const char *name, unsigned type)
{
	struct serial_device *dev = malloc(sizeof(struct serial_device));
	dev->private = dev;
	dev->name = "Terminal";
	dev->get = vtcon_get;
	dev->put = vtcon_put;
	dev->ready = vtcon_ready;
	return dev;
}
