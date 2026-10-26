#pragma once

#include "screen.h"

typedef struct {
    Screen base;
    int selected;
    int server_mode;
    int selected_server;
    int server_scroll;
    int editing;
    int uppercase;
    int selected_key;
    int edit_length;
    char edit_buffer[256];
} ScreenSettings;

ScreenSettings* screen_settings_create(void);
