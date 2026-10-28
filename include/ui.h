#pragma once

#include "renderer.h"

#include <SDL2/SDL.h>

int ui_init(void);
void ui_shutdown(void);
void ui_fill(AppRenderer* renderer, SDL_Rect rect, SDL_Color color);
void ui_round_rect(AppRenderer* renderer, SDL_Rect rect, int radius, SDL_Color color);
void ui_text(AppRenderer* renderer, const char* text, int x, int y, int size, SDL_Color color);
int ui_text_width(const char* text, int size);

/* Animated three-dot spinner centred at (x, y); phase from SDL_GetTicks(). */
void ui_spinner(AppRenderer* renderer, int x, int y, int radius, uint32_t phase_ms);

/* Horizontal progress bar with rounded ends. fraction 0..1. */
void ui_progress(AppRenderer* renderer, SDL_Rect rect, double fraction,
                 SDL_Color fill, SDL_Color track);

/* Blit an ARGB8888 surface into the canvas scaled to w x h canvas units. */
void ui_blit_surface(AppRenderer* renderer, const SDL_Surface* surface,
                     int x, int y, int w, int h);
