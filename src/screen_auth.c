#include "screen_auth.h"
#include "app.h"
#include "preferences.h"
#include "telegram.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const SDL_Color COLOR_TEXT = {238, 241, 247, 255};
static const SDL_Color COLOR_MUTED = {156, 168, 185, 255};
static const SDL_Color COLOR_BLUE = {46, 140, 220, 255};
static const SDL_Color COLOR_PANEL = {31, 39, 51, 255};

static const char* AUTH_DIGITS[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
static const char* AUTH_ENGLISH[] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i",
    "j", "k", "l", "m", "n", "o", "p", "q", "r",
    "s", "t", "u", "v", "w", "x", "y", "z"
};
static const char* AUTH_RUSSIAN[] = {
    "а", "б", "в", "г", "д", "е", "ё", "ж", "з", "и", "й",
    "к", "л", "м", "н", "о", "п", "р", "с", "т", "у",
    "ф", "х", "ц", "ч", "ш", "щ", "ъ", "ы", "ь", "э", "ю", "я"
};
static const char* AUTH_RUSSIAN_UPPER[] = {
    "А", "Б", "В", "Г", "Д", "Е", "Ё", "Ж", "З", "И", "Й",
    "К", "Л", "М", "Н", "О", "П", "Р", "С", "Т", "У",
    "Ф", "Х", "Ц", "Ч", "Ш", "Щ", "Ъ", "Ы", "Ь", "Э", "Ю", "Я"
};
static const char* AUTH_SYMBOLS[] = {
    "+", "-", ".", "@", "_", "!", "?", "#", "$", "%", "&"
};
#define AUTH_COLUMNS 8

static int auth_letter_count(void) {
    return preferences_is_russian()
        ? (int)(sizeof(AUTH_RUSSIAN) / sizeof(AUTH_RUSSIAN[0]))
        : (int)(sizeof(AUTH_ENGLISH) / sizeof(AUTH_ENGLISH[0]));
}

static int auth_key_count(void) {
    return 10 + auth_letter_count() +
           (int)(sizeof(AUTH_SYMBOLS) / sizeof(AUTH_SYMBOLS[0])) + 5;
}

static const char* auth_key_label(int index) {
    int letters = auth_letter_count();
    if (index < 10) return AUTH_DIGITS[index];
    if (index < 10 + letters) {
        int letter = index - 10;
        if (preferences_is_russian()) {
            return AUTH_RUSSIAN[letter];
        }
        return AUTH_ENGLISH[letter];
    }
    index -= 10 + letters;
    if (index < (int)(sizeof(AUTH_SYMBOLS) / sizeof(AUTH_SYMBOLS[0]))) {
        return AUTH_SYMBOLS[index];
    }
    static const char* controls[] = {"SPACE", "DEL", "CASE", "OK", "QR"};
    return controls[index - (int)(sizeof(AUTH_SYMBOLS) / sizeof(AUTH_SYMBOLS[0]))];
}

static int auth_is_collecting(void) {
    TgState state = tg_state();
    return state == TgStatePhone || state == TgStateCode || state == TgStatePassword;
}

static void remove_last_character(ScreenAuth* screen) {
    while (screen->input_length > 0 &&
           ((unsigned char)screen->input[screen->input_length - 1] & 0xc0) == 0x80) {
        screen->input[--screen->input_length] = '\0';
    }
    if (screen->input_length > 0) screen->input[--screen->input_length] = '\0';
}

static void append_character(ScreenAuth* screen, char value) {
    if (screen->input_length + 1 < (int)sizeof(screen->input)) {
        screen->input[screen->input_length++] = value;
        screen->input[screen->input_length] = '\0';
    }
}

static void append_auth_text(ScreenAuth* screen, const char* value) {
    size_t length = strlen(value);
    if (screen->input_length + (int)length >= (int)sizeof(screen->input)) return;
    memcpy(screen->input + screen->input_length, value, length);
    screen->input_length += (int)length;
    screen->input[screen->input_length] = '\0';
}

static void submit_auth(ScreenAuth* screen) {
    if (tg_submit_auth(screen->input)) {
        screen->input_length = 0;
        screen->input[0] = '\0';
    }
}

static void activate_auth_key(ScreenAuth* screen) {
    int letters = auth_letter_count();
    int symbols = 10 + letters;
    int controls = symbols + (int)(sizeof(AUTH_SYMBOLS) / sizeof(AUTH_SYMBOLS[0]));
    if (screen->selected_key < 10) {
        append_character(screen, (char)('0' + screen->selected_key));
    } else if (screen->selected_key < symbols) {
        int letter = screen->selected_key - 10;
        if (preferences_is_russian()) {
            append_auth_text(screen, screen->uppercase
                ? AUTH_RUSSIAN_UPPER[letter] : AUTH_RUSSIAN[letter]);
        } else {
            char value = AUTH_ENGLISH[letter][0];
            append_character(screen, screen->uppercase
                ? (char)(value - 'a' + 'A') : value);
        }
    } else if (screen->selected_key < controls) {
        append_auth_text(screen, AUTH_SYMBOLS[screen->selected_key - symbols]);
    } else if (screen->selected_key == controls) {
        append_character(screen, ' ');
    } else if (screen->selected_key == controls + 1) {
        remove_last_character(screen);
    } else if (screen->selected_key == controls + 2) {
        screen->uppercase = !screen->uppercase;
    } else if (screen->selected_key == controls + 3) {
        submit_auth(screen);
    } else if (screen->selected_key == controls + 4) {
        screen->input_length = 0;
        screen->input[0] = '\0';
        tg_request_qr_auth();
    }
}

static void auth_handle(Screen* self, const SDL_Event* event) {
    ScreenAuth* screen = (ScreenAuth*)self;
    if (event->type != SDL_KEYDOWN) return;

    if (event->key.keysym.sym == SDLK_t) {
        app_set_screen(ScreenId_Settings);
        return;
    }

    if (auth_is_collecting()) {
        SDL_Keycode key = event->key.keysym.sym;
        if (key >= SDLK_a && key <= SDLK_z) {
            char value = (char)('a' + key - SDLK_a);
            append_character(screen, screen->uppercase
                ? (char)(value - 'a' + 'A') : value);
            return;
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            append_character(screen, (char)('0' + key - SDLK_0));
            return;
        }
        switch (key) {
            case SDLK_UP:
                if (screen->selected_key >= AUTH_COLUMNS) {
                    screen->selected_key -= AUTH_COLUMNS;
                }
                break;
            case SDLK_DOWN:
                if (screen->selected_key + AUTH_COLUMNS < auth_key_count()) {
                    screen->selected_key += AUTH_COLUMNS;
                }
                break;
            case SDLK_LEFT:
                if (screen->selected_key > 0) --screen->selected_key;
                break;
            case SDLK_RIGHT:
                if (screen->selected_key + 1 < auth_key_count()) ++screen->selected_key;
                break;
            case SDLK_RETURN:
            case SDLK_s:
                submit_auth(screen);
                break;
            case SDLK_SPACE:
                activate_auth_key(screen);
                break;
            case SDLK_BACKSPACE:
            case SDLK_x:
                remove_last_character(screen);
                break;
            case SDLK_y:
                screen->uppercase = !screen->uppercase;
                break;
            default:
                break;
        }
        return;
    }

    switch (event->key.keysym.sym) {
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (tg_state() == TgStateReady) app_set_screen(ScreenId_Chats);
            break;
        default:
            break;
    }
}

static void auth_update(Screen* self) {
    (void)self;
    if (tg_state() == TgStateReady) app_set_screen(ScreenId_Chats);
}

static void auth_render(Screen* self, AppRenderer* renderer) {
    (void)self;
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, (SDL_Color){20, 25, 33, 255});
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 54}, (SDL_Color){29, 39, 53, 255});
    ui_text(renderer, "Telegram", 22, 12, 24, COLOR_TEXT);
    ui_text(renderer, tg_status(), 22, 70, 18,
            tg_state() == TgStateReady ? COLOR_BLUE : COLOR_MUTED);

    if (auth_is_collecting()) {
        ui_text(renderer, tg_prompt(), 22, 108, 14, COLOR_MUTED);
        ui_round_rect(renderer, (SDL_Rect){18, 132, 604, 38}, 10, COLOR_PANEL);
        char masked[sizeof(((ScreenAuth*)self)->input)];
        const ScreenAuth* screen = (const ScreenAuth*)self;
        if (tg_state() == TgStatePassword) {
            int length = screen->input_length;
            if (length >= (int)sizeof(masked)) length = (int)sizeof(masked) - 1;
            memset(masked, '*', (size_t)length);
            masked[length] = '\0';
        } else {
            snprintf(masked, sizeof(masked), "%s", screen->input);
        }
        ui_text(renderer, masked, 30, 142, 18, COLOR_TEXT);
        for (int i = 0; i < auth_key_count(); ++i) {
            int row = i / AUTH_COLUMNS;
            int column = i % AUTH_COLUMNS;
            int x = 7 + column * 79;
            int y = 188 + row * 28;
            const char* label = auth_key_label(i);
            int letters = auth_letter_count();
            if (i >= 10 && i < 10 + letters && screen->uppercase) {
                int letter = i - 10;
                if (preferences_is_russian()) label = AUTH_RUSSIAN_UPPER[letter];
                else {
                    static char uppercase_label[2];
                    uppercase_label[0] = (char)(AUTH_ENGLISH[letter][0] - 'a' + 'A');
                    uppercase_label[1] = '\0';
                    label = uppercase_label;
                }
            }
            ui_round_rect(renderer, (SDL_Rect){x, y, 73, 24}, 6,
                          i == screen->selected_key ? COLOR_BLUE : COLOR_PANEL);
            ui_text(renderer, label, x + 6, y + 4, 13, COLOR_TEXT);
        }
        if (tg_last_error()[0]) {
            ui_text(renderer, tg_last_error(), 16, 414, 14,
                    (SDL_Color){255, 204, 105, 255});
        }
        ui_text(renderer, tr("D-pad: select  A: type  START: submit",
                             "D-pad: выбор  A: ввод  START: далее"),
                16, 438, 13, COLOR_MUTED);
    } else if (tg_state() == TgStateQr) {
        unsigned char matrix[37][37];
        ui_round_rect(renderer, (SDL_Rect){18, 105, 604, 318}, 14, COLOR_PANEL);
        ui_text(renderer, tr("Scan with Telegram on your phone",
                             "Отсканируйте в приложении Telegram"),
                28, 113, 16, COLOR_TEXT);
        if (tg_qr_matrix(matrix)) {
            const int module = 5;
            const int quiet = 4;
            const int side = (37 + quiet * 2) * module;
            const int origin_x = (640 - side) / 2;
            const int origin_y = 145;
            ui_fill(renderer, (SDL_Rect){origin_x, origin_y, side, side},
                    (SDL_Color){255, 255, 255, 255});
            for (int y = 0; y < 37; ++y) {
                for (int x = 0; x < 37; ++x) {
                    if (!matrix[y][x]) continue;
                    ui_fill(renderer,
                            (SDL_Rect){origin_x + (x + quiet) * module,
                                       origin_y + (y + quiet) * module, module, module},
                            (SDL_Color){12, 17, 24, 255});
                }
            }
        } else {
            ui_text(renderer, tr("QR link is not available or is too long",
                                 "Ссылка QR недоступна или слишком длинная"),
                    44, 245, 14, (SDL_Color){255, 204, 105, 255});
        }
        ui_text(renderer, tg_status(), 28, 391, 13, COLOR_MUTED);
        ui_text(renderer, tr("Use Telegram: Settings > Devices > Link Desktop Device",
                             "Telegram: Настройки > Устройства > Подключить устройство"),
                24, 432, 13, COLOR_MUTED);
    } else if (tg_state() == TgStateInitializing) {
        ui_round_rect(renderer, (SDL_Rect){18, 170, 604, 130}, 12, COLOR_PANEL);
        ui_text(renderer, tr("Connecting", "Подключение"), 34, 188, 20, COLOR_TEXT);
        ui_text(renderer, tg_status(), 34, 225, 16, COLOR_MUTED);
        if (tg_last_error()[0]) {
            ui_text(renderer, tg_last_error(), 34, 265, 13,
                    (SDL_Color){255, 204, 105, 255});
        }
    } else {
        ui_round_rect(renderer, (SDL_Rect){18, 122, 604, 82}, 12, COLOR_PANEL);
        ui_text(renderer, tr("TDLib-powered Telegram client", "Клиент Telegram на TDLib"),
                34, 136, 18, COLOR_TEXT);
        ui_text(renderer, tr("Sign in with your Telegram account.", "Войдите в аккаунт Telegram."),
                34, 166, 14, COLOR_MUTED);
        ui_round_rect(renderer, (SDL_Rect){18, 220, 604, 100}, 12, COLOR_PANEL);
        ui_text(renderer, tr("Setup required", "Требуется настройка"), 34, 234, 18, COLOR_TEXT);
        ui_text(renderer, tr("Install lib/libtdjson.so and telegram.conf.",
                             "Установите lib/libtdjson.so и создайте telegram.conf."),
                34, 264, 14, COLOR_MUTED);
        ui_text(renderer, "telegram.conf: api_id=...  api_hash=...", 34, 290, 14, COLOR_MUTED);
        if (tg_last_error()[0]) {
            ui_text(renderer, tg_last_error(), 18, 344, 14, (SDL_Color){255, 204, 105, 255});
        }
    }
}

static void auth_destroy(Screen* self) {
    free(self);
}

ScreenAuth* screen_auth_create(void) {
    ScreenAuth* screen = calloc(1, sizeof(*screen));
    if (!screen) return NULL;

    screen->base.handle = auth_handle;
    screen->base.update = auth_update;
    screen->base.render = auth_render;
    screen->base.destroy = auth_destroy;
    return screen;
}
