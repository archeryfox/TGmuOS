#include "screen_chats.h"
#include "app.h"
#include "preferences.h"
#include "telegram.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <stdlib.h>

#define VISIBLE_CHATS 5

static const SDL_Color COLOR_BG = {20, 25, 33, 255};
static const SDL_Color COLOR_HEADER = {29, 39, 53, 255};
static const SDL_Color COLOR_ROW = {31, 39, 51, 255};
static const SDL_Color COLOR_SELECTED = {44, 74, 105, 255};
static const SDL_Color COLOR_TEXT = {238, 241, 247, 255};
static const SDL_Color COLOR_MUTED = {156, 168, 185, 255};
static const SDL_Color COLOR_BLUE = {46, 140, 220, 255};

static void chats_handle(Screen* self, const SDL_Event* event) {
    ScreenChats* screen = (ScreenChats*)self;
    if (event->type != SDL_KEYDOWN) return;

    switch (event->key.keysym.sym) {
        case SDLK_UP:
            if (screen->selected > 0) --screen->selected;
            if (screen->selected < screen->scroll) screen->scroll = screen->selected;
            break;
        case SDLK_DOWN:
            if (screen->selected < tg_chat_count() - 1) ++screen->selected;
            if (screen->selected >= screen->scroll + VISIBLE_CHATS) {
                screen->scroll = screen->selected - VISIBLE_CHATS + 1;
            }
            break;
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (tg_chat_count() > screen->selected) {
                tg_select_chat(screen->selected);
                app_set_screen(ScreenId_Chat);
            }
            break;
        case SDLK_BACKSPACE:
            app_set_screen(ScreenId_Auth);
            break;
        case SDLK_s:
        case SDLK_t:
        case SDLK_y:
            app_set_screen(ScreenId_Settings);
            break;
        default:
            break;
    }
}

static void chats_update(Screen* self) {
    (void)self;
    int count = tg_chat_count();
    if (count == 0) return;
    if (((ScreenChats*)self)->selected >= count) {
        ((ScreenChats*)self)->selected = count - 1;
    }
}

static void chats_render(Screen* self, AppRenderer* renderer) {
    ScreenChats* screen = (ScreenChats*)self;
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 480}, COLOR_BG);
    ui_fill(renderer, (SDL_Rect){0, 0, 640, 58}, COLOR_HEADER);
    ui_text(renderer, tr("Chats", "Чаты"), 22, 10, 24, COLOR_TEXT);
    ui_text(renderer, tr("TELEGRAM", "ТЕЛЕГРАМ"), 510, 20, 14, COLOR_BLUE);

    for (int row = 0; row < VISIBLE_CHATS; ++row) {
        int index = screen->scroll + row;
        const TgChat* chat = tg_chat_at(index);
        if (!chat) break;

        int y = 72 + row * 68;
        ui_round_rect(renderer, (SDL_Rect){12, y, 616, 60}, 15,
                      index == screen->selected ? COLOR_SELECTED : COLOR_ROW);
        ui_round_rect(renderer, (SDL_Rect){24, y + 12, 36, 36}, 12,
                      index == screen->selected ? COLOR_BLUE : COLOR_HEADER);
        ui_text(renderer, chat->title, 76, y + 7, 18, COLOR_TEXT);
        ui_text(renderer, chat->preview, 76, y + 33, 14, COLOR_MUTED);
    }
    if (tg_chat_count() == 0) {
        ui_text(renderer, tr("Loading Telegram chats…", "Загрузка чатов Telegram…"),
                28, 100, 18, COLOR_MUTED);
    }

    ui_fill(renderer, (SDL_Rect){0, 430, 640, 50}, COLOR_HEADER);
    ui_text(renderer, tr("A: OPEN", "A: ОТКРЫТЬ"), 18, 446, 14, COLOR_TEXT);
    ui_text(renderer, tr("START: SETTINGS", "START: НАСТРОЙКИ"), 156, 446, 14, COLOR_TEXT);
    ui_text(renderer, tr("B: BACK", "B: НАЗАД"), 500, 446, 14, COLOR_MUTED);
}

static void chats_destroy(Screen* self) {
    free(self);
}

ScreenChats* screen_chats_create(void) {
    ScreenChats* screen = calloc(1, sizeof(*screen));
    if (!screen) return NULL;

    screen->base.handle = chats_handle;
    screen->base.update = chats_update;
    screen->base.render = chats_render;
    screen->base.destroy = chats_destroy;
    screen->selected = 0;
    screen->scroll = 0;
    return screen;
}
