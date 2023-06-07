/*

	Dervied from code:

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

struct joystick {
	int                 instance_id;
	SDL_GameController *controller;
	uint8_t             button_mask;
};

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

static unsigned trace = 0;
static struct joystick joystick[NUM_JS];
static int num_js = 0;

static struct joystick *js_find(int id)
{
	struct joystick *js = joystick;
	while(js < &joystick[NUM_JS]) {
		if (js->instance_id == id)
			return js;
		js++;
	}
	return NULL;
}

void joystick_trace(unsigned enable)
{
	trace = !!enable;
}

void joystick_create(void)
{
	struct joystick *js = joystick;
	int i;

	if (trace)
		fprintf(stderr,"joystick_create\n");

	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

	/* Set up the table */
	for (i = 0; i < NUM_JS; ++i) {
		js->instance_id = -1;
		js->button_mask = 0xFF;
	}

	/* Add the joysticks */
	num_js = SDL_NumJoysticks();
	for (i = 0; i < num_js; ++i) {
		joystick_add(i);
	}
}

void joystick_add(int index)
{
	SDL_GameController *c;
	SDL_JoystickID id;
	struct joystick *js;

	if (trace)
		fprintf(stderr,"joystick_add\n");

	if (!SDL_IsGameController(index))
		return;

	c = SDL_GameControllerOpen(index);
	if (c == NULL) {
		fprintf(stderr, "Could not open controller %d: %s\n", index, SDL_GetError());
		return;
	}

	id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));

	js = js_find(id);

	if (js == NULL) {
		js = js_find(-1);
		if (js != NULL) {
			js->instance_id = id;
			js->controller = c;
			js->button_mask = 0xFF;
		}
	}
}

void joystick_remove(int id)
{
	struct joystick *js;
	SDL_GameController *c;

	if (trace)
		fprintf(stderr,"joystick_remove\n");

	js = js_find(id);

	if (js == NULL)
		return;

	c = SDL_GameControllerFromInstanceID(id);
	if (c == NULL) {
		fprintf(stderr, "Could not find controller from instance_id %d: %s\n", id, SDL_GetError());
	} else {
		SDL_GameControllerClose(c);
		js->controller = NULL;
		js->instance_id = -1;
	}
}

void joystick_button_down(int id, uint8_t button)
{
	struct joystick *js = js_find(id);
	if (js != NULL) {
		js->button_mask &= ~(button_map[button]);
		if (trace)
			fprintf(stderr,"joystick_button_down: %02X\n",js->button_mask);
	}
}

void joystick_button_up(int id, uint8_t button)
{
	struct joystick *js = js_find(id);
	if (js != NULL) {
		js->button_mask |= button_map[button];
		if (trace)
			fprintf(stderr,"joystick_button_up: %02X\n",js->button_mask);
	}
}


uint8_t joystick_read(int id)
{
	uint8_t data = 0xFF;
	struct joystick *js = js_find(id);
	if (js)
		data = js->button_mask;
#if 0
	if (trace)
		fprintf(stderr,"joystick_read: %02x\n", data);
#endif
	return data;
}

