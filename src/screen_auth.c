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
static const char* const AUTH_ENGLISH_ROWS[3][10] = {
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", NULL},
    {"z", "x", "c", "v", "b", "n", "m", NULL, NULL, NULL},
};
static const int AUTH_ENGLISH_ROW_COUNTS[3] = {10, 9, 7};
static const char* const AUTH_RUSSIAN_ROWS[3][12] = {
    {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ъ"},
    {"ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э", NULL},
    {"ё", "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", NULL, NULL},
};
static const char* const AUTH_RUSSIAN_UPPER_ROWS[3][12] = {
    {"Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х", "Ъ"},
    {"Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Э", NULL},
    {"Ё", "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", NULL, NULL},
};
static const int AUTH_RUSSIAN_ROW_COUNTS[3] = {12, 11, 10};
static const char* AUTH_SYMBOLS[] = {
    "+", "-", ".", "@", "_", "!", "?", "#", "$", "%", "&"
};
#define AUTH_DIGIT_COUNT 10
#define AUTH_SYMBOL_COUNT 11
#define AUTH_SERVICE_COUNT 6
#define AUTH_KEYBOARD_ROWS 6

static int auth_letter_row_count(const ScreenAuth* screen, int row) {
    return screen->russian_keyboard
        ? AUTH_RUSSIAN_ROW_COUNTS[row] : AUTH_ENGLISH_ROW_COUNTS[row];
}

static int auth_letter_count(const ScreenAuth* screen) {
    int count = 0;
    for (int row = 0; row < 3; ++row) count += auth_letter_row_count(screen, row);
    return count;
}

static int auth_symbol_start(const ScreenAuth* screen) {
    return AUTH_DIGIT_COUNT + auth_letter_count(screen);
}

static int auth_service_start(const ScreenAuth* screen) {
    return auth_symbol_start(screen) + AUTH_SYMBOL_COUNT;
}

static int auth_key_count(const ScreenAuth* screen) {
    return auth_service_start(screen) + AUTH_SERVICE_COUNT;
}

static int auth_row_count(const ScreenAuth* screen, int row) {
    if (row == 0) return AUTH_DIGIT_COUNT;
    if (row >= 1 && row <= 3) return auth_letter_row_count(screen, row - 1);
    if (row == 4) return AUTH_SYMBOL_COUNT;
    if (row == 5) return AUTH_SERVICE_COUNT;
    return 0;
}

static int auth_key_position(const ScreenAuth* screen, int index,
                             int* row, int* column) {
    if (index < 0 || index >= auth_key_count(screen)) return 0;
    if (index < AUTH_DIGIT_COUNT) {
        *row = 0;
        *column = index;
        return 1;
    }
    int letters_end = auth_symbol_start(screen);
    if (index < letters_end) {
        int remaining = index - AUTH_DIGIT_COUNT;
        for (int letter_row = 0; letter_row < 3; ++letter_row) {
            int count = auth_letter_row_count(screen, letter_row);
            if (remaining < count) {
                *row = letter_row + 1;
                *column = remaining;
                return 1;
            }
            remaining -= count;
        }
        return 0;
    }
    if (index < auth_service_start(screen)) {
        *row = 4;
        *column = index - letters_end;
        return 1;
    }
    *row = 5;
    *column = index - auth_service_start(screen);
    return 1;
}

static int auth_key_index_at(const ScreenAuth* screen, int row, int column) {
    if (row == 0) return column;
    if (row >= 1 && row <= 3) {
        int index = AUTH_DIGIT_COUNT;
        for (int letter_row = 0; letter_row < row - 1; ++letter_row) {
            index += auth_letter_row_count(screen, letter_row);
        }
        return index + column;
    }
    if (row == 4) return auth_symbol_start(screen) + column;
    return auth_service_start(screen) + column;
}

static void auth_move_selection(ScreenAuth* screen, int direction) {
    int row, column;
    if (!auth_key_position(screen, screen->selected_key, &row, &column)) {
        screen->selected_key = 0;
        return;
    }
    if (direction == 0 && column > 0) --column;
    else if (direction == 1 && column + 1 < auth_row_count(screen, row)) ++column;
    else if (direction == 2 && row > 0) {
        --row;
        if (column >= auth_row_count(screen, row)) column = auth_row_count(screen, row) - 1;
    } else if (direction == 3 && row + 1 < AUTH_KEYBOARD_ROWS) {
        ++row;
        if (column >= auth_row_count(screen, row)) column = auth_row_count(screen, row) - 1;
    } else return;
    screen->selected_key = auth_key_index_at(screen, row, column);
}

static const char* auth_key_label(const ScreenAuth* screen, int index,
                                 char* buffer, size_t capacity) {
    if (index < AUTH_DIGIT_COUNT) return AUTH_DIGITS[index];
    int letters_end = auth_symbol_start(screen);
    if (index < letters_end) {
        int row, column;
        if (!auth_key_position(screen, index, &row, &column)) return "";
        if (screen->russian_keyboard) {
            return screen->uppercase ? AUTH_RUSSIAN_UPPER_ROWS[row - 1][column]
                                     : AUTH_RUSSIAN_ROWS[row - 1][column];
        }
        const char* label = AUTH_ENGLISH_ROWS[row - 1][column];
        if (screen->uppercase) {
            snprintf(buffer, capacity, "%c", label[0] - 'a' + 'A');
            return buffer;
        }
        return label;
    }
    if (index < auth_service_start(screen)) {
        return AUTH_SYMBOLS[index - letters_end];
    }
    static const char* const ENGLISH_SERVICE[] = {
        "SPACE", "DEL", "CASE", "РУС", "OK", "QR"
    };
    static const char* const RUSSIAN_SERVICE[] = {
        "ПРОБЕЛ", "УДЛ", "РЕГ", "EN", "OK", "QR"
    };
    int service = index - auth_service_start(screen);
    return screen->russian_keyboard ? RUSSIAN_SERVICE[service]
                                    : ENGLISH_SERVICE[service];
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
    if (screen->selected_key < AUTH_DIGIT_COUNT) {
        append_character(screen, (char)('0' + screen->selected_key));
    } else if (screen->selected_key < auth_symbol_start(screen)) {
        char label[8];
        append_auth_text(screen, auth_key_label(screen, screen->selected_key,
                                                label, sizeof(label)));
    } else if (screen->selected_key < auth_service_start(screen)) {
        append_auth_text(screen, AUTH_SYMBOLS[
            screen->selected_key - auth_symbol_start(screen)]);
    } else if (screen->selected_key == auth_service_start(screen)) {
        append_character(screen, ' ');
    } else if (screen->selected_key == auth_service_start(screen) + 1) {
        remove_last_character(screen);
    } else if (screen->selected_key == auth_service_start(screen) + 2) {
        screen->uppercase = !screen->uppercase;
    } else if (screen->selected_key == auth_service_start(screen) + 3) {
        screen->russian_keyboard = !screen->russian_keyboard;
        screen->selected_key = auth_service_start(screen) + 3;
    } else if (screen->selected_key == auth_service_start(screen) + 4) {
        submit_auth(screen);
    } else if (screen->selected_key == auth_service_start(screen) + 5) {
        screen->input_length = 0;
        screen->input[0] = '\0';
        tg_request_qr_auth();
    }
}

static void auth_handle(Screen* self, const SDL_Event* event) {
    ScreenAuth* screen = (ScreenAuth*)self;
    if (event->type == SDL_TEXTINPUT) {
        if (auth_is_collecting()) append_auth_text(screen, event->text.text);
        return;
    }
    if (event->type != SDL_KEYDOWN || event->key.repeat) return;

    if (event->key.keysym.sym == SDLK_t &&
        event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
        app_set_screen(ScreenId_Settings);
        return;
    }

    if (auth_is_collecting()) {
        SDL_Keycode key = event->key.keysym.sym;
        switch (key) {
            case SDLK_UP: auth_move_selection(screen, 2); break;
            case SDLK_DOWN: auth_move_selection(screen, 3); break;
            case SDLK_LEFT: auth_move_selection(screen, 0); break;
            case SDLK_RIGHT: auth_move_selection(screen, 1); break;
            case SDLK_RETURN:
                submit_auth(screen);
                break;
            case SDLK_SPACE:
                if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                    activate_auth_key(screen);
                }
                break;
            case SDLK_s:
                if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                    submit_auth(screen);
                }
                break;
            case SDLK_BACKSPACE:
                remove_last_character(screen);
                break;
            case SDLK_x:
                if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                    remove_last_character(screen);
                }
                break;
            case SDLK_y:
                if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                    screen->uppercase = !screen->uppercase;
                }
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
    ScreenAuth* screen = (ScreenAuth*)self;
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, (SDL_Color){20, 25, 33, 255});
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 54}, (SDL_Color){29, 39, 53, 255});
    ui_text(renderer, "Telegram", 22, 12, 24, COLOR_TEXT);
    ui_text(renderer, tg_status(), 22, 70, 18,
            tg_state() == TgStateReady ? COLOR_BLUE : COLOR_MUTED);

    if (auth_is_collecting()) {
        ui_text(renderer, tg_prompt(), 22, 108, 14, COLOR_MUTED);
        ui_round_rect(renderer, (SDL_Rect){18, 132, 604, 38}, 10, COLOR_PANEL);
        char masked[sizeof(((ScreenAuth*)self)->input)];
        if (tg_state() == TgStatePassword) {
            int length = screen->input_length;
            if (length >= (int)sizeof(masked)) length = (int)sizeof(masked) - 1;
            memset(masked, '*', (size_t)length);
            masked[length] = '\0';
        } else {
            snprintf(masked, sizeof(masked), "%s", screen->input);
        }
        ui_text(renderer, masked, 30, 142, 18, COLOR_TEXT);
        if (screen->selected_key >= auth_key_count(screen)) screen->selected_key = 0;
        for (int row = 0; row < AUTH_KEYBOARD_ROWS; ++row) {
            int count = auth_row_count(screen, row);
            int gap = 4;
            int key_width = (600 - gap * (count - 1)) / count;
            int max_key_width = row == 5 ? 92 : 54;
            if (key_width > max_key_width) key_width = max_key_width;
            int total_width = count * key_width + gap * (count - 1);
            int x = (640 - total_width) / 2;
            int y = 188 + row * 29;
            int index = auth_key_index_at(screen, row, 0);
            for (int column = 0; column < count; ++column) {
                int key_index = index + column;
                char label_buffer[8];
                const char* label = auth_key_label(screen, key_index,
                                                   label_buffer,
                                                   sizeof(label_buffer));
                ui_round_rect(renderer,
                              (SDL_Rect){x, y, key_width, 24}, 6,
                              key_index == screen->selected_key
                                  ? COLOR_BLUE : COLOR_PANEL);
                int text_width = ui_text_width(label, 13);
                int text_x = x + (key_width - text_width) / 2;
                if (text_x < x + 2) text_x = x + 2;
                ui_text(renderer, label, text_x, y + 4, 13, COLOR_TEXT);
                x += key_width + gap;
            }
        }
        if (tg_last_error()[0]) {
            ui_text(renderer, tg_last_error(), 16, 414, 14,
                    (SDL_Color){255, 204, 105, 255});
        }
        ui_text(renderer, tr("D-pad: select  A: type  RU/EN key: language  START: submit",
                             "D-pad: выбор  A: ввод  RU/EN: язык  START: далее"),
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
    } else if (tg_state() == TgStateError) {
        ui_round_rect(renderer, (SDL_Rect){18, 122, 604, 184}, 12, COLOR_PANEL);
        ui_text(renderer, tr("Connection error", "Ошибка подключения"),
                34, 140, 20, COLOR_TEXT);
        ui_text(renderer, tg_status(), 34, 176, 15, COLOR_MUTED);
        if (tg_last_error()[0]) {
            ui_text(renderer, tg_last_error(), 34, 212, 13,
                    (SDL_Color){255, 204, 105, 255});
        }
        ui_text(renderer, tr("T: settings", "T: настройки"),
                24, 438, 13, COLOR_MUTED);
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
    screen->russian_keyboard = 1;
    return screen;
}
