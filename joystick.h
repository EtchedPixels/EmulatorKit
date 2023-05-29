#pragma once
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdint.h>
#include <stdbool.h>

#define NUM_JOYSTICKS 2

extern bool Joystick_slots_enabled[NUM_JOYSTICKS];

// initialize SDL controllers
bool joystick_create(void);

void joystick_add(int index);
void joystick_remove(int index);

void joystick_button_down(int instance_id, uint8_t button);
void joystick_button_up(int instance_id, uint8_t button);

void joystick_trace(bool enable);

uint8_t joystick_read(uint8_t i);

#endif
