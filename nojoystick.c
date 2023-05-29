#include <stdint.h>
#include <stdbool.h>
#include "joystick.h"

bool Joystick_slots_enabled[NUM_JOYSTICKS] = {false, false};

bool joystick_create(void)
{
	return false;
}

uint8_t joystick_read(uint8_t i)
{
	return 0x78;
}

void joystick_trace(bool enable)
{
}
