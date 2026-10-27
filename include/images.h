#pragma once

#include "renderer.h"

#include <SDL2/SDL.h>

/* Lazy dlopen() of the device SDL2_image; 0 when unavailable. */
int images_available(void);

/* Load (and LRU-cache) an image as ARGB8888 surface; NULL on failure. */
SDL_Surface* images_get(const char* path);

void images_shutdown(void);
