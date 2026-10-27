#define _GNU_SOURCE

#include "images.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define IMAGE_CACHE_SIZE 12

static void* image_library;
static SDL_Surface* (*pIMG_Load)(const char*);
static int (*pIMG_Init)(int);
static const char* (*pIMG_GetError)(void);

static struct {
    char path[256];
    SDL_Surface* surface;
} cache[IMAGE_CACHE_SIZE];
static int cache_next;

int images_available(void) {
    if (image_library) return pIMG_Load != NULL;
    image_library = dlopen("libSDL2_image-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!image_library) {
        image_library = dlopen("libSDL2_image.so.0", RTLD_NOW | RTLD_LOCAL);
    }
    if (!image_library) {
        fprintf(stderr, "images: SDL2_image not found\n");
        return 0;
    }
    void* symbol = dlsym(image_library, "IMG_Load");
    if (symbol) memcpy(&pIMG_Load, &symbol, sizeof(pIMG_Load));
    symbol = dlsym(image_library, "IMG_Init");
    if (symbol) memcpy(&pIMG_Init, &symbol, sizeof(pIMG_Init));
    symbol = dlsym(image_library, "IMG_GetError");
    if (symbol) memcpy(&pIMG_GetError, &symbol, sizeof(pIMG_GetError));
    if (pIMG_Init) pIMG_Init(1 | 2); /* JPG | PNG */
    if (!pIMG_Load) {
        fprintf(stderr, "images: IMG_Load missing\n");
        return 0;
    }
    fprintf(stderr, "images: SDL2_image loaded\n");
    return 1;
}

SDL_Surface* images_get(const char* path) {
    if (!path || !path[0] || !images_available()) return NULL;
    for (int i = 0; i < IMAGE_CACHE_SIZE; ++i) {
        if (cache[i].surface && strcmp(cache[i].path, path) == 0) {
            return cache[i].surface;
        }
    }
    SDL_Surface* loaded = pIMG_Load(path);
    if (!loaded) {
        if (pIMG_GetError) {
            fprintf(stderr, "images: load failed: %s\n", pIMG_GetError());
        }
        return NULL;
    }
    SDL_Surface* converted =
        SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
    if (!converted) return NULL;

    int slot = cache_next;
    cache_next = (cache_next + 1) % IMAGE_CACHE_SIZE;
    if (cache[slot].surface) SDL_FreeSurface(cache[slot].surface);
    snprintf(cache[slot].path, sizeof(cache[slot].path), "%s", path);
    cache[slot].surface = converted;
    return converted;
}

void images_shutdown(void) {
    for (int i = 0; i < IMAGE_CACHE_SIZE; ++i) {
        if (cache[i].surface) {
            SDL_FreeSurface(cache[i].surface);
            cache[i].surface = NULL;
            cache[i].path[0] = '\0';
        }
    }
    if (image_library) {
        dlclose(image_library);
        image_library = NULL;
        pIMG_Load = NULL;
        pIMG_Init = NULL;
        pIMG_GetError = NULL;
    }
}
