#pragma once

#include "screen.h"

typedef struct {
    Screen base;
    int selected;
    int scroll;
} ScreenChats;

ScreenChats* screen_chats_create(void);
