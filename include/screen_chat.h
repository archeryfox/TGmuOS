#pragma once

#include "screen.h"

#include <stdint.h>

#define CHAT_MESSAGE_LENGTH 96

typedef struct {
    Screen base;
    int selected_key;
    int uppercase;
    int symbols;
    int keyboard_visible;
    int scroll_target;
    float scroll_position;
    uint32_t last_update;
    int last_message_count;
    int64_t current_chat_id;
    char draft[CHAT_MESSAGE_LENGTH];
    int draft_length;
} ScreenChat;

ScreenChat* screen_chat_create(void);
