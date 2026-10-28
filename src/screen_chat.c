#include "screen_chat.h"
#include "app.h"
#include "images.h"
#include "preferences.h"
#include "screen_profile.h"
#include "telegram.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keyboard geometry: the panel is anchored to the bottom of the screen so
 * it no longer floats over the message area. */
#define KEYBOARD_KEY_HEIGHT 22
#define KEYBOARD_ROW_STEP 24
#define KEYBOARD_TOP 336
#define INPUT_FIELD_HEIGHT 34

/* QWERTY rows; letter keys are smaller than the service keys. */
static const char* const ENGLISH_ROWS[3][10] = {
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l"},
    {"z", "x", "c", "v", "b", "n", "m"},
};
static const int ENGLISH_ROW_COUNTS[3] = {10, 9, 7};
static const char* const RUSSIAN_ROWS[3][12] = {
    {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ъ"},
    {"ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э"},
    {"ё", "я", "ч", "с", "м", "и", "т", "ь", "б", "ю"},
};
static const char* const RUSSIAN_UPPER_ROWS[3][12] = {
    {"Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х", "Ъ"},
    {"Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Э"},
    {"Ё", "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю"},
};
static const int RUSSIAN_ROW_COUNTS[3] = {12, 11, 10};
static const char* const SYMBOL_ROWS[3][11] = {
    {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "."},
    {",", "!", "?", "@", "#", "-", "_", "(", ")", "[", "]"},
    {"'", "\"", "/", ":", ";", "+", "=", "*", "&", "%", "$"},
};
static const int SYMBOL_ROW_COUNTS[3] = {11, 11, 11};
#define KEYBOARD_ROWS 3
#define SERVICE_KEY_COUNT 6

static const SDL_Color COLOR_BG = {20, 25, 33, 255};
static const SDL_Color COLOR_HEADER = {29, 39, 53, 255};
static const SDL_Color COLOR_PANEL = {31, 39, 51, 255};
static const SDL_Color COLOR_SELECTED = {44, 74, 105, 255};
static const SDL_Color COLOR_BLUE = {46, 140, 220, 255};
static const SDL_Color COLOR_TEXT = {238, 241, 247, 255};
static const SDL_Color COLOR_MUTED = {156, 168, 185, 255};

static int chat_max_scroll(const ScreenChat* screen);

static void send_draft(ScreenChat* screen) {
    if (screen->draft_length == 0) return;
    if (tg_send_message(screen->draft)) {
        screen->draft_length = 0;
        screen->draft[0] = '\0';
    }
}

/* Current layout: three rows of character keys plus a service row. */
typedef struct {
    const char* const* rows[KEYBOARD_ROWS];
    int counts[KEYBOARD_ROWS];
    int total;
} KeyLayout;

static KeyLayout current_layout(int symbols, int russian, int uppercase) {
    KeyLayout layout = {{0}, {0}, 0};
    if (symbols) {
        for (int row = 0; row < KEYBOARD_ROWS; ++row) {
            layout.rows[row] = SYMBOL_ROWS[row];
            layout.counts[row] = SYMBOL_ROW_COUNTS[row];
            layout.total += layout.counts[row];
        }
        return layout;
    }
    for (int row = 0; row < KEYBOARD_ROWS; ++row) {
        if (russian) {
            layout.rows[row] = uppercase
                ? RUSSIAN_UPPER_ROWS[row] : RUSSIAN_ROWS[row];
            layout.counts[row] = RUSSIAN_ROW_COUNTS[row];
        } else {
            layout.rows[row] = ENGLISH_ROWS[row];
            layout.counts[row] = ENGLISH_ROW_COUNTS[row];
        }
        layout.total += layout.counts[row];
    }
    return layout;
}

static int key_count_for(const ScreenChat* screen) {
    return current_layout(screen->symbols, screen->russian_keyboard,
                          screen->uppercase).total + SERVICE_KEY_COUNT;
}

/* Locate the row/column for a key index (character area + service row). */
static void key_position(int index, const KeyLayout* layout,
                         int* row, int* column) {
    int remaining = index;
    for (int r = 0; r < KEYBOARD_ROWS; ++r) {
        if (remaining < layout->counts[r]) {
            *row = r;
            *column = remaining;
            return;
        }
        remaining -= layout->counts[r];
    }
    *row = KEYBOARD_ROWS;
    *column = remaining;
}

static int key_index_at(const KeyLayout* layout, int row, int column) {
    int index = 0;
    for (int r = 0; r < row && r < KEYBOARD_ROWS; ++r) {
        index += layout->counts[r];
    }
    return index + column;
}

/* Move selection by keyboard direction; rows have different lengths. */
static void move_key_selection(ScreenChat* screen, int direction) {
    KeyLayout layout = current_layout(screen->symbols, screen->russian_keyboard,
                                      screen->uppercase);
    if (screen->selected_key < 0 || screen->selected_key >= key_count_for(screen)) {
        screen->selected_key = 0;
        return;
    }
    int row, column;
    key_position(screen->selected_key, &layout, &row, &column);
    if (direction == 0 && column > 0) {
        --column;
    } else if (direction == 1 && column + 1 < (row < KEYBOARD_ROWS
                     ? layout.counts[row] : SERVICE_KEY_COUNT)) {
        ++column;
    } else if (direction == 2 && row > 0) {
        --row;
        int count = row < KEYBOARD_ROWS ? layout.counts[row] : SERVICE_KEY_COUNT;
        if (column >= count) column = count - 1;
    } else if (direction == 3 && row < KEYBOARD_ROWS) {
        ++row;
        int count = row < KEYBOARD_ROWS ? layout.counts[row] : SERVICE_KEY_COUNT;
        if (column >= count) column = count - 1;
    } else {
        return;
    }
    screen->selected_key = key_index_at(&layout, row, column);
}

static int service_key_index(const ScreenChat* screen) {
    return current_layout(screen->symbols, screen->russian_keyboard,
                          screen->uppercase).total;
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

/* Label for a character key; English uppercases on the fly (Y key). */
static const char* key_label(ScreenChat* screen, const KeyLayout* layout,
                             int index, char* buffer, size_t capacity) {
    int row, column;
    key_position(index, layout, &row, &column);
    const char* label = layout->rows[row][column];
    if (!screen->symbols && !screen->russian_keyboard && screen->uppercase &&
            label[0] && label[1] == '\0' && label[0] >= 'a' && label[0] <= 'z') {
        snprintf(buffer, capacity, "%c", label[0] - 'a' + 'A');
        return buffer;
    }
    return label;
}

static void activate_key(ScreenChat* screen) {
    KeyLayout layout = current_layout(screen->symbols, screen->russian_keyboard,
                                      screen->uppercase);
    int letters = layout.total;
    int service = service_key_index(screen);
    if (screen->selected_key < letters) {
        char label[8];
        append_text(screen, key_label(screen, &layout, screen->selected_key,
                                      label, sizeof(label)));
    } else if (screen->selected_key == service) {
        append_text(screen, " ");
    } else if (screen->selected_key == service + 1) {
        delete_last_character(screen);
    } else if (screen->selected_key == service + 2) {
        screen->symbols = !screen->symbols;
    } else if (screen->selected_key == service + 3) {
        screen->russian_keyboard = !screen->russian_keyboard;
        screen->selected_key = service_key_index(screen) + 3;
    } else if (screen->selected_key == service + 4) {
        screen->keyboard_visible = 0;
    } else if (screen->selected_key == service + 5) {
        send_draft(screen);
    }
}

static void chat_handle(Screen* self, const SDL_Event* event) {
    ScreenChat* screen = (ScreenChat*)self;
    if (event->type == SDL_TEXTINPUT) {
        append_text(screen, event->text.text);
        if (screen->keyboard_visible) {
            screen->keyboard_visible = 0;
            screen->scroll_target = chat_max_scroll(screen);
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
            move_key_selection(screen, 2);
            break;
        case SDLK_DOWN:
            if (!screen->keyboard_visible) {
                screen->scroll_target += 42;
                int max_scroll = chat_max_scroll(screen);
                if (screen->scroll_target > max_scroll) screen->scroll_target = max_scroll;
                break;
            }
            move_key_selection(screen, 3);
            break;
        case SDLK_PAGEUP:
            screen->scroll_target -= 140;
            if (screen->scroll_target < 0) screen->scroll_target = 0;
            tg_load_older_messages();
            break;
        case SDLK_PAGEDOWN:
            screen->scroll_target += 140;
            if (!screen->keyboard_visible) {
                int max_scroll = chat_max_scroll(screen);
                if (screen->scroll_target > max_scroll) screen->scroll_target = max_scroll;
            }
            break;
        case SDLK_LEFT:
            if (screen->keyboard_visible) move_key_selection(screen, 0);
            break;
        case SDLK_RIGHT:
            if (screen->keyboard_visible) move_key_selection(screen, 1);
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
            screen->scroll_target = chat_max_scroll(screen);
            break;
        case SDLK_s:
            if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                send_draft(screen);
            }
            break;
        case SDLK_y:
            if (event->key.keysym.scancode == SDL_SCANCODE_UNKNOWN) {
                if (screen->keyboard_visible) {
                    if (screen->symbols) screen->symbols = 0;
                    else screen->uppercase = !screen->uppercase;
                } else {
                    tg_open_profile_for_selected_chat();
                    app_set_screen(ScreenId_Profile);
                }
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
            screen->scroll_target = chat_max_scroll(screen);
        } else if (message_count > previous_count && screen->scroll_target == 0) {
            screen->scroll_target = chat_max_scroll(screen);
        } else if (message_count > previous_count) {
            screen->scroll_target = chat_max_scroll(screen);
        }
    }
    int viewport = screen->keyboard_visible ? 3 : 6;
    (void)viewport;
    int max_scroll = chat_max_scroll(screen);
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

#define MESSAGE_LINE_HEIGHT 17
#define MESSAGE_MAX_TEXT_WIDTH 400
#define PHOTO_MAX_WIDTH 240
#define PHOTO_MAX_HEIGHT 170

/* Greedy word wrap: returns the number of lines the text needs at width. */
static int wrapped_line_count(const char* text, int max_width) {
    if (!text || !text[0]) return 1;
    int lines = 1;
    int line_width = 0;
    const char* word = text;
    while (*word) {
        while (*word == ' ') {
            if (line_width > 0) line_width += ui_text_width(" ", 14);
            ++word;
        }
        if (!*word) break;
        const char* end = word;
        while (*end && *end != ' ') ++end;
        char buffer[256];
        size_t length = (size_t)(end - word);
        if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
        memcpy(buffer, word, length);
        buffer[length] = '\0';
        int word_width = ui_text_width(buffer, 14);
        if (word_width > max_width) {
            /* Very long unbreakable word: split by measured chunks. */
            if (line_width > 0) {
                ++lines;
                line_width = 0;
            }
            lines += word_width / max_width;
            line_width = word_width % max_width;
        } else if (line_width > 0 && line_width + word_width > max_width) {
            ++lines;
            line_width = word_width;
        } else {
            line_width += word_width;
        }
        word = end;
    }
    return lines;
}

static int message_height(const TgMessage* message) {
    int height = 14;
    if (message->sender_name[0] && !message->outgoing) {
        height += 16; /* sender caption line in group chats */
    }
    if (message->photo_file_id || message->photo_path[0]) {
        height += 12;
        if (message->photo_path[0] && images_available()) {
            SDL_Surface* photo = images_get(message->photo_path);
            if (photo) {
                int image_height = photo->h;
                if (photo->w > PHOTO_MAX_WIDTH) {
                    image_height = photo->h * PHOTO_MAX_WIDTH / photo->w;
                }
                if (image_height > PHOTO_MAX_HEIGHT) {
                    image_height = PHOTO_MAX_HEIGHT;
                }
                height += image_height;
            } else {
                height += 40;
            }
        } else {
            height += 40;
        }
    } else if (message->media_kind != TgMediaNone) {
        height += 12 + 26; /* label line for media without a thumbnail */
    }
    if (message->text[0]) {
        int max_width = message->photo_file_id || message->photo_path[0]
            ? PHOTO_MAX_WIDTH : MESSAGE_MAX_TEXT_WIDTH;
        height += wrapped_line_count(message->text, max_width) *
                  MESSAGE_LINE_HEIGHT + 4;
    } else if (!message->photo_file_id && !message->photo_path[0] &&
               message->media_kind == TgMediaNone) {
        height += MESSAGE_LINE_HEIGHT;
    }
    return height;
}

static int chat_content_height(void) {
    int total = 0;
    int count = tg_message_count();
    for (int i = 0; i < count; ++i) {
        const TgMessage* message = tg_message_at(i);
        if (message) total += message_height(message);
    }
    return total;
}

static int chat_max_scroll(const ScreenChat* screen) {
    int max = chat_content_height() -
              ((screen->keyboard_visible ? 286 : 380) - 72);
    return max > 0 ? max : 0;
}

static int utf8_char_length(char first) {
    unsigned char byte = (unsigned char)first;
    if (byte < 0x80) return 1;
    if ((byte & 0xe0) == 0xc0) return 2;
    if ((byte & 0xf0) == 0xe0) return 3;
    if ((byte & 0xf8) == 0xf0) return 4;
    return 1;
}

static int text_n_width(const char* text, int length) {
    char buffer[256];
    if (length >= (int)sizeof(buffer)) length = (int)sizeof(buffer) - 1;
    memcpy(buffer, text, (size_t)length);
    buffer[length] = '\0';
    return ui_text_width(buffer, 14);
}

/* Draw one word, splitting across lines if it alone exceeds max_width. */
static void draw_long_word(AppRenderer* renderer, const char* word, int length,
                           int x, int* pen_y, int max_width, SDL_Color color) {
    int start = 0;
    while (start < length) {
        int best = start;
        for (int i = start; i < length;) {
            i += utf8_char_length(word[i]);
            if (text_n_width(word + start, i - start) > max_width) break;
            best = i;
        }
        if (best == start) {
            best = start + utf8_char_length(word[start]);
            if (best > length) best = length;
        }
        char buffer[256];
        int chunk = best - start;
        if (chunk >= (int)sizeof(buffer)) chunk = (int)sizeof(buffer) - 1;
        memcpy(buffer, word + start, (size_t)chunk);
        buffer[chunk] = '\0';
        ui_text(renderer, buffer, x, *pen_y, 14, color);
        start = best;
        if (start < length) *pen_y += MESSAGE_LINE_HEIGHT;
    }
}

/* Draw text wrapped into lines of max_width starting at (x, y). */
static int draw_wrapped_text(AppRenderer* renderer, const char* text,
                             int x, int y, int max_width, SDL_Color color) {
    if (!text || !text[0]) return 1;
    int lines = 1;
    int pen_x = x;
    int pen_y = y;
    const char* word = text;
    int space_width = ui_text_width(" ", 14);
    while (*word) {
        while (*word == ' ') ++word;
        if (!*word) break;
        const char* end = word;
        while (*end && *end != ' ') end += utf8_char_length(*end);
        int word_width = text_n_width(word, (int)(end - word));
        if (word_width > max_width) {
            if (pen_x > x) {
                pen_y += MESSAGE_LINE_HEIGHT;
                ++lines;
                pen_x = x;
            }
            draw_long_word(renderer, word, (int)(end - word), x, &pen_y,
                           max_width, color);
            if (pen_y > y + (lines - 1) * MESSAGE_LINE_HEIGHT) {
                lines = (pen_y - y) / MESSAGE_LINE_HEIGHT + 1;
            }
            pen_x = x + word_width % max_width;
        } else if (pen_x > x && pen_x + space_width + word_width > x + max_width) {
            pen_y += MESSAGE_LINE_HEIGHT;
            ++lines;
            pen_x = x;
            ui_text(renderer, "", pen_x, pen_y, 14, color);
            {
                char buffer[256];
                int length = (int)(end - word);
                if (length >= (int)sizeof(buffer)) length = (int)sizeof(buffer) - 1;
                memcpy(buffer, word, (size_t)length);
                buffer[length] = '\0';
                ui_text(renderer, buffer, pen_x, pen_y, 14, color);
            }
            pen_x += word_width;
        } else {
            char buffer[256];
            int length = (int)(end - word);
            if (length >= (int)sizeof(buffer)) length = (int)sizeof(buffer) - 1;
            memcpy(buffer, word, (size_t)length);
            buffer[length] = '\0';
            if (pen_x > x) pen_x += space_width;
            ui_text(renderer, buffer, pen_x, pen_y, 14, color);
            pen_x += word_width;
        }
        word = end;
    }
    return lines;
}

static const char* media_kind_label(int kind) {
    switch (kind) {
        case TgMediaPhoto: return tr("Photo", "Фото");
        case TgMediaVideo: return tr("Video", "Видео");
        case TgMediaAnimation: return "GIF";
        case TgMediaAudio: return tr("Audio", "Аудио");
        case TgMediaDocument: return tr("File", "Файл");
        case TgMediaVoice: return tr("Voice message", "Голосовое сообщение");
        case TgMediaVideoNote: return tr("Video message", "Видеосообщение");
        default: return tr("Attachment", "Вложение");
    }
}

static void chat_render(Screen* self, AppRenderer* renderer) {
    ScreenChat* screen = (ScreenChat*)self;
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, COLOR_BG);
    ui_round_rect(renderer, (SDL_Rect){0, 0, 640, 54}, 0, COLOR_HEADER);
    /* Avatar in the header when the chat photo is available. */
    if (tg_selected_chat_has_photo()) {
        SDL_Surface* surface = images_get(tg_selected_chat_photo_path());
        if (surface) {
            ui_blit_surface(renderer, surface, 10, 8, 38, 38);
        }
    } else {
        ui_round_rect(renderer, (SDL_Rect){10, 8, 38, 38}, 19, COLOR_BLUE);
    }
    ui_text(renderer, tg_selected_chat_title(), 60, 7, 18, COLOR_TEXT);
    ui_text(renderer, tr("TELEGRAM", "ТЕЛЕГРАМ"), 60, 31, 14, COLOR_MUTED);

    int message_count = tg_message_count();
    int viewport_bottom = screen->keyboard_visible ? 286 : 380;
    int content_y = 72 - (int)screen->scroll_position;
    for (int i = 0; i < message_count; ++i) {
        const TgMessage* message = tg_message_at(i);
        if (!message) continue;
        int height = message_height(message);
        if (content_y + height >= 62 && content_y <= viewport_bottom) {
            int has_photo = message->photo_file_id || message->photo_path[0];
            SDL_Surface* photo = message->photo_path[0]
                ? images_get(message->photo_path) : NULL;
            int image_width = 0;
            int image_height = 0;
            if (photo) {
                image_width = photo->w;
                image_height = photo->h;
                if (image_width > PHOTO_MAX_WIDTH) {
                    image_height = photo->h * PHOTO_MAX_WIDTH / photo->w;
                    image_width = PHOTO_MAX_WIDTH;
                }
                if (image_height > PHOTO_MAX_HEIGHT) {
                    image_width = image_width * PHOTO_MAX_HEIGHT / image_height;
                    image_height = PHOTO_MAX_HEIGHT;
                }
            }
            int text_width = message->text[0]
                ? ui_text_width(message->text, 14) : 0;
            int wrap_width = has_photo ? PHOTO_MAX_WIDTH : MESSAGE_MAX_TEXT_WIDTH;
            int width = has_photo
                ? (image_width > text_width ? image_width : text_width) + 22
                : text_width + 22;
            if (!has_photo && width > MESSAGE_MAX_TEXT_WIDTH + 22) {
                width = MESSAGE_MAX_TEXT_WIDTH + 22;
            }
            if (width < 60) width = 60;
            if (width > 616) width = 616;
            int x = message->outgoing ? 620 - width : 18;
            ui_round_rect(renderer, (SDL_Rect){x, content_y, width, height - 4}, 11,
                          message->outgoing ? COLOR_SELECTED : COLOR_PANEL);
            int inside_y = content_y + 7;
            if (message->sender_name[0] && !message->outgoing) {
                ui_text(renderer, message->sender_name, x + 10, inside_y, 12,
                        COLOR_BLUE);
                inside_y += 16;
            }
            if (has_photo) {
                if (photo) {
                    ui_blit_surface(renderer, photo, x + 11, inside_y,
                                    image_width, image_height);
                    inside_y += image_height + 5;
                } else {
                    /* Download in progress: label + live progress bar. */
                    const char* label = message->media_kind == TgMediaPhoto
                        ? tr("Loading photo…", "Загрузка фото…")
                        : media_kind_label(message->media_kind);
                    int label_width = ui_text_width(label, 13);
                    int label_x = x + (width - label_width) / 2;
                    if (label_x < x + 8) label_x = x + 8;
                    ui_text(renderer, label, label_x, inside_y + 6, 13, COLOR_MUTED);
                    double fraction = 0;
                    if (message->media_expected > 0) {
                        fraction = (double)message->media_downloaded /
                                   (double)message->media_expected;
                    }
                    ui_progress(renderer,
                                (SDL_Rect){x + 11, inside_y + 24, width - 22, 7},
                                fraction, COLOR_BLUE, COLOR_HEADER);
                    inside_y += 40;
                }
            } else if (message->media_kind != TgMediaNone) {
                const char* label = media_kind_label(message->media_kind);
                int label_width = ui_text_width(label, 13);
                int label_x = x + (width - label_width) / 2;
                if (label_x < x + 8) label_x = x + 8;
                ui_text(renderer, label, label_x, inside_y + 4, 13, COLOR_MUTED);
                inside_y += 26;
            }
            if (message->text[0]) {
                draw_wrapped_text(renderer, message->text, x + 10, inside_y,
                                  wrap_width, COLOR_TEXT);
            }
        }
        content_y += height;
    }
    if (message_count == 0) {
        if (tg_state() == TgStateReady) {
            ui_text(renderer, tr("Loading messages…", "Загрузка сообщений…"),
                    22, 100, 15, COLOR_MUTED);
            ui_spinner(renderer, 320, 150, 12, SDL_GetTicks());
        } else {
            ui_text(renderer, tr("No messages yet", "Сообщений пока нет"),
                    22, 100, 14, COLOR_MUTED);
        }
    }
    if (tg_last_error()[0]) {
        ui_text(renderer, tg_last_error(), 18, 207, 12, (SDL_Color){255, 204, 105, 255});
    }

    int input_y = screen->keyboard_visible ? 296 : 396;
    ui_round_rect(renderer, (SDL_Rect){12, input_y, 616, INPUT_FIELD_HEIGHT}, 10, COLOR_PANEL);
    /* Keep the draft on one line: measure a fitting prefix. */
    if (screen->draft_length) {
        char fitted[96];
        size_t length = strlen(screen->draft);
        if (length >= sizeof(fitted)) length = sizeof(fitted) - 1;
        while (length > 0) {
            memcpy(fitted, screen->draft, length);
            fitted[length] = '\0';
            if (ui_text_width(fitted, 14) <= 596) break;
            --length;
        }
        ui_text(renderer, fitted, 22, input_y + 9, 14, COLOR_TEXT);
    } else {
        ui_text(renderer, tr("Write a message", "Введите сообщение"),
                22, input_y + 9, 14, COLOR_MUTED);
    }

    if (screen->keyboard_visible) {
        if (screen->selected_key >= key_count_for(screen)) screen->selected_key = 0;
        KeyLayout layout = current_layout(screen->symbols, screen->russian_keyboard,
                                          screen->uppercase);
        int selected = screen->selected_key;
        int index = 0;
        for (int row = 0; row < KEYBOARD_ROWS; ++row) {
            int count = layout.counts[row];
            int gap = 3;
            int margin = 12;
            int key_width = (640 - margin * 2 - gap * (count - 1)) / count;
            int y = KEYBOARD_TOP + row * KEYBOARD_ROW_STEP;
            for (int column = 0; column < count; ++column) {
                int x = margin + column * (key_width + gap);
                char label_buffer[8];
                const char* label = key_label(screen, &layout, index,
                                              label_buffer, sizeof(label_buffer));
                int text_width = ui_text_width(label, 13);
                int text_x = x + (key_width - text_width) / 2;
                if (text_x < x + 2) text_x = x + 2;
                ui_round_rect(renderer, (SDL_Rect){x, y, key_width, KEYBOARD_KEY_HEIGHT}, 6,
                              index == selected ? COLOR_BLUE : COLOR_PANEL);
                ui_text(renderer, label, text_x, y + 3, 13, COLOR_TEXT);
                ++index;
            }
        }
        /* Service row: wider keys, centered labels, anchored above the
         * hint bar together with the letter rows. */
        static const int service_widths[SERVICE_KEY_COUNT] = {110, 66, 66, 82, 92, 90};
        int keyboard_russian = screen->russian_keyboard;
        const char* labels[SERVICE_KEY_COUNT] = {
            keyboard_russian ? "ПРОБЕЛ" : "SPACE",
            keyboard_russian ? "УДЛ" : "DEL",
            screen->symbols ? "ABC" : "123",
            "EN/РУС",
            keyboard_russian ? "СКРЫТЬ" : "HIDE",
            keyboard_russian ? "ОТПР" : "SEND",
        };
        int gap = 6;
        int total_width = 0;
        for (int i = 0; i < SERVICE_KEY_COUNT; ++i) {
            total_width += service_widths[i];
        }
        total_width += gap * (SERVICE_KEY_COUNT - 1);
        int x = (640 - total_width) / 2;
        int y = KEYBOARD_TOP + KEYBOARD_ROWS * KEYBOARD_ROW_STEP;
        for (int i = 0; i < SERVICE_KEY_COUNT; ++i) {
            int key_index = layout.total + i;
            const char* label = labels[i];
            int text_width = ui_text_width(label, 13);
            int text_x = x + (service_widths[i] - text_width) / 2;
            if (text_x < x + 2) text_x = x + 2;
            ui_round_rect(renderer, (SDL_Rect){x, y, service_widths[i], KEYBOARD_KEY_HEIGHT}, 6,
                          key_index == selected ? COLOR_BLUE : COLOR_PANEL);
            ui_text(renderer, label, text_x, y + 3, 13, COLOR_TEXT);
            x += service_widths[i] + gap;
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
    screen->russian_keyboard = 0;
    screen->keyboard_visible = 0;
    return screen;
}
