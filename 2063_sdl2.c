#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define WITH_SDL

#include "event.h"
#include "system.h"
#include "joystick.h"

static const uint8_t button_map[SDL_CONTROLLER_BUTTON_MAX] = {
	1 << 0,  // SDL_CONTROLLER_BUTTON_A
	1 << 4,  // SDL_CONTROLLER_BUTTON_B
	0,       // SDL_CONTROLLER_BUTTON_X
	0,       // SDL_CONTROLLER_BUTTON_Y
	0,       // SDL_CONTROLLER_BUTTON_BACK
	0,       // SDL_CONTROLLER_BUTTON_GUIDE
	0,       // SDL_CONTROLLER_BUTTON_START
	0,       // SDL_CONTROLLER_BUTTON_LEFTSTICK
	0,       // SDL_CONTROLLER_BUTTON_RIGHTSTICK
	0,       // SDL_CONTROLLER_BUTTON_LEFTSHOULDER
	0,       // SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
	1 << 7,  // SDL_CONTROLLER_BUTTON_DPAD_UP
	1 << 6,  // SDL_CONTROLLER_BUTTON_DPAD_DOWN
	1 << 2,  // SDL_CONTROLLER_BUTTON_DPAD_LEFT
	1 << 5,  // SDL_CONTROLLER_BUTTON_DPAD_RIGHT
};

static int js2063_event(void *dev, void *evp)
{
	SDL_Event *ev = evp;
	while (SDL_PollEvent(ev)) {
		switch(ev->type) {
		case SDL_JOYDEVICEADDED:
			joystick_add(ev->jdevice.which);
			break;
		case SDL_JOYDEVICEREMOVED:
			joystick_remove(ev->jdevice.which);
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			joystick_button_down(ev->cbutton.which, ev->cbutton.button, button_map);
			break;
		case SDL_CONTROLLERBUTTONUP:
			joystick_button_up(ev->cbutton.which, ev->cbutton.button, button_map);
			break;
		}
	}
	return 0;
}

void js2063_add_events(void)
{
	add_ui_handler(js2063_event, NULL);
}
