#include "input.h"

Input g_input = { {0}, {0}, {0} };

void input_begin_frame(void) {
    for (int i = 0; i < Button_Count; ++i) {
        g_input.just_pressed[i] = 0;
        g_input.just_released[i] = 0;
    }
}

void input_update(Input* self, const SDL_Event* e) {
    if (e->type == SDL_KEYDOWN) {
        int b = -1;
        switch (e->key.keysym.sym) {
            case SDLK_UP:      b = Button_Up; break;
            case SDLK_DOWN:    b = Button_Down; break;
            case SDLK_LEFT:    b = Button_Left; break;
            case SDLK_RIGHT:   b = Button_Right; break;
            case SDLK_RETURN:
            case SDLK_SPACE:   b = Button_A; break;
            case SDLK_BACKSPACE:b = Button_B; break;
            case SDLK_ESCAPE:   b = Button_Menu; break;
            case SDLK_x:        b = Button_X; break;
            case SDLK_y:        b = Button_Y; break;
            case SDLK_m:        b = Button_Menu; break;
            case SDLK_s:       b = Button_Start; break;
            case SDLK_t:       b = Button_Select; break;
        }
        if (b >= 0 && !self->pressed[b]) {
            self->pressed[b] = 1;
            self->just_pressed[b] = 1;
        }
    } else if (e->type == SDL_KEYUP) {
        int b = -1;
        switch (e->key.keysym.sym) {
            case SDLK_UP:      b = Button_Up; break;
            case SDLK_DOWN:    b = Button_Down; break;
            case SDLK_LEFT:    b = Button_Left; break;
            case SDLK_RIGHT:   b = Button_Right; break;
            case SDLK_RETURN:
            case SDLK_SPACE:   b = Button_A; break;
            case SDLK_BACKSPACE:b = Button_B; break;
            case SDLK_ESCAPE:   b = Button_Menu; break;
            case SDLK_x:        b = Button_X; break;
            case SDLK_y:        b = Button_Y; break;
            case SDLK_m:        b = Button_Menu; break;
            case SDLK_s:       b = Button_Start; break;
            case SDLK_t:       b = Button_Select; break;
        }
        if (b >= 0 && self->pressed[b]) {
            self->pressed[b] = 0;
            self->just_released[b] = 1;
        }
    }
}

int input_pressed(Input* self, Button b) {
    return self->pressed[b];
}

int input_just_pressed(Input* self, Button b) {
    return self->just_pressed[b];
}

int input_just_released(Input* self, Button b) {
    return self->just_released[b];
}
