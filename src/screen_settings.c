#include "screen_settings.h"
#include "app.h"
#include "preferences.h"
#include "telegram.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const SDL_Color COLOR_BG = {20, 25, 33, 255};
static const SDL_Color COLOR_HEADER = {29, 39, 53, 255};
static const SDL_Color COLOR_TEXT = {238, 241, 247, 255};
static const SDL_Color COLOR_MUTED = {156, 168, 185, 255};
static const SDL_Color COLOR_PANEL = {31, 39, 51, 255};
static const SDL_Color COLOR_BLUE = {46, 140, 220, 255};

static void copy_text_fitting(char* output, size_t capacity,
                              const char* text, int max_width, int size) {
    size_t length = strlen(text);
    if (length >= capacity) length = capacity - 1;
    while (length > 0) {
        while (length > 0 && ((unsigned char)text[length] & 0xc0) == 0x80) {
            --length;
        }
        memcpy(output, text, length);
        output[length] = '\0';
        if (ui_text_width(output, size) <= max_width) return;
        --length;
    }
    output[0] = '\0';
}

static void keep_server_visible(ScreenSettings* screen) {
    int count = preferences_subscription_server_count();
    if (screen->selected_server < screen->server_scroll) {
        screen->server_scroll = screen->selected_server;
    } else if (screen->selected_server >= screen->server_scroll + 7) {
        screen->server_scroll = screen->selected_server - 6;
    }
    int max_scroll = count > 7 ? count - 7 : 0;
    if (screen->server_scroll > max_scroll) screen->server_scroll = max_scroll;
}

static void enter_server_list(ScreenSettings* screen) {
    int count = preferences_subscription_server_count();
    const char* selected = preferences_subscription_selected_server();
    screen->selected_server = 0;
    for (int i = 0; i < count; ++i) {
        if (strcmp(preferences_subscription_server_id(i), selected) == 0) {
            screen->selected_server = i;
            break;
        }
    }
    screen->server_scroll = 0;
    screen->server_mode = 1;
    keep_server_visible(screen);
}

static void settings_handle(Screen* self, const SDL_Event* event) {
    ScreenSettings* screen = (ScreenSettings*)self;
    if (event->type != SDL_KEYDOWN || event->key.repeat) return;
    if (screen->server_mode) {
        int count = preferences_subscription_server_count();
        switch (event->key.keysym.sym) {
            case SDLK_BACKSPACE:
            case SDLK_ESCAPE:
                screen->server_mode = 0;
                return;
            case SDLK_UP:
                if (screen->selected_server > 0) --screen->selected_server;
                keep_server_visible(screen);
                return;
            case SDLK_DOWN:
                if (screen->selected_server + 1 < count) ++screen->selected_server;
                keep_server_visible(screen);
                return;
            case SDLK_x: {
                char current_id[PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH] = "";
                if (screen->selected_server < count) {
                    snprintf(current_id, sizeof(current_id), "%s",
                             preferences_subscription_server_id(
                                 screen->selected_server));
                }
                int reachable = 0;
                int pinged = preferences_ping_all_servers(1500, &reachable);
                if (pinged < 0) {
                    SDL_Log("Unable to save the server list after ping");
                } else {
                    SDL_Log("Pinged %d servers, %d reachable",
                            pinged, reachable);
                }
                int new_count = preferences_subscription_server_count();
                screen->selected_server = 0;
                for (int i = 0; i < new_count; ++i) {
                    if (strcmp(preferences_subscription_server_id(i),
                               current_id) == 0) {
                        screen->selected_server = i;
                        break;
                    }
                }
                screen->server_scroll = 0;
                keep_server_visible(screen);
                return;
            }
            case SDLK_RETURN:
            case SDLK_SPACE:
                if (count > 0) {
                    if (!preferences_set_subscription_server(
                            preferences_subscription_server_id(
                                screen->selected_server))) {
                        SDL_Log("Unable to save selected subscription server");
                    }
                }
                screen->server_mode = 0;
                return;
            default:
                return;
        }
    }
    switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            app_set_screen(tg_state() == TgStateReady ? ScreenId_Chats : ScreenId_Auth);
            break;
        case SDLK_UP:
            if (screen->selected > 0) --screen->selected;
            break;
        case SDLK_DOWN:
            if (screen->selected < 7) ++screen->selected;
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (screen->selected == 0) {
                preferences_toggle_language();
            } else if (screen->selected == 1) {
                preferences_toggle_animations();
            } else if (screen->selected == 2) {
                if (!tg_apply_subscription(
                        !preferences_subscription_enabled())) {
                    SDL_Log("Unable to apply subscription settings");
                }
            } else if (screen->selected == 3) {
                enter_server_list(screen);
            } else if (screen->selected == 7) {
                if (!tg_apply_subscription(preferences_subscription_enabled())) {
                    SDL_Log("Unable to apply the Telegram connection");
                }
            }
            break;
        default:
            break;
    }
}

static void settings_update(Screen* self) {
    (void)self;
}

static void settings_render(Screen* self, AppRenderer* renderer) {
    ScreenSettings* screen = (ScreenSettings*)self;
    if (screen->server_mode) {
        int count = preferences_subscription_server_count();
        ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, COLOR_BG);
        ui_fill(renderer, (SDL_Rect){0, 0, 640, 58}, COLOR_HEADER);
        ui_text(renderer, tr("Choose server (sorted by ping)",
                             "Выбор сервера (по пингу)"),
                22, 10, 21, COLOR_TEXT);
        if (count == 0) {
            ui_text(renderer, tr("No server list; restart the app to refresh",
                                 "Список пуст; перезапустите приложение"),
                    24, 100, 15, COLOR_MUTED);
        }
        for (int row = 0; row < 7; ++row) {
            int index = screen->server_scroll + row;
            if (index >= count) break;
            int y = 70 + row * 49;
            const char* name = preferences_subscription_server_name(index);
            char display_name[PREFERENCES_SUBSCRIPTION_SERVER_NAME_LENGTH];
            copy_text_fitting(display_name, sizeof(display_name), name, 440, 14);
            ui_round_rect(renderer, (SDL_Rect){18, y, 604, 42}, 13,
                          index == screen->selected_server ? COLOR_BLUE : COLOR_PANEL);
            ui_text(renderer, display_name, 30, y + 13, 14, COLOR_TEXT);
            int ping = preferences_subscription_server_ping(index);
            char ping_label[32];
            if (ping < 0) {
                snprintf(ping_label, sizeof(ping_label), "%s",
                         tr("Offline", "Нет связи"));
            } else {
                snprintf(ping_label, sizeof(ping_label), "%d ms", ping);
            }
            int ping_width = ui_text_width(ping_label, 13);
            ui_text(renderer, ping_label, 604 - ping_width, y + 14, 13, COLOR_MUTED);
        }
        ui_fill(renderer, (SDL_Rect){0, 438, 640, 42}, COLOR_HEADER);
        ui_text(renderer, tr("A: save server  X: ping all  B: back",
                             "A: сохранить сервер  X: пинг всех  B: назад"),
                18, 451, 13, COLOR_TEXT);
        return;
    }
    const char* labels[] = {
        tr("Language", "Язык"),
        tr("Animations", "Анимации"),
        tr("VLESS subscription", "Подписка VLESS"),
        tr("Choose server", "Выбрать сервер"),
        tr("Subscription link", "Ссылка подписки"),
        tr("Engine", "Движок"),
        tr("Traffic", "Трафик"),
        tr("Apply connection", "Применить подключение")
    };
    const char* selected_server = tr("Automatic (best ping)", "Авто (лучший пинг)");
    static char server_value[PREFERENCES_SUBSCRIPTION_SERVER_NAME_LENGTH + 24];
    const char* selected_id = preferences_subscription_selected_server();
    for (int i = 0; *selected_id &&
            i < preferences_subscription_server_count(); ++i) {
        if (strcmp(preferences_subscription_server_id(i), selected_id) == 0) {
            int ping = preferences_subscription_server_ping(i);
            snprintf(server_value, sizeof(server_value), "%s  ",
                     preferences_subscription_server_name(i));
            if (ping >= 0) {
                size_t length = strlen(server_value);
                snprintf(server_value + length, sizeof(server_value) - length,
                         "%d ms", ping);
            } else {
                size_t length = strlen(server_value);
                snprintf(server_value + length, sizeof(server_value) - length,
                         "%s", tr("(offline)", "(нет связи)"));
            }
            selected_server = server_value;
            break;
        }
    }
    const char* values[] = {
        preferences_is_russian() ? "Русский" : "English",
        preferences_animations_enabled() ? tr("On", "Включены") : tr("Off", "Выключены"),
        preferences_subscription_enabled() ? tr("Enabled", "Включена")
                                           : tr("Disabled", "Выключена"),
        selected_server,
        preferences_subscription_configured() ? tr("Installed", "Установлена")
                                             : tr("Not configured", "Не задана"),
        "sing-box",
        tr("Telegram client only", "Только клиент Telegram"),
        tr("Apply now", "Применить сейчас")
    };

    ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, COLOR_BG);
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 58}, COLOR_HEADER);
    ui_text(renderer, tr("Settings", "Настройки"), 22, 10, 24, COLOR_TEXT);

    for (int i = 0; i < 8; ++i) {
        int y = 68 + i * 44;
        ui_round_rect(renderer, (SDL_Rect){18, y, 604, 39}, 13,
                      i == screen->selected ? COLOR_BLUE : COLOR_PANEL);
        ui_text(renderer, labels[i], 30, y + 12, 14, COLOR_TEXT);
        if (i == 3) {
            char display_value[96];
            copy_text_fitting(display_value, sizeof(display_value),
                              values[i], 260, 14);
            ui_text(renderer, display_value, 350, y + 12, 14, COLOR_TEXT);
        } else {
            ui_text(renderer, values[i], 350, y + 12, 14, COLOR_TEXT);
        }
    }

    if (tg_last_error()[0]) {
        ui_text(renderer, tg_last_error(), 18, 424, 12,
                (SDL_Color){255, 204, 105, 255});
    } else {
        ui_text(renderer, tg_status(), 18, 424, 12, COLOR_MUTED);
    }
    ui_text(renderer, tr("A: change/apply  B: back",
                         "A: изменить/применить  B: назад"),
            18, 451, 12, COLOR_MUTED);
}

static void settings_destroy(Screen* self) {
    free(self);
}

ScreenSettings* screen_settings_create(void) {
    ScreenSettings* screen = calloc(1, sizeof(*screen));
    if (!screen) return NULL;
    screen->base.handle = settings_handle;
    screen->base.update = settings_update;
    screen->base.render = settings_render;
    screen->base.destroy = settings_destroy;
    return screen;
}
