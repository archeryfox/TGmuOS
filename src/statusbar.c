#define _POSIX_C_SOURCE 200809L

#include "statusbar.h"

#include "preferences.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CACHE_MS 4000

static uint32_t last_read_ms;
static int battery_percent = -1;
static int battery_charging;
static int network_up;
static int vpn_active;
static char clock_text[8] = "--:--";

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int read_int_file(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return -1;
    int value = -1;
    if (fscanf(file, "%d", &value) != 1) value = -1;
    fclose(file);
    return value;
}

static void read_battery(void) {
    battery_percent = -1;
    battery_charging = 0;
    DIR* directory = opendir("/sys/class/power_supply");
    if (!directory) return;
    struct dirent* entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') continue;
        char path[320];
        snprintf(path, sizeof(path),
                 "/sys/class/power_supply/%s/capacity", entry->d_name);
        int capacity = read_int_file(path);
        if (capacity < 0) continue;
        battery_percent = capacity;
        snprintf(path, sizeof(path),
                 "/sys/class/power_supply/%s/status", entry->d_name);
        FILE* status = fopen(path, "r");
        if (status) {
            char text[32] = {0};
            if (fgets(text, sizeof(text), status) &&
                    strstr(text, "Charging")) {
                battery_charging = 1;
            }
            fclose(status);
        }
        break;
    }
    closedir(directory);
}

static void read_network(void) {
    network_up = 0;
    DIR* directory = opendir("/sys/class/net");
    if (!directory) return;
    struct dirent* entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.' || strcmp(entry->d_name, "lo") == 0 ||
                strncmp(entry->d_name, "tun", 3) == 0 ||
                strncmp(entry->d_name, "tailscale", 9) == 0) {
            continue;
        }
        char path[320];
        snprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
                 entry->d_name);
        FILE* file = fopen(path, "r");
        if (!file) continue;
        char state[32] = {0};
        if (fgets(state, sizeof(state), file) && strstr(state, "up")) {
            network_up = 1;
        }
        fclose(file);
        if (network_up) break;
    }
    closedir(directory);
}

static int pid_file_alive(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return 0;
    int pid = 0;
    int ok = fscanf(file, "%d", &pid) == 1 && pid > 1;
    fclose(file);
    if (!ok) return 0;
    char exe[64];
    snprintf(exe, sizeof(exe), "/proc/%d/exe", pid);
    struct stat info;
    return lstat(exe, &info) == 0;
}

static void read_vpn(void) {
    vpn_active = pid_file_alive("/root/.config/muvpn/sing-box.pid");
}

static void refresh(void) {
    uint32_t now = now_ms();
    if (last_read_ms && now - last_read_ms < CACHE_MS) return;
    last_read_ms = now;

    time_t current = time(NULL);
    struct tm* local = localtime(&current);
    if (local) {
        snprintf(clock_text, sizeof(clock_text), "%02d:%02d",
                 local->tm_hour, local->tm_min);
    }

    read_battery();
    read_network();
    read_vpn();
}

void ui_status_bar(AppRenderer* renderer, int x, int y) {
    refresh();

    static const SDL_Color COLOR_TEXT = {238, 241, 247, 255};
    static const SDL_Color COLOR_MUTED = {156, 168, 185, 255};
    static const SDL_Color COLOR_OK = {120, 200, 120, 255};
    static const SDL_Color COLOR_WARN = {255, 120, 100, 255};
    SDL_Color battery_color = battery_percent >= 0 && battery_percent <= 20
        ? COLOR_WARN : COLOR_TEXT;

    char text[96];
    const char* proxy = preferences_proxy_enabled()
        ? tr("PROXY", "ПРОКСИ") : tr("DIRECT", "НАПРЯМУЮ");
    SDL_Color proxy_color = preferences_proxy_enabled() ? COLOR_OK : COLOR_MUTED;
    const char* vpn = vpn_active ? "VPN" : "";
    SDL_Color vpn_color = COLOR_OK;

    snprintf(text, sizeof(text), "%d%%%s", battery_percent,
             battery_charging ? "+" : "");
    int width = ui_text_width(text, 13);
    ui_text(renderer, text, x - width, y, 13,
            battery_percent < 0 ? COLOR_MUTED : battery_color);
    x -= width + 12;

    snprintf(text, sizeof(text), "%s", clock_text);
    width = ui_text_width(text, 13);
    ui_text(renderer, text, x - width, y, 13, COLOR_TEXT);
    x -= width + 12;

    if (vpn[0]) {
        width = ui_text_width(vpn, 13);
        ui_text(renderer, vpn, x - width, y, 13, vpn_color);
        x -= width + 8;
    }

    width = ui_text_width(proxy, 13);
    ui_text(renderer, proxy, x - width, y, 13, proxy_color);
    x -= width + 8;

    /* Network dot: green when a WiFi interface is up, red otherwise. */
    ui_round_rect(renderer, (SDL_Rect){x - 8, y + 3, 8, 8}, 4,
                  network_up ? COLOR_OK : COLOR_WARN);
}
