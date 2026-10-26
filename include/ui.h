#pragma once

#include "renderer.h"

#include <SDL2/SDL.h>

int ui_init(void);
void ui_shutdown(void);
void ui_fill(AppRenderer* renderer, SDL_Rect rect, SDL_Color color);
void ui_round_rect(AppRenderer* renderer, SDL_Rect rect, int radius, SDL_Color color);
void ui_text(AppRenderer* renderer, const char* text, int x, int y, int size, SDL_Color color);
int ui_text_width(const char* text, int size);
