#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define WITH_SDL

#include "system.h"
#include "joystick.h"

void ui_event(void)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_JOYDEVICEADDED:
			joystick_add(ev.jdevice.which);
			break;
		case SDL_JOYDEVICEREMOVED:
			joystick_remove(ev.jdevice.which);
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			joystick_button_down(ev.cbutton.which, ev.cbutton.button);
			break;
		case SDL_CONTROLLERBUTTONUP:
			joystick_button_up(ev.cbutton.which, ev.cbutton.button);
			break;
		case SDL_QUIT:
			emulator_done = 1;
			break;
		}
	}
}
