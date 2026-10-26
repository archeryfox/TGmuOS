#pragma once

#include "screen.h"

typedef struct {
    ScreenId current;
} App;

extern App g_app;

void app_set_screen(ScreenId id);
ScreenId app_current_screen(void);
