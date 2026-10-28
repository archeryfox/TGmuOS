#pragma once

#include "screen.h"

typedef struct {
    Screen base;
    int return_screen;
} ScreenProfile;

ScreenProfile* screen_profile_create(void);
void screen_profile_set_return(ScreenProfile* screen, int return_screen);
