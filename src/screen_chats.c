#include "screen_chats.h"
#include "app.h"
#include "preferences.h"
#include "screen_profile.h"
#include "statusbar.h"
#include "telegram.h"
#include "ui.h"
#include "images.h"

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
static const SDL_Color COLOR_ARCHIVE = {88, 88, 96, 255};

static void draw_avatar(ScreenChats* screen, AppRenderer* renderer, int index, int y) {
    const TgChat* chat = tg_chat_at(index);
    if (!chat) return;
    int x = 24;
    int size = 36;
    if (tg_chat_has_photo(index)) {
        SDL_Surface* surface = images_get(tg_chat_photo_path(index));
        if (surface) {
            ui_blit_surface(renderer, surface, x, y + 12, size, size);
            return;
        }
    }
    /* Fallback: first letter of the title on a coloured circle. */
    ui_round_rect(renderer, (SDL_Rect){x, y + 12, size, size}, size / 2,
                  index == screen->selected ? COLOR_BLUE : COLOR_HEADER);
    char initial[2] = {0};
    if (chat->title[0]) {
        initial[0] = chat->title[0];
    }
    ui_text(renderer, initial, x + size / 2 - 4, y + 12 + size / 2 - 8, 16, COLOR_TEXT);
}

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
        case SDLK_LEFT:
            if (tg_current_folder() > 0) {
                tg_set_folder(tg_current_folder() - 1);
                ((ScreenChats*)self)->selected = 0;
                ((ScreenChats*)self)->scroll = 0;
            }
            break;
        case SDLK_RIGHT:
            if (tg_current_folder() < tg_folder_count()) {
                tg_set_folder(tg_current_folder() + 1);
                ((ScreenChats*)self)->selected = 0;
                ((ScreenChats*)self)->scroll = 0;
            }
            break;
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (tg_chat_count() > screen->selected) {
                tg_select_chat(screen->selected);
                app_set_screen(ScreenId_Chat);
            }
            break;
        case SDLK_y:
            if (tg_chat_count() > screen->selected) {
                tg_open_profile_for_chat(screen->selected);
                app_set_screen(ScreenId_Profile);
            }
            break;
        case SDLK_BACKSPACE:
            /* B is "back" on the console: there is nowhere to go back to on
             * the main list, so it is a no-op here (it used to jump to the
             * auth screen, which looked like "scrolling opens a profile"). */
            break;
        case SDLK_s:
        case SDLK_t:
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
    ui_status_bar(renderer, 618, 23);

    int folder_total = tg_folder_count() + 1;
    int widths[10] = {0};
    int positions[10] = {0};
    int total_width = 0;
    int selected = tg_current_folder();
    for (int index = 0; index < folder_total; ++index) {
        const char* title = index == 0 ? tr("All", "Все")
                                       : tg_folder_title(index);
        positions[index] = total_width;
        widths[index] = ui_text_width(title, 13) + 18;
        if (widths[index] < 44) widths[index] = 44;
        total_width += widths[index] + 6;
    }
    int tab_offset = 12;
    if (total_width > 616 && selected >= 0 && selected < folder_total) {
        tab_offset = 320 - positions[selected] - widths[selected] / 2;
        if (tab_offset > 12) tab_offset = 12;
        if (tab_offset < 628 - total_width) tab_offset = 628 - total_width;
    }
    for (int index = 0; index < folder_total; ++index) {
        int tab_x = tab_offset + positions[index];
        if (tab_x + widths[index] < 0 || tab_x >= 640) continue;
        const char* title = index == 0 ? tr("All", "Все")
                                       : tg_folder_title(index);
        ui_round_rect(renderer, (SDL_Rect){tab_x, 62, widths[index], 20}, 8,
                      index == selected ? COLOR_BLUE : COLOR_ROW);
        ui_text(renderer, title, tab_x + 9, 65, 13,
                index == selected ? COLOR_TEXT : COLOR_MUTED);
    }

for (int row = 0; row < VISIBLE_CHATS; ++row) {
        int index = screen->scroll + row;
        const TgChat* chat = tg_chat_at(index);
        if (!chat) break;

        int y = 88 + row * 68;
        ui_round_rect(renderer, (SDL_Rect){12, y, 616, 60}, 15,
                       index == screen->selected ? COLOR_SELECTED : COLOR_ROW);
        draw_avatar(screen, renderer, index, y);
        ui_text(renderer, chat->title, 76, y + 7, 18,
                chat->archived ? COLOR_ARCHIVE : COLOR_TEXT);
        ui_text(renderer, chat->preview, 76, y + 33, 14, COLOR_MUTED);
        if (chat->archived) {
            ui_text(renderer, tr("ARCHIVE", "АРХИВ"), 540, y + 33, 12, COLOR_ARCHIVE);
        }
    }
    if (tg_chat_count() == 0) {
        ui_text(renderer, tr("Loading Telegram chats…", "Загрузка чатов Telegram…"),
                28, 116, 18, COLOR_MUTED);
        ui_spinner(renderer, 320, 168, 12, SDL_GetTicks());
        if (tg_state() == TgStateReady) {
            const char* hint = tr("Connected — waiting for the chat list",
                                  "Подключено — ждём список чатов");
            int hint_width = ui_text_width(hint, 13);
            ui_text(renderer, hint, 320 - hint_width / 2, 196, 13, COLOR_MUTED);
        }
    }

    ui_fill(renderer, (SDL_Rect){0, 430, 640, 50}, COLOR_HEADER);
    ui_text(renderer, tr("A: OPEN", "A: ОТКРЫТЬ"), 18, 446, 14, COLOR_TEXT);
    ui_text(renderer, tr("Y: PROFILE", "Y: ПРОФИЛЬ"), 132, 446, 14, COLOR_TEXT);
    ui_text(renderer, tr("LEFT/RIGHT: FOLDER", "ВЛЕВО/ВПРАВО: ПАПКА"), 262, 446, 14, COLOR_TEXT);
    ui_text(renderer, tr("START: SETTINGS", "START: НАСТРОЙКИ"), 476, 446, 14, COLOR_TEXT);
    ui_text(renderer, tr("B: BACK", "B: НАЗАД"), 586, 446, 14, COLOR_MUTED);
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
