#pragma once

#include <stdint.h>

#define TG_MAX_CHATS 64
#define TG_MAX_MESSAGES 128
#define TG_TEXT_LENGTH 256

typedef enum {
    TgStateUnavailable,
    TgStateInitializing,
    TgStatePhone,
    TgStateCode,
    TgStatePassword,
    TgStateQr,
    TgStateReady,
    TgStateError
} TgState;

typedef struct {
    int64_t id;
    char title[TG_TEXT_LENGTH];
    char preview[TG_TEXT_LENGTH];
} TgChat;

typedef struct {
    int64_t id;
    int outgoing;
    char text[TG_TEXT_LENGTH];
    int photo_file_id;
    int photo_pending;
    char photo_path[256];
} TgMessage;

void tg_start(void);
void tg_stop(void);
void tg_poll(void);
TgState tg_state(void);
const char* tg_status(void);
const char* tg_prompt(void);
const char* tg_qr_link(void);
int tg_qr_matrix(unsigned char matrix[37][37]);
void tg_request_qr_auth(void);
void tg_load_older_messages(void);
const char* tg_last_error(void);
int tg_chat_count(void);
const TgChat* tg_chat_at(int index);
int tg_folder_count(void);
const char* tg_folder_title(int index);
int tg_current_folder(void);
void tg_set_folder(int index);
int64_t tg_selected_chat_id(void);
const char* tg_selected_chat_title(void);
int tg_message_count(void);
const TgMessage* tg_message_at(int index);
void tg_select_chat(int index);
int tg_submit_auth(const char* value);
int tg_send_message(const char* text);
int tg_apply_proxy(int enabled);
int tg_apply_subscription(int enabled);
int tg_logout(void);
int tg_has_saved_account(void);
int tg_restore_saved_account(void);
int tg_json_self_test(void);
