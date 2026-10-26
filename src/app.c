#include "app.h"

App g_app = { ScreenId_Auth };

void app_set_screen(ScreenId id) {
    if (id >= 0 && id < ScreenId_Count) {
        g_app.current = id;
    }
}

ScreenId app_current_screen(void) {
    return g_app.current;
}
