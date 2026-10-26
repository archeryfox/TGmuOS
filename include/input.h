#pragma once

#include <SDL2/SDL.h>

typedef enum {
    Button_Up,
    Button_Down,
    Button_Left,
    Button_Right,
    Button_A,
    Button_B,
    Button_X,
    Button_Y,
    Button_Menu,
    Button_Start,
    Button_Select,
    Button_Power,
    Button_Count
} Button;

typedef struct {
    int pressed[Button_Count];
    int just_pressed[Button_Count];
    int just_released[Button_Count];
} Input;

extern Input g_input;

void input_begin_frame(void);
void input_update(Input* self, const SDL_Event* e);
int input_pressed(Input* self, Button b);
int input_just_pressed(Input* self, Button b);
int input_just_released(Input* self, Button b);
