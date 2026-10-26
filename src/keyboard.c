#define _GNU_SOURCE

#include "keyboard.h"

#include "preferences.h"

#include <SDL2/SDL.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAX_KEYBOARDS 16

typedef struct {
    int fd;
    char path[64];
    int shift;
    int caps_lock;
    int control;
    int alt;
} KeyboardDevice;

static KeyboardDevice devices[MAX_KEYBOARDS];
static int device_count;
static uint32_t next_scan_time;

static int bit_is_set(const unsigned long* bits, int bit) {
    return (bits[bit / (8 * sizeof(unsigned long))] >>
            (bit % (8 * sizeof(unsigned long)))) & 1UL;
}

static int supports_keyboard_keys(int fd) {
    unsigned long event_bits[(EV_MAX + 8 * sizeof(unsigned long)) /
                             (8 * sizeof(unsigned long))] = {0};
    unsigned long key_bits[(KEY_MAX + 8 * sizeof(unsigned long)) /
                           (8 * sizeof(unsigned long))] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
        !bit_is_set(event_bits, EV_KEY) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return 0;
    }
    return bit_is_set(key_bits, KEY_A) && bit_is_set(key_bits, KEY_Z) &&
           bit_is_set(key_bits, KEY_SPACE) && bit_is_set(key_bits, KEY_ENTER);
}

static int device_is_open(const char* path) {
    for (int i = 0; i < device_count; ++i) {
        if (strcmp(devices[i].path, path) == 0) return 1;
    }
    return 0;
}

static void scan_devices(void) {
    DIR* directory = opendir("/dev/input");
    if (!directory) return;

    struct dirent* entry;
    while (device_count < MAX_KEYBOARDS && (entry = readdir(directory))) {
        if (strncmp(entry->d_name, "event", 5) != 0 ||
            entry->d_name[5] < '0' || entry->d_name[5] > '9') {
            continue;
        }
        char path[64];
        int length = snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path) || device_is_open(path)) {
            continue;
        }
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (!supports_keyboard_keys(fd)) {
            close(fd);
            continue;
        }
        devices[device_count].fd = fd;
        memcpy(devices[device_count].path, path, (size_t)length + 1);
        ++device_count;
    }
    closedir(directory);
}

static SDL_Scancode scancode_for_key(int code) {
    static const int linux_letters[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
        KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
    };
    for (size_t i = 0; i < sizeof(linux_letters) / sizeof(linux_letters[0]); ++i) {
        if (code == linux_letters[i]) return (SDL_Scancode)(SDL_SCANCODE_A + i);
    }
    static const int linux_digits[] = {
        KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0
    };
    for (size_t i = 0; i < sizeof(linux_digits) / sizeof(linux_digits[0]); ++i) {
        if (code == linux_digits[i]) {
            return i == 9 ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + i);
        }
    }
    switch (code) {
        case KEY_SPACE: return SDL_SCANCODE_SPACE;
        case KEY_ENTER:
        case KEY_KPENTER: return SDL_SCANCODE_RETURN;
        case KEY_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
        case KEY_ESC: return SDL_SCANCODE_ESCAPE;
        case KEY_TAB: return SDL_SCANCODE_TAB;
        case KEY_UP: return SDL_SCANCODE_UP;
        case KEY_DOWN: return SDL_SCANCODE_DOWN;
        case KEY_LEFT: return SDL_SCANCODE_LEFT;
        case KEY_RIGHT: return SDL_SCANCODE_RIGHT;
        case KEY_PAGEUP: return SDL_SCANCODE_PAGEUP;
        case KEY_PAGEDOWN: return SDL_SCANCODE_PAGEDOWN;
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT: return SDL_SCANCODE_LSHIFT;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL: return SDL_SCANCODE_LCTRL;
        case KEY_LEFTALT:
        case KEY_RIGHTALT: return SDL_SCANCODE_LALT;
        case KEY_MINUS: return SDL_SCANCODE_MINUS;
        case KEY_EQUAL: return SDL_SCANCODE_EQUALS;
        case KEY_LEFTBRACE: return SDL_SCANCODE_LEFTBRACKET;
        case KEY_RIGHTBRACE: return SDL_SCANCODE_RIGHTBRACKET;
        case KEY_SEMICOLON: return SDL_SCANCODE_SEMICOLON;
        case KEY_APOSTROPHE: return SDL_SCANCODE_APOSTROPHE;
        case KEY_GRAVE: return SDL_SCANCODE_GRAVE;
        case KEY_COMMA: return SDL_SCANCODE_COMMA;
        case KEY_DOT: return SDL_SCANCODE_PERIOD;
        case KEY_SLASH: return SDL_SCANCODE_SLASH;
        case KEY_BACKSLASH: return SDL_SCANCODE_BACKSLASH;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

static const char* character_for_key(int code, int shifted, int caps_lock, int russian) {
    static const char* russian_lower[26] = {
        "ф", "и", "с", "в", "у", "а", "п", "р", "ш", "о", "л", "д", "ь",
        "т", "щ", "з", "й", "к", "ы", "е", "г", "м", "ц", "ч", "н", "я"
    };
    static const char* russian_upper[26] = {
        "Ф", "И", "С", "В", "У", "А", "П", "Р", "Ш", "О", "Л", "Д", "Ь",
        "Т", "Щ", "З", "Й", "К", "Ы", "Е", "Г", "М", "Ц", "Ч", "Н", "Я"
    };
    static const int linux_letters[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
        KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
    };
    int uppercase = shifted != caps_lock;
    static const char* english_lower[] = {
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",
        "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
        "y", "z"
    };
    static const char* english_upper[] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
        "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X",
        "Y", "Z"
    };
    for (size_t i = 0; i < sizeof(linux_letters) / sizeof(linux_letters[0]); ++i) {
        if (code == linux_letters[i]) {
            if (russian) return uppercase ? russian_upper[i] : russian_lower[i];
            return uppercase ? english_upper[i] : english_lower[i];
        }
    }
    static const int linux_digits[] = {
        KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0
    };
    static const char* plain_digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    static const char* shifted_digits[] = {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"};
    for (size_t i = 0; i < sizeof(linux_digits) / sizeof(linux_digits[0]); ++i) {
        if (code == linux_digits[i]) return shifted ? shifted_digits[i] : plain_digits[i];
    }
    switch (code) {
        case KEY_SPACE: return " ";
        case KEY_MINUS: return shifted ? "_" : "-";
        case KEY_EQUAL: return shifted ? "+" : "=";
        case KEY_LEFTBRACE: return russian ? (shifted ? "Х" : "х") : (shifted ? "{" : "[");
        case KEY_RIGHTBRACE: return russian ? (shifted ? "Ъ" : "ъ") : (shifted ? "}" : "]");
        case KEY_SEMICOLON: return russian ? (shifted ? "Ж" : "ж") : (shifted ? ":" : ";");
        case KEY_APOSTROPHE: return russian ? (shifted ? "Э" : "э") : (shifted ? "\"" : "'");
        case KEY_GRAVE: return russian ? (shifted ? "Ё" : "ё") : (shifted ? "~" : "`");
        case KEY_COMMA: return russian ? (shifted ? "Б" : "б") : (shifted ? "<" : ",");
        case KEY_DOT: return russian ? (shifted ? "Ю" : "ю") : (shifted ? ">" : ".");
        case KEY_SLASH: return russian ? (shifted ? "," : ".") : (shifted ? "?" : "/");
        case KEY_BACKSLASH: return shifted ? "|" : "\\";
        default: return NULL;
    }
}

static void push_key_event(const KeyboardDevice* device, const struct input_event* input) {
    SDL_Scancode scancode = scancode_for_key(input->code);
    if (scancode == SDL_SCANCODE_UNKNOWN) return;

    SDL_Event event;
    SDL_zero(event);
    event.type = input->value == 0 ? SDL_KEYUP : SDL_KEYDOWN;
    event.key.state = input->value == 0 ? SDL_RELEASED : SDL_PRESSED;
    event.key.repeat = input->value == 2 ? 1 : 0;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
    event.key.keysym.mod = (device->shift ? KMOD_SHIFT : KMOD_NONE) |
                           (device->control ? KMOD_CTRL : KMOD_NONE) |
                           (device->alt ? KMOD_ALT : KMOD_NONE);
    SDL_PushEvent(&event);
}

static void push_text_event(const char* text) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_TEXTINPUT;
    snprintf(event.text.text, sizeof(event.text.text), "%s", text);
    SDL_PushEvent(&event);
}

static void process_key_event(KeyboardDevice* device, const struct input_event* input) {
    int pressed = input->value != 0;
    switch (input->code) {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT: device->shift = pressed; break;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL: device->control = pressed; break;
        case KEY_LEFTALT:
        case KEY_RIGHTALT: device->alt = pressed; break;
        case KEY_CAPSLOCK:
            if (input->value == 1) device->caps_lock = !device->caps_lock;
            break;
        default: break;
    }
    if (input->value == 0) {
        push_key_event(device, input);
        return;
    }
    if (input->value != 1 && input->value != 2) return;
    push_key_event(device, input);

    const char* text = character_for_key(
        input->code, device->shift, device->caps_lock, preferences_is_russian());
    if (text && !device->control && !device->alt) push_text_event(text);
}

int keyboard_init(void) {
    memset(devices, 0, sizeof(devices));
    device_count = 0;
    next_scan_time = 0;
    scan_devices();
    return device_count;
}

void keyboard_poll(void) {
    uint32_t now = SDL_GetTicks();
    if (now >= next_scan_time) {
        scan_devices();
        next_scan_time = now + 1000;
    }
    for (int i = 0; i < device_count; ++i) {
        struct input_event event;
        ssize_t bytes;
        while ((bytes = read(devices[i].fd, &event, sizeof(event))) == sizeof(event)) {
            if (event.type == EV_KEY) process_key_event(&devices[i], &event);
        }
        if (bytes == 0 ||
            (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            close(devices[i].fd);
            devices[i] = devices[--device_count];
            --i;
        }
    }
}

void keyboard_shutdown(void) {
    for (int i = 0; i < device_count; ++i) close(devices[i].fd);
    device_count = 0;
}

int keyboard_self_test(void) {
    return strcmp(character_for_key(KEY_A, 0, 0, 0), "a") == 0 &&
           strcmp(character_for_key(KEY_A, 1, 0, 0), "A") == 0 &&
           strcmp(character_for_key(KEY_A, 0, 1, 0), "A") == 0 &&
           strcmp(character_for_key(KEY_A, 1, 1, 0), "a") == 0 &&
           strcmp(character_for_key(KEY_A, 0, 0, 1), "ф") == 0 &&
           strcmp(character_for_key(KEY_A, 1, 0, 1), "Ф") == 0 &&
           strcmp(character_for_key(KEY_SPACE, 0, 0, 0), " ") == 0 &&
           strcmp(character_for_key(KEY_1, 1, 0, 0), "!") == 0 &&
           strcmp(character_for_key(KEY_1, 0, 1, 0), "1") == 0;
}
