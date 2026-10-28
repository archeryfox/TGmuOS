#define _POSIX_C_SOURCE 200809L

#include "screen_profile.h"

#include "app.h"
#include "images.h"
#include "preferences.h"
#include "statusbar.h"
#include "telegram.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const SDL_Color COLOR_BG = {20, 25, 33, 255};
static const SDL_Color COLOR_HEADER = {29, 39, 53, 255};
static const SDL_Color COLOR_PANEL = {31, 39, 51, 255};
static const SDL_Color COLOR_TEXT = {238, 241, 247, 255};
static const SDL_Color COLOR_MUTED = {156, 168, 185, 255};
static const SDL_Color COLOR_BLUE = {46, 140, 220, 255};
static const SDL_Color COLOR_ONLINE = {120, 200, 120, 255};

static void profile_handle(Screen* self, const SDL_Event* event) {
    ScreenProfile* screen = (ScreenProfile*)self;
    if (event->type != SDL_KEYDOWN || event->key.repeat) return;
    switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            app_set_screen(screen->return_screen);
            break;
        default:
            break;
    }
}

static void profile_update(Screen* self) {
    (void)self;
}

/* Word-wrap a long description into the panel, returns the next y. */
static int draw_description(AppRenderer* renderer, const char* text,
                            int x, int y, int max_width, int max_y) {
    char word[128];
    int pen_x = x;
    int pen_y = y;
    const char* cursor = text;
    while (*cursor && pen_y < max_y) {
        while (*cursor == ' ') ++cursor;
        if (!*cursor) break;
        const char* end = cursor;
        while (*end && *end != ' ') ++end;
        size_t length = (size_t)(end - cursor);
        if (length >= sizeof(word)) length = sizeof(word) - 1;
        memcpy(word, cursor, length);
        word[length] = '\0';
        int word_width = ui_text_width(word, 14);
        if (pen_x > x && pen_x + word_width > x + max_width) {
            pen_y += 18;
            pen_x = x;
        }
        ui_text(renderer, word, pen_x, pen_y, 14, COLOR_TEXT);
        pen_x += word_width + 5;
        cursor = end;
    }
    return pen_y + 18;
}

static const TgChat* profile_chat(void) {
    int64_t id = tg_profile_chat_id();
    for (int i = 0; i < tg_chat_count(); ++i) {
        const TgChat* chat = tg_chat_at(i);
        if (chat && chat->id == id) return chat;
    }
    return NULL;
}

static const char* chat_type_label(const TgChat* chat) {
    switch (chat ? chat->type : TgChatUnknown) {
        case TgChatPrivate: return tr("Personal chat", "Личный чат");
        case TgChatBasicGroup: return tr("Group", "Группа");
        case TgChatSupergroup: return tr("Supergroup", "Супергруппа");
        case TgChatChannel: return tr("Channel", "Канал");
        default: return tr("Chat", "Чат");
    }
}

static void draw_avatar_image(AppRenderer* renderer, const char* path,
                              const char* fallback_letter, int x, int y, int size) {
    SDL_Surface* photo = path && path[0] ? images_get(path) : NULL;
    if (photo) {
        ui_blit_surface(renderer, photo, x, y, size, size);
    } else {
        ui_round_rect(renderer, (SDL_Rect){x, y, size, size}, size / 2, COLOR_BLUE);
        char initial[2] = {fallback_letter && fallback_letter[0]
                               ? fallback_letter[0] : '?', '\0'};
        ui_text(renderer, initial, x + size / 2 - 5, y + size / 2 - 10, 20,
                COLOR_TEXT);
    }
}

static void profile_render(Screen* self, AppRenderer* renderer) {
    (void)self;
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, COLOR_BG);
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 58}, COLOR_HEADER);
    ui_text(renderer, tr("Profile", "Профиль"), 22, 10, 24, COLOR_TEXT);
    ui_status_bar(renderer, 618, 23);

    int content_y = 84;
    const TgUser* user = tg_profile_is_user() ? tg_profile_user() : NULL;
    const TgChat* chat = tg_profile_is_user() ? NULL : profile_chat();
    char name[512] = "";

    if (user) {
        snprintf(name, sizeof(name), "%s%s%s", user->first_name,
                 user->last_name[0] ? " " : "", user->last_name);
        if (!name[0] && user->username[0]) snprintf(name, sizeof(name), "@%s", user->username);
        if (!name[0]) snprintf(name, sizeof(name), "user %lld", (long long)user->id);
    } else if (chat) {
        snprintf(name, sizeof(name), "%s", chat->title);
    } else {
        snprintf(name, sizeof(name), "…");
    }

    draw_avatar_image(renderer,
                      user ? user->photo_path
                           : chat ? chat->photo_path : NULL,
                      name, 32, content_y, 96);
    int text_x = 152;
    ui_text(renderer, name, text_x, content_y + 8, 22, COLOR_TEXT);
    int line_y = content_y + 40;

    if (user) {
        if (user->username[0]) {
            char handle[48];
            snprintf(handle, sizeof(handle), "@%s", user->username);
            ui_text(renderer, handle, text_x, line_y, 15, COLOR_BLUE);
            line_y += 22;
        }
        if (user->phone[0]) {
            char phone[40];
            snprintf(phone, sizeof(phone), "+%s", user->phone);
            ui_text(renderer, phone, text_x, line_y, 15, COLOR_TEXT);
            line_y += 22;
        }
        if (user->status[0]) {
            int online = strcmp(user->status, "online") == 0;
            ui_text(renderer, user->status, text_x, line_y, 14,
                    online ? COLOR_ONLINE : COLOR_MUTED);
            line_y += 22;
        }
    } else if (chat) {
        ui_text(renderer, chat_type_label(chat), text_x, line_y, 15, COLOR_MUTED);
        line_y += 22;
        if (tg_profile_member_count() > 0) {
            char members[48];
            snprintf(members, sizeof(members), "%d %s", tg_profile_member_count(),
                     tr("members", "участников"));
            ui_text(renderer, members, text_x, line_y, 15, COLOR_TEXT);
            line_y += 22;
        }
    }

    const char* description = tg_profile_description();
    if (description[0]) {
        int panel_y = line_y + 16;
        if (panel_y < 210) panel_y = 210;
        ui_round_rect(renderer, (SDL_Rect){24, panel_y, 592, 190}, 14, COLOR_PANEL);
        draw_description(renderer, description, 42, panel_y + 18, 556,
                         panel_y + 168);
    } else if ((chat && tg_profile_member_count() == 0) ||
               (user && !user->username[0] && !user->phone[0])) {
        ui_text(renderer, tr("No additional info", "Нет дополнительной информации"),
                text_x, line_y + 4, 14, COLOR_MUTED);
    }

    ui_fill(renderer, (SDL_Rect){0, 438, 640, 42}, COLOR_HEADER);
    ui_text(renderer, tr("B: BACK", "B: НАЗАД"), 18, 451, 13, COLOR_MUTED);
}

static void profile_destroy(Screen* self) {
    free(self);
}

ScreenProfile* screen_profile_create(void) {
    ScreenProfile* screen = calloc(1, sizeof(*screen));
    if (!screen) return NULL;
    screen->base.handle = profile_handle;
    screen->base.update = profile_update;
    screen->base.render = profile_render;
    screen->base.destroy = profile_destroy;
    screen->return_screen = ScreenId_Chats;
    return screen;
}

void screen_profile_set_return(ScreenProfile* screen, int return_screen) {
    if (screen) screen->return_screen = return_screen;
}
