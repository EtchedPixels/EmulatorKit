/*
 *	Convert mouse movements into mouse serial data and provide it each
 *	time we are asked for a next byte
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "serialdevice.h"
#include "mousesystems.h"

static int8_t mouse_x, mouse_y;
static unsigned mouse_buttons;
static unsigned mouse_state;

void msmouse_update(int dx, int dy, unsigned buttons)
{
	/* Should probably scale and range check this lot */
	mouse_x += dx;
	mouse_y += dy;
	mouse_buttons = buttons;
}

static uint8_t msmouse_get(struct serial_device *dev)
{
	uint8_t r;
	static uint8_t save_y, save_x;

	switch(mouse_state++) {
	case 0:
		r = 0x40;
		if (mouse_buttons & 1)
			r |= 0x20;
		if (mouse_buttons & 2)
			r |= 0x10;
		r |= (mouse_y >> 4) & 0x0C;
		r |= (mouse_x >> 6) & 0x03;
		save_y = mouse_y;
		save_x = mouse_x;
		mouse_y = 0;
		mouse_x = 0;
		return r;
	case 1:
		return save_y & 0x3F;
	case 2:
		return save_x & 0x3F;
	default:
		/* Shouldn't happen */
		return 0x00;
	}
}

static void msmouse_put(struct serial_device *dev, uint8_t ch)
{
	/* No talk back */
}

static uint8_t msmouse_ready(struct serial_device *dev)
{
	return mouse_state < 3 ? 1 : 0;	/* Never write ready */
}

void msmouse_tick(void)			/* Call about 40 times/sec to give a report */
{
	mouse_state = 0;
}

struct serial_device msmouse = {
	"MS Mouse",
	NULL,
	msmouse_get,
	msmouse_put,
	msmouse_ready
};
