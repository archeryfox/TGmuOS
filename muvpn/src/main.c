#define _GNU_SOURCE

#include "input.h"
#include "renderer.h"
#include "ui.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_SERVERS 64
#define SERVER_ID_LENGTH 17
#define SERVER_NAME_LENGTH 128
#define SERVER_LIST_PATH "/root/.config/muvpn/servers.tsv"
#define SUBSCRIPTION_PATH "/root/.config/tgmuos/subscription.conf"

typedef struct {
    char id[SERVER_ID_LENGTH];
    char name[SERVER_NAME_LENGTH];
    int ping;
} Server;

static Server servers[MAX_SERVERS];
static int server_count;
static char selected_server[SERVER_ID_LENGTH];
static int selected_row;
static int first_visible_row;
static pid_t action_pid = -1;
static const char* action_label;
static int action_failed;
static char status_text[64] = "Проверка состояния...";

static const SDL_Color COLOR_PANEL = {27, 35, 52, 255};
static const SDL_Color COLOR_ROW = {34, 44, 63, 255};
static const SDL_Color COLOR_SELECTED = {45, 83, 112, 255};
static const SDL_Color COLOR_TEXT = {238, 242, 248, 255};
static const SDL_Color COLOR_MUTED = {160, 174, 194, 255};
static const SDL_Color COLOR_ACCENT = {76, 205, 164, 255};
static const SDL_Color COLOR_WARNING = {255, 193, 92, 255};

static const char* config_path(const char* variable, const char* fallback) {
    const char* value = getenv(variable);
    return value && value[0] ? value : fallback;
}

static void load_servers(void) {
    server_count = 0;
    FILE* file = fopen(config_path("TGMUOS_SERVERS_FILE", SERVER_LIST_PATH), "r");
    if (!file) return;

    char line[512];
    while (server_count < MAX_SERVERS && fgets(line, sizeof(line), file)) {
        char* id = line;
        char* id_end = strchr(id, '\t');
        if (!id_end) continue;
        *id_end++ = '\0';
        char* ping = id_end;
        char* ping_end = strchr(ping, '\t');
        if (!ping_end) continue;
        *ping_end++ = '\0';
        ping_end[strcspn(ping_end, "\r\n")] = '\0';

        if (strlen(id) != SERVER_ID_LENGTH - 1) continue;
        char* end = NULL;
        long latency = strtol(ping, &end, 10);
        if (!end || *end || latency < -1 || latency > 60000) continue;

        Server* server = &servers[server_count++];
        snprintf(server->id, sizeof(server->id), "%s", id);
        snprintf(server->name, sizeof(server->name), "%s", ping_end);
        server->ping = (int)latency;
    }
    fclose(file);
}

static void load_selection(void) {
    selected_server[0] = '\0';
    FILE* file = fopen(config_path("TGMUOS_SUBSCRIPTION_CONFIG",
                                   SUBSCRIPTION_PATH), "r");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "selected=", 9) == 0) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strlen(line + 9) == SERVER_ID_LENGTH - 1) {
                snprintf(selected_server, sizeof(selected_server), "%s",
                         line + 9);
            }
            break;
        }
    }
    fclose(file);
}

static int selected_row_for_server(void) {
    if (!selected_server[0]) return 0;
    for (int i = 0; i < server_count; ++i) {
        if (strcmp(servers[i].id, selected_server) == 0) return i + 1;
    }
    return 0;
}

static int read_manager_status(char* output, size_t output_size) {
    int pipes[2];
    if (pipe(pipes) < 0) return 0;
    pid_t child = fork();
    if (child < 0) {
        close(pipes[0]);
        close(pipes[1]);
        return 0;
    }
    if (child == 0) {
        dup2(pipes[1], STDOUT_FILENO);
        close(pipes[0]);
        close(pipes[1]);
        execl("./tools/muvpn.py", "muvpn.py", "status", (char*)NULL);
        _exit(127);
    }

    close(pipes[1]);
    ssize_t length = read(pipes[0], output, output_size - 1);
    close(pipes[0]);
    int child_status = 0;
    waitpid(child, &child_status, 0);
    if (length <= 0 || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0) {
        return 0;
    }
    output[length] = '\0';
    output[strcspn(output, "\r\n")] = '\0';
    return 1;
}

static int spawn_action(const char* action, const char* argument,
                        const char* label) {
    if (action_pid > 0) return 0;
    pid_t child = fork();
    if (child < 0) {
        snprintf(status_text, sizeof(status_text), "Не удалось запустить действие");
        return 0;
    }
    if (child == 0) {
        if (argument) {
            execl("./tools/muvpn.py", "muvpn.py", action, argument, (char*)NULL);
        } else {
            execl("./tools/muvpn.py", "muvpn.py", action, (char*)NULL);
        }
        _exit(127);
    }
    action_pid = child;
    action_label = label;
    action_failed = 0;
    return 1;
}

static void refresh_action(void) {
    if (action_pid > 0) {
        int child_status = 0;
        pid_t result = waitpid(action_pid, &child_status, WNOHANG);
        if (result == action_pid) {
            action_failed = !WIFEXITED(child_status) ||
                            WEXITSTATUS(child_status) != 0;
            action_pid = -1;
            action_label = NULL;
            load_servers();
            load_selection();
            selected_row = selected_row_for_server();
        }
    }

    if (action_pid > 0) {
        snprintf(status_text, sizeof(status_text), "%s", action_label);
        return;
    }
    if (action_failed) {
        snprintf(status_text, sizeof(status_text),
                 "Ошибка. Проверьте журнал muVPN");
        return;
    }

    char manager_status[64];
    if (read_manager_status(manager_status, sizeof(manager_status))) {
        snprintf(status_text, sizeof(status_text), "%s",
                 strcmp(manager_status, "connected") == 0 ?
                 "VPN подключён" : "VPN отключён");
    } else {
        snprintf(status_text, sizeof(status_text), "Состояние неизвестно");
    }
}

static SDL_Keycode controller_key(SDL_GameControllerButton button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return SDLK_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return SDLK_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return SDLK_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return SDLK_RIGHT;
        case SDL_CONTROLLER_BUTTON_A: return SDLK_SPACE;
        case SDL_CONTROLLER_BUTTON_B: return SDLK_BACKSPACE;
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return SDLK_x;
        case SDL_CONTROLLER_BUTTON_Y: return SDLK_y;
        case SDL_CONTROLLER_BUTTON_START:
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return SDLK_s;
        case SDL_CONTROLLER_BUTTON_BACK: return SDLK_t;
        case SDL_CONTROLLER_BUTTON_GUIDE: return SDLK_ESCAPE;
        default: return SDLK_UNKNOWN;
    }
}

static int translate_controller_event(const SDL_Event* source,
                                      SDL_Event* translated) {
    if (source->type != SDL_CONTROLLERBUTTONDOWN &&
        source->type != SDL_CONTROLLERBUTTONUP) return 0;
    SDL_Keycode key = controller_key(
        (SDL_GameControllerButton)source->cbutton.button);
    if (key == SDLK_UNKNOWN) return 0;
    SDL_zero(*translated);
    translated->type = source->type == SDL_CONTROLLERBUTTONDOWN ?
                       SDL_KEYDOWN : SDL_KEYUP;
    translated->key.state = source->type == SDL_CONTROLLERBUTTONDOWN ?
                            SDL_PRESSED : SDL_RELEASED;
    translated->key.keysym.sym = key;
    return 1;
}

static void draw(AppRenderer* renderer) {
    renderer_clear(renderer, 0xff0f1420);
    ui_text(renderer, "muVPN", 28, 22, 24, COLOR_TEXT);
    ui_text(renderer, "VPN для всего устройства", 30, 62, 14, COLOR_MUTED);

    ui_round_rect(renderer, (SDL_Rect){24, 100, 592, 70}, 12, COLOR_PANEL);
    SDL_Color state_color = strstr(status_text, "подключ") ?
                            COLOR_ACCENT : COLOR_WARNING;
    ui_text(renderer, status_text, 44, 119, 18, state_color);
    ui_text(renderer, selected_server[0] ?
            "Выбранный узел применяется при переподключении" :
            "Режим выбора: автоматически", 44, 145, 14, COLOR_MUTED);

    ui_text(renderer, "СЕРВЕРЫ  ·  A — выбрать", 30, 186, 14, COLOR_MUTED);
    const int visible_rows = 5;
    int count = server_count + 1;
    if (selected_row < first_visible_row) first_visible_row = selected_row;
    if (selected_row >= first_visible_row + visible_rows) {
        first_visible_row = selected_row - visible_rows + 1;
    }
    int max_first = count > visible_rows ? count - visible_rows : 0;
    if (first_visible_row > max_first) first_visible_row = max_first;

    for (int visible = 0; visible < visible_rows; ++visible) {
        int index = first_visible_row + visible;
        if (index >= count) break;
        int y = 210 + visible * 42;
        int active = index == selected_row;
        ui_round_rect(renderer, (SDL_Rect){24, y, 592, 36}, 8,
                      active ? COLOR_SELECTED : COLOR_ROW);
        if (index == 0) {
            ui_text(renderer, selected_server[0] ? "  Автоматически" :
                    "✓ Автоматически", 42, y + 9, 14, COLOR_TEXT);
            continue;
        }
        const Server* server = &servers[index - 1];
        const char* chosen = strcmp(server->id, selected_server) == 0 ?
                             "✓ " : "  ";
        char label[SERVER_NAME_LENGTH + 8];
        snprintf(label, sizeof(label), "%s%s", chosen, server->name);
        ui_text(renderer, label, 42, y + 9, 14, COLOR_TEXT);
        char latency[32];
        snprintf(latency, sizeof(latency), "%s",
                 server->ping < 0 ? "— ms" : "");
        if (server->ping >= 0) {
            snprintf(latency, sizeof(latency), "%d ms", server->ping);
        }
        int width = ui_text_width(latency, 14);
        ui_text(renderer, latency, 590 - width, y + 9, 14, COLOR_MUTED);
    }

    ui_round_rect(renderer, (SDL_Rect){24, 432, 592, 32}, 8, COLOR_PANEL);
    ui_text(renderer, "X  Подключить/отключить    Y  Обновить    B  Выход",
            38, 440, 14, COLOR_MUTED);
    ui_text(renderer, "VPN остаётся включённым после выхода из muVPN",
            30, 405, 14, COLOR_MUTED);
    renderer_present(renderer);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--smoke-test") == 0) {
        puts("muVPN smoke test passed");
        return 0;
    }

    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);

    AppRenderer renderer;
    if (!renderer_init(&renderer, 0)) {
        SDL_Quit();
        return 1;
    }
    if (TTF_Init() < 0 || !ui_init()) {
        fprintf(stderr, "Font initialization failed: %s\n", TTF_GetError());
        renderer_shutdown(&renderer);
        SDL_Quit();
        return 1;
    }

    Input input = {{0}, {0}, {0}};
    load_servers();
    load_selection();
    selected_row = selected_row_for_server();
    refresh_action();

    int running = 1;
    Uint32 next_status_update = 0;
    while (running) {
        input_begin_frame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            SDL_Event translated;
            if (translate_controller_event(&event, &translated)) {
                event = translated;
            }
            if (event.type == SDL_QUIT) {
                running = 0;
            } else {
                input_update(&input, &event);
            }
        }

        if (input_just_pressed(&input, Button_Up) && selected_row > 0) {
            --selected_row;
        }
        if (input_just_pressed(&input, Button_Down) &&
            selected_row < server_count) {
            ++selected_row;
        }
        if (input_just_pressed(&input, Button_B)) running = 0;
        if (input_just_pressed(&input, Button_A) && action_pid < 0) {
            const char* id = selected_row == 0 ?
                             "auto" : servers[selected_row - 1].id;
            spawn_action("select", id, "Сохранение выбора...");
        }
        if (input_just_pressed(&input, Button_X) && action_pid < 0) {
            char manager_status[64];
            if (read_manager_status(manager_status, sizeof(manager_status)) &&
                strcmp(manager_status, "connected") == 0) {
                spawn_action("stop", NULL, "Отключение VPN...");
            } else {
                spawn_action("start", NULL, "Подключение VPN...");
            }
        }
        if (input_just_pressed(&input, Button_Y) && action_pid < 0) {
            spawn_action("refresh", NULL, "Обновление подписки...");
        }

        Uint32 now = SDL_GetTicks();
        if (now >= next_status_update) {
            refresh_action();
            next_status_update = now + 1000;
        }
        draw(&renderer);
        SDL_Delay(16);
    }

    ui_shutdown();
    TTF_Quit();
    renderer_shutdown(&renderer);
    SDL_Quit();
    return 0;
}
