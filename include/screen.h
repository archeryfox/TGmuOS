#pragma once

#include "renderer.h"

#include <SDL2/SDL.h>

typedef enum {
    ScreenId_Auth,
    ScreenId_Chats,
    ScreenId_Chat,
    ScreenId_Settings,
    ScreenId_Count
} ScreenId;

typedef struct Screen Screen;

struct Screen {
    void (*handle)(Screen* self, const SDL_Event* e);
    void (*update)(Screen* self);
    void (*render)(Screen* self, AppRenderer* renderer);
    void (*destroy)(Screen* self);
};
