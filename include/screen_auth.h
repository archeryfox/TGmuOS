#pragma once

#include "screen.h"

typedef struct {
    Screen base;
    int selected_key;
    int uppercase;
    int input_length;
    char input[128];
} ScreenAuth;

ScreenAuth* screen_auth_create(void);
