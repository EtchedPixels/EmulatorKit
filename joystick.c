/*

	Copyright (c) 2019 Michael Steil

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions
	are met:

	1. Redistributions of source code must retain the above copyright
	notice, this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright
	notice, this list of conditions and the following disclaimer in the
	documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
	FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
	COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
	BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
	OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
	TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
	USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "joystick.h"

#include <SDL2/SDL.h>
#include <stdio.h>

struct joystick_info {
	int                 instance_id;
	SDL_GameController *controller;
	uint8_t             button_mask;
};

static const uint16_t button_map[SDL_CONTROLLER_BUTTON_MAX] = {
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

static bool trace = false;
static struct joystick_info *Joystick_controllers     = NULL;
static int                   Num_joystick_controllers = 0;

static void
resize_joystick_controllers(int new_size)
{
	if (new_size == 0) {
		free(Joystick_controllers);
		Joystick_controllers     = NULL;
		Num_joystick_controllers = 0;
		return;
	}

	struct joystick_info *old_controllers = Joystick_controllers;
	Joystick_controllers = (struct joystick_info *)malloc(sizeof(struct joystick_info) * new_size);

	int min_size = new_size < Num_joystick_controllers ? new_size : Num_joystick_controllers;
	if (min_size > 0) {
		memcpy(Joystick_controllers, old_controllers, sizeof(struct joystick_info) * min_size);
		free(old_controllers);
	}

	for (int i = min_size; i < new_size; ++i) {
		Joystick_controllers[i].instance_id = -1;
		Joystick_controllers[i].controller  = NULL;
		Joystick_controllers[i].button_mask = 0xFF;
	}
}

static void
add_joystick_controller(struct joystick_info *info)
{
	int i;
	if (trace)
		fprintf(stderr,"add_joystick_controller\n");

	for (i = 0; i < Num_joystick_controllers; ++i) {
		if (Joystick_controllers[i].instance_id == -1) {
			memcpy(&Joystick_controllers[i], info, sizeof(struct joystick_info));
			return;
		}
	}

	i = Num_joystick_controllers;
	resize_joystick_controllers(Num_joystick_controllers << 1);

	memcpy(&Joystick_controllers[i], info, sizeof(struct joystick_info));
}

static void
remove_joystick_controller(int instance_id)
{
	if (trace )
		fprintf(stderr,"remove_joystick_controller\n");

	for (int i = 0; i < Num_joystick_controllers; ++i) {
		if (Joystick_controllers[i].instance_id == instance_id) {
			Joystick_controllers[i].instance_id = -1;
			Joystick_controllers[i].controller  = NULL;
			return;
		}
	}
}

static struct joystick_info *
find_joystick_controller(int instance_id)
{
	for (int i = 0; i < Num_joystick_controllers; ++i) {
		if (Joystick_controllers[i].instance_id == instance_id) {
			return &Joystick_controllers[i];
		}
	}

	return NULL;
}

bool Joystick_slots_enabled[NUM_JOYSTICKS] = {false, false};
static int Joystick_slots[NUM_JOYSTICKS];

void
joystick_trace(bool enable)
{
	if ( enable )
		trace = true;
	else
		trace = false;
}

bool
joystick_create(void)
{
	if (trace)
		fprintf(stderr,"joystick_create\n");

	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

	for (int i = 0; i < NUM_JOYSTICKS; ++i) {
		Joystick_slots[i] = -1;
	}

	const int num_joysticks = SDL_NumJoysticks();

	Num_joystick_controllers = num_joysticks > 16 ? num_joysticks : 16;
	Joystick_controllers     = malloc(sizeof(struct joystick_info) * Num_joystick_controllers);

	for (int i = 0; i < Num_joystick_controllers; ++i) {
		Joystick_controllers[i].instance_id = -1;
		Joystick_controllers[i].controller  = NULL;
		Joystick_controllers[i].button_mask = 0xFF;
	}

	for (int i = 0; i < num_joysticks; ++i) {
		joystick_add(i);
	}

	return true;
}

void
joystick_add(int index)
{
	if (trace)
		fprintf(stderr,"joystick_add\n");

	if (!SDL_IsGameController(index)) {
		return;
	}

	SDL_GameController *controller = SDL_GameControllerOpen(index);
	if (controller == NULL) {
		fprintf(stderr, "Could not open controller %d: %s\n", index, SDL_GetError());
		return;
	}

	SDL_JoystickID instance_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
	bool           exists      = false;
	for (int i = 0; i < NUM_JOYSTICKS; ++i) {
		if (!Joystick_slots_enabled[i]) {
			continue;
		}

		if (Joystick_slots[i] == instance_id) {
			exists = true;
			break;
		}
	}

	if (!exists) {
		for (int i = 0; i < NUM_JOYSTICKS; ++i) {
			if (!Joystick_slots_enabled[i]) {
				continue;
			}

			if (Joystick_slots[i] == -1) {
				Joystick_slots[i] = instance_id;
				break;
			}
		}

		struct joystick_info new_info;
		new_info.instance_id = instance_id;
		new_info.controller  = controller;
		new_info.button_mask = 0xFF;
		add_joystick_controller(&new_info);
	}
}

void
joystick_remove(int instance_id)
{
	if (trace)
		fprintf(stderr,"joystick_remove\n");

	for (int i = 0; i < NUM_JOYSTICKS; ++i) {
		if (Joystick_slots[i] == instance_id) {
			Joystick_slots[i] = -1;
			break;
		}
	}

	SDL_GameController *controller = SDL_GameControllerFromInstanceID(instance_id);
	if (controller == NULL) {
		fprintf(stderr, "Could not find controller from instance_id %d: %s\n", instance_id, SDL_GetError());
	} else {
		SDL_GameControllerClose(controller);
		remove_joystick_controller(instance_id);
	}
}

void
joystick_button_down(int instance_id, uint8_t button)
{
	struct joystick_info *joy = find_joystick_controller(instance_id);
	if (joy != NULL) {
		joy->button_mask &= ~(button_map[button]);
		if (trace)
			fprintf(stderr,"joystick_button_down: %02X\n",joy->button_mask);
	}
}

void
joystick_button_up(int instance_id, uint8_t button)
{
	struct joystick_info *joy = find_joystick_controller(instance_id);
	if (joy != NULL) {
		joy->button_mask |= button_map[button];
		if (trace)
			fprintf(stderr,"joystick_button_up: %02X\n",joy->button_mask);
	}
}


uint8_t
joystick_read( uint8_t i )
{
	uint8_t Joystick_data = 0xFF;
	struct joystick_info *joy = find_joystick_controller(Joystick_slots[i]);
	if (joy != NULL) {
		Joystick_data = joy->button_mask;
	}
#if 0
	if (trace)
		fprintf(stderr,"joystick_read: %02x\n", Joystick_data);
#endif
	return Joystick_data;
}

