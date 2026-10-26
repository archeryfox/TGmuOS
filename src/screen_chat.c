#include "screen_chat.h"
#include "app.h"
#include "preferences.h"
#include "telegram.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* ENGLISH_KEYS[] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i",
    "j", "k", "l", "m", "n", "o", "p", "q", "r",
    "s", "t", "u", "v", "w", "x", "y", "z"
};
static const char* RUSSIAN_KEYS[] = {
    "а", "б", "в", "г", "д", "е", "ё", "ж", "з", "и", "й",
    "к", "л", "м", "н", "о", "п", "р", "с", "т", "у",
    "ф", "х", "ц", "ч", "ш", "щ", "ъ", "ы", "ь", "э", "ю", "я"
};
static const char* RUSSIAN_UPPER_KEYS[] = {
    "А", "Б", "В", "Г", "Д", "Е", "Ё", "Ж", "З", "И", "Й",
    "К", "Л", "М", "Н", "О", "П", "Р", "С", "Т", "У",
    "Ф", "Х", "Ц", "Ч", "Ш", "Щ", "Ъ", "Ы", "Ь", "Э", "Ю", "Я"
};
static const char* SYMBOL_KEYS[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    ".", ",", "!", "?", "@", "#", "-", "_", "(", ")", "[",
    "]", "'", "\"", "/", ":", ";", "+", "=", "*", "&", "%", "$"
};
#define KEY_COLUMNS 7

static const SDL_Color COLOR_BG = {20, 25, 33, 255};
static const SDL_Color COLOR_HEADER = {29, 39, 53, 255};
static const SDL_Color COLOR_PANEL = {31, 39, 51, 255};
static const SDL_Color COLOR_SELECTED = {44, 74, 105, 255};
static const SDL_Color COLOR_BLUE = {46, 140, 220, 255};
static const SDL_Color COLOR_TEXT = {238, 241, 247, 255};
static const SDL_Color COLOR_MUTED = {156, 168, 185, 255};

static void send_draft(ScreenChat* screen) {
    if (screen->draft_length == 0) return;
    if (tg_send_message(screen->draft)) {
        screen->draft_length = 0;
        screen->draft[0] = '\0';
    }
}

static int character_key_count(void) {
    return preferences_is_russian()
        ? (int)(sizeof(RUSSIAN_KEYS) / sizeof(RUSSIAN_KEYS[0]))
        : (int)(sizeof(ENGLISH_KEYS) / sizeof(ENGLISH_KEYS[0]));
}

static int key_count(void) {
    return character_key_count() + 5;
}

static void append_text(ScreenChat* screen, const char* value) {
    size_t length = strlen(value);
    if (screen->draft_length + (int)length >= CHAT_MESSAGE_LENGTH) return;
    memcpy(screen->draft + screen->draft_length, value, length);
    screen->draft_length += (int)length;
    screen->draft[screen->draft_length] = '\0';
}

static void delete_last_character(ScreenChat* screen) {
    while (screen->draft_length > 0 &&
           ((unsigned char)screen->draft[screen->draft_length - 1] & 0xc0) == 0x80) {
        screen->draft[--screen->draft_length] = '\0';
    }
    if (screen->draft_length > 0) {
        screen->draft[--screen->draft_length] = '\0';
    }
}

static void activate_key(ScreenChat* screen) {
    int letters = character_key_count();
    if (screen->selected_key < letters) {
        if (screen->symbols) {
            append_text(screen, SYMBOL_KEYS[screen->selected_key]);
        } else if (preferences_is_russian()) {
            append_text(screen, screen->uppercase
                ? RUSSIAN_UPPER_KEYS[screen->selected_key]
                : RUSSIAN_KEYS[screen->selected_key]);
        } else {
            char value = (char)('a' + screen->selected_key);
            if (screen->uppercase) {
                value = (char)(value - 'a' + 'A');
            }
            append_text(screen, (char[]){value, '\0'});
        }
    } else if (screen->selected_key == letters) {
        append_text(screen, " ");
    } else if (screen->selected_key == letters + 1) {
        delete_last_character(screen);
    } else if (screen->selected_key == letters + 2) {
        screen->symbols = !screen->symbols;
    } else if (screen->selected_key == letters + 3) {
        screen->keyboard_visible = 0;
    } else if (screen->selected_key == letters + 4) {
        send_draft(screen);
    }
}

static void chat_handle(Screen* self, const SDL_Event* event) {
    ScreenChat* screen = (ScreenChat*)self;
    if (event->type == SDL_TEXTINPUT) {
        append_text(screen, event->text.text);
        if (screen->keyboard_visible) {
            screen->keyboard_visible = 0;
            int count = tg_message_count();
            screen->scroll_target = count > 6 ? (count - 6) * 48 : 0;
        }
        return;
    }
    if (event->type == SDL_MOUSEWHEEL) {
        screen->scroll_target -= event->wheel.y * 48;
        if (screen->scroll_target < 0) screen->scroll_target = 0;
        tg_load_older_messages();
        return;
    }
    if (event->type != SDL_KEYDOWN || event->key.repeat) return;

    switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
            app_set_screen(ScreenId_Chats);
            break;
        case SDLK_BACKSPACE:
            if (event->key.keysym.scancode != SDL_SCANCODE_UNKNOWN &&
                screen->draft_length > 0) {
                delete_last_character(screen);
            } else {
                app_set_screen(ScreenId_Chats);
            }
            break;
        case SDLK_UP:
            if (!screen->keyboard_visible) {
                screen->scroll_target -= 42;
                if (screen->scroll_target < 0) screen->scroll_target = 0;
                tg_load_older_messages();
                break;
            }
            if (screen->selected_key >= KEY_COLUMNS) screen->selected_key -= KEY_COLUMNS;
            break;
        case SDLK_DOWN:
            if (!screen->keyboard_visible) {
                screen->scroll_target += 42;
                int max_scroll = tg_message_count() > 6
                    ? (tg_message_count() - 6) * 48 : 0;
                if (screen->scroll_target > max_scroll) screen->scroll_target = max_scroll;
                break;
            }
            if (screen->selected_key + KEY_COLUMNS < key_count()) {
                screen->selected_key += KEY_COLUMNS;
            }
            break;
        case SDLK_PAGEUP:
            screen->scroll_target -= 140;
            if (screen->scroll_target < 0) screen->scroll_target = 0;
            tg_load_older_messages();
            break;
        case SDLK_PAGEDOWN:
            screen->scroll_target += 140;
            if (!screen->keyboard_visible) {
                int max_scroll = tg_message_count() > 6
                    ? (tg_message_count() - 6) * 48 : 0;
                if (screen->scroll_target > max_scroll) screen->scroll_target = max_scroll;
            }
            break;
        case SDLK_LEFT:
            if (screen->selected_key > 0) --screen->selected_key;
            break;
        case SDLK_RIGHT:
            if (screen->selected_key < key_count() - 1) ++screen->selected_key;
            break;
        case SDLK_RETURN:
            if (event->key.keysym.mod & KMOD_SHIFT) {
                append_text(screen, "\n");
            } else {
                send_draft(screen);
            }
            break;
        case SDLK_SPACE:
            if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN &&
                screen->keyboard_visible) activate_key(screen);
            break;
        case SDLK_x:
            if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                screen->keyboard_visible = !screen->keyboard_visible;
            }
            {
                int visible = screen->keyboard_visible ? 3 : 6;
                int count = tg_message_count();
                screen->scroll_target = count > visible
                    ? (count - visible) * 48 : 0;
            }
            break;
        case SDLK_s:
            if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                send_draft(screen);
            }
            break;
        case SDLK_y:
            if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                if (screen->symbols) screen->symbols = 0;
                else screen->uppercase = !screen->uppercase;
            }
            break;
        case SDLK_t:
            if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                app_set_screen(ScreenId_Settings);
            }
            break;
        default:
            break;
    }
}

static void chat_update(Screen* self) {
    ScreenChat* screen = (ScreenChat*)self;
    int64_t chat_id = tg_selected_chat_id();
    if (chat_id != screen->current_chat_id) {
        screen->current_chat_id = chat_id;
        screen->last_message_count = 0;
        screen->scroll_target = 0;
        screen->scroll_position = 0.0f;
    }
    uint32_t now = SDL_GetTicks();
    if (screen->last_update == 0) screen->last_update = now;
    uint32_t elapsed = now - screen->last_update;
    screen->last_update = now;
    int message_count = tg_message_count();
    if (message_count != screen->last_message_count) {
        int previous_count = screen->last_message_count;
        screen->last_message_count = message_count;
        if (previous_count == 0) {
            int visible = screen->keyboard_visible ? 3 : 6;
            screen->scroll_target = message_count > visible
                ? (message_count - visible) * 48 : 0;
        } else if (message_count > previous_count && screen->scroll_target == 0) {
            screen->scroll_target = (message_count - previous_count) * 48;
        } else if (message_count > previous_count) {
            int visible = screen->keyboard_visible ? 3 : 6;
            screen->scroll_target = message_count > visible
                ? (message_count - visible) * 48 : 0;
        }
    }
    int viewport = screen->keyboard_visible ? 3 : 6;
    int max_scroll = message_count > viewport ? (message_count - viewport) * 48 : 0;
    if (screen->scroll_target > max_scroll) screen->scroll_target = max_scroll;
    if (preferences_animations_enabled()) {
        float step = (float)elapsed * 0.8f;
        float difference = (float)screen->scroll_target - screen->scroll_position;
        if (difference > step) screen->scroll_position += step;
        else if (difference < -step) screen->scroll_position -= step;
        else screen->scroll_position = (float)screen->scroll_target;
    } else {
        screen->scroll_position = (float)screen->scroll_target;
    }
}

static void chat_render(Screen* self, AppRenderer* renderer) {
    ScreenChat* screen = (ScreenChat*)self;
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, COLOR_BG);
    ui_round_rect(renderer, (SDL_Rect){0, 0, 640, 54}, 0, COLOR_HEADER);
    ui_text(renderer, tg_selected_chat_title(), 18, 7, 18, COLOR_TEXT);
    ui_text(renderer, tr("TELEGRAM", "ТЕЛЕГРАМ"), 18, 31, 14, COLOR_MUTED);

    int message_count = tg_message_count();
    int first_message = 0;
    int viewport_bottom = screen->keyboard_visible ? 205 : 380;
    for (int i = first_message; i < message_count; ++i) {
        const TgMessage* message = tg_message_at(i);
        if (!message) continue;
        int y = 72 + i * 48 - (int)screen->scroll_position;
        if (y + 40 < 62 || y > viewport_bottom) continue;
        int width = ui_text_width(message->text, 14) + 22;
        if (width > 420) width = 420;
        int x = message->outgoing ? 620 - width : 18;
        ui_round_rect(renderer, (SDL_Rect){x, y, width, 38}, 11,
                      message->outgoing ? COLOR_SELECTED : COLOR_PANEL);
        ui_text(renderer, message->text, x + 10, y + 9, 14, COLOR_TEXT);
    }
    if (message_count == 0) {
        ui_text(renderer, tr("No messages yet", "Сообщений пока нет"), 22, 100, 14, COLOR_MUTED);
    }
    if (tg_last_error()[0]) {
        ui_text(renderer, tg_last_error(), 18, 207, 12, (SDL_Color){255, 204, 105, 255});
    }

    int input_y = screen->keyboard_visible ? 218 : 396;
    ui_round_rect(renderer, (SDL_Rect){12, input_y, 616, 38}, 10, COLOR_PANEL);
    ui_text(renderer, screen->draft_length ? screen->draft :
            tr("Write a message", "Введите сообщение"),
            22, input_y + 10, 14, screen->draft_length ? COLOR_TEXT : COLOR_MUTED);

    if (screen->keyboard_visible) {
        int letters = character_key_count();
        int count = key_count();
        for (int i = 0; i < count; ++i) {
            int row = i / KEY_COLUMNS;
            int column = i % KEY_COLUMNS;
            int x = 13 + column * 88;
            int y = 264 + row * 28;
            const char* label;
            if (i < letters) {
                label = preferences_is_russian() ? RUSSIAN_KEYS[i] : ENGLISH_KEYS[i];
                if (preferences_is_russian() && screen->uppercase) {
                    label = RUSSIAN_UPPER_KEYS[i];
                }
                if (screen->symbols) label = SYMBOL_KEYS[i];
            } else if (i == letters) label = "SPACE";
            else if (i == letters + 1) label = "DEL";
            else if (i == letters + 2) label = screen->symbols
                ? tr("ABC", "АБВ") : "123";
            else if (i == letters + 3) label = tr("HIDE", "СКРЫТЬ");
            else label = tr("SEND", "ОТПРАВИТЬ");
            ui_round_rect(renderer, (SDL_Rect){x, y, 82, 24}, 6,
                          i == screen->selected_key ? COLOR_BLUE : COLOR_PANEL);
            ui_text(renderer, label, x + 7, y + 4, 13, COLOR_TEXT);
        }
    }

    ui_fill(renderer, (SDL_Rect){0, 438, 640, 42}, COLOR_HEADER);
    ui_text(renderer, screen->keyboard_visible
            ? tr("A: TYPE  X: HIDE", "A: ВВОД  X: СКРЫТЬ")
            : tr("X: SHOW KEYBOARD", "X: ПОКАЗАТЬ КЛАВИАТУРУ"),
            14, 451, 13, COLOR_TEXT);
    ui_text(renderer, tr("START: SEND", "START: ОТПРАВИТЬ"), 250, 451, 13, COLOR_TEXT);
    ui_text(renderer, tr("USB: TYPE", "USB: ВВОД"), 425, 451, 13, COLOR_TEXT);
    ui_text(renderer, tr("B: BACK", "B: НАЗАД"), 524, 451, 13, COLOR_MUTED);
}

static void chat_destroy(Screen* self) {
    free(self);
}

ScreenChat* screen_chat_create(void) {
    ScreenChat* screen = calloc(1, sizeof(*screen));
    if (!screen) return NULL;

    screen->base.handle = chat_handle;
    screen->base.update = chat_update;
    screen->base.render = chat_render;
    screen->base.destroy = chat_destroy;
    screen->selected_key = 0;
    screen->uppercase = 0;
    screen->symbols = 0;
    screen->keyboard_visible = 1;
    return screen;
}
