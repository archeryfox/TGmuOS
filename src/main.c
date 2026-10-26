#include "app.h"
#include "input.h"
#include "keyboard.h"
#include "qr.h"
#include "screen_auth.h"
#include "screen_chat.h"
#include "screen_chats.h"
#include "screen_settings.h"
#include "ui.h"
#include "renderer.h"
#include "telegram.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int controller_key(SDL_GameControllerButton button, SDL_Keycode* key) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: *key = SDLK_UP; return 1;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: *key = SDLK_DOWN; return 1;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: *key = SDLK_LEFT; return 1;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: *key = SDLK_RIGHT; return 1;
        case SDL_CONTROLLER_BUTTON_A: *key = SDLK_SPACE; return 1;
        case SDL_CONTROLLER_BUTTON_B: *key = SDLK_BACKSPACE; return 1;
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: *key = SDLK_x; return 1;
        case SDL_CONTROLLER_BUTTON_Y: *key = SDLK_y; return 1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: *key = SDLK_s; return 1;
        case SDL_CONTROLLER_BUTTON_START: *key = SDLK_s; return 1;
        case SDL_CONTROLLER_BUTTON_BACK: *key = SDLK_t; return 1;
        case SDL_CONTROLLER_BUTTON_GUIDE: *key = SDLK_ESCAPE; return 1;
        default: return 0;
    }
}

static int translate_controller_event(const SDL_Event* source, SDL_Event* translated) {
    if (source->type != SDL_CONTROLLERBUTTONDOWN &&
        source->type != SDL_CONTROLLERBUTTONUP) return 0;

    SDL_Keycode key;
    if (!controller_key((SDL_GameControllerButton)source->cbutton.button, &key)) return 0;

    SDL_zero(*translated);
    translated->type = source->type == SDL_CONTROLLERBUTTONDOWN ? SDL_KEYDOWN : SDL_KEYUP;
    translated->key.state = source->type == SDL_CONTROLLERBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    translated->key.keysym.sym = key;
    return 1;
}

static void destroy_screens(Screen** screens) {
    for (int i = 0; i < ScreenId_Count; ++i) {
        if (screens[i] && screens[i]->destroy) {
            screens[i]->destroy(screens[i]);
            screens[i] = NULL;
        }
    }
}

int main(int argc, char* argv[]) {
    int smoke_test = argc > 1 && strcmp(argv[1], "--smoke-test") == 0;
    int frame_test = argc > 1 && strcmp(argv[1], "--frame-test") == 0;
    int settings_test = argc > 1 && strcmp(argv[1], "--settings-test") == 0;
    int telegram_json_test = argc > 1 && strcmp(argv[1], "--telegram-json-test") == 0;
    int qr_test = argc > 1 && strcmp(argv[1], "--qr-test") == 0;
    int keyboard_test = argc > 1 && strcmp(argv[1], "--keyboard-test") == 0;
    int headless = smoke_test || settings_test ||
                   (argc > 1 && strcmp(argv[1], "--headless") == 0);

    if (keyboard_test) {
        int passed = keyboard_self_test();
        fprintf(stderr, "External keyboard test %s\n", passed ? "passed" : "failed");
        return passed ? 0 : 1;
    }
    if (qr_test) {
        int passed = qr_self_test();
        fprintf(stderr, "QR encoder test %s\n", passed ? "passed" : "failed");
        return passed ? 0 : 1;
    }
    if (telegram_json_test) {
        int passed = tg_json_self_test();
        fprintf(stderr, "Telegram JSON test %s\n", passed ? "passed" : "failed");
        return passed ? 0 : 1;
    }

    AppRenderer renderer;
    memset(&renderer, 0, sizeof(renderer));
    renderer.fd = -1;
    SDL_GameController* controller = NULL;
    Screen* screens[ScreenId_Count] = {NULL};
    int result = 1;
    int sdl_initialized = 0;
    int ttf_initialized = 0;
    int ui_initialized = 0;
    int renderer_initialized = 0;

    fprintf(stderr, "tgmuos starting%s\n", headless ? " (headless test)" : "");

    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    sdl_initialized = 1;
    int keyboard_count = keyboard_init();
    fprintf(stderr, "External keyboards detected: %d\n", keyboard_count);
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    if (SDL_NumJoysticks() > 0) {
        if (SDL_IsGameController(0)) {
            controller = SDL_GameControllerOpen(0);
            if (controller) {
                const char* name = SDL_GameControllerName(controller);
                fprintf(stderr, "Opened controller: %s\n", name ? name : "unknown");
            } else {
                fprintf(stderr, "SDL_GameControllerOpen failed: %s\n", SDL_GetError());
            }
        } else {
            fprintf(stderr, "No game-controller mapping found for joystick 0\n");
        }
    } else {
        fprintf(stderr, "No game controller detected\n");
    }

    if (!renderer_init(&renderer, headless)) goto cleanup;
    renderer_initialized = 1;

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        goto cleanup;
    }
    ttf_initialized = 1;

    if (!ui_init()) goto cleanup;
    ui_initialized = 1;
    tg_start();
    tg_poll();

    if (smoke_test || frame_test) {
        const SDL_Keycode keys[] = {SDLK_SPACE, SDLK_SPACE};
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            SDL_Event event = {0};
            event.type = SDL_KEYDOWN;
            event.key.keysym.sym = keys[i];
            if (SDL_PushEvent(&event) < 0) {
                fprintf(stderr, "SDL_PushEvent failed: %s\n", SDL_GetError());
                goto cleanup;
            }
            event.type = SDL_KEYUP;
            if (SDL_PushEvent(&event) < 0) {
                fprintf(stderr, "SDL_PushEvent failed: %s\n", SDL_GetError());
                goto cleanup;
            }
        }
    }

    screens[ScreenId_Auth] = (Screen*)screen_auth_create();
    screens[ScreenId_Chats] = (Screen*)screen_chats_create();
    screens[ScreenId_Chat] = (Screen*)screen_chat_create();
    screens[ScreenId_Settings] = (Screen*)screen_settings_create();
    for (int i = 0; i < ScreenId_Count; ++i) {
        if (!screens[i]) {
            fprintf(stderr, "Failed to create screen %d\n", i);
            goto cleanup;
        }
    }

    if (settings_test) {
        app_set_screen(ScreenId_Settings);
        const SDL_Keycode keys[] = {SDLK_DOWN, SDLK_DOWN, SDLK_DOWN, SDLK_SPACE};
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            SDL_Event event = {0};
            event.type = SDL_KEYDOWN;
            event.key.keysym.sym = keys[i];
            if (SDL_PushEvent(&event) < 0) {
                fprintf(stderr, "SDL_PushEvent failed: %s\n", SDL_GetError());
                goto cleanup;
            }
        }
    }

    fprintf(stderr, "UI ready; press ESC to quit\n");
    result = 0;
    int running = 1;
    int first_frame = 1;
    SDL_Event event;

    while (running) {
        tg_poll();
        keyboard_poll();
        input_begin_frame();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
                break;
            }

            SDL_Event translated_event;
            const SDL_Event* app_event = &event;
            if (translate_controller_event(&event, &translated_event)) {
                app_event = &translated_event;
            }
            input_update(&g_input, app_event);

            if (app_event->type == SDL_KEYDOWN && app_event->key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
                break;
            }

            ScreenId current = app_current_screen();
            if (current >= 0 && current < ScreenId_Count && screens[current]) {
                screens[current]->handle(screens[current], app_event);
            }
        }

        ScreenId current = app_current_screen();
        if (current < 0 || current >= ScreenId_Count || !screens[current]) {
            fprintf(stderr, "Invalid active screen: %d\n", current);
            result = 1;
            break;
        }

        screens[current]->update(screens[current]);
        renderer_clear(&renderer, 0xff141921u);
        screens[current]->render(screens[current], &renderer);
        renderer_present(&renderer);

        if (first_frame) {
            fprintf(stderr, "First frame rendered on screen %d\n", current);
            first_frame = 0;
        }
        if (smoke_test || frame_test || settings_test) running = 0;
        SDL_Delay(16);
    }

cleanup:
    keyboard_shutdown();
    destroy_screens(screens);
    tg_stop();
    if (ui_initialized) ui_shutdown();
    if (renderer_initialized) renderer_shutdown(&renderer);
    if (controller) SDL_GameControllerClose(controller);
    if (ttf_initialized) TTF_Quit();
    if (sdl_initialized) SDL_Quit();
    fprintf(stderr, "tgmuos stopped\n");
    return result;
}
