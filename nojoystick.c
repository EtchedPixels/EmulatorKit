#include <stdint.h>
#include <stdbool.h>
#include "joystick.h"

bool Joystick_slots_enabled[NUM_JS] = {false, false};

void joystick_create(void)
{
}

uint8_t joystick_read(int i)
{
	return 0x78;
}

void joystick_trace(unsigned enable)
{
}
