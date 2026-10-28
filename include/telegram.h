#pragma once

#include <stdint.h>

#define TG_MAX_CHATS 200
#define TG_MAX_MESSAGES 128
#define TG_TEXT_LENGTH 256
#define TG_MESSAGE_TEXT 2048

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

typedef enum {
    TgChatUnknown,
    TgChatPrivate,
    TgChatBasicGroup,
    TgChatSupergroup,
    TgChatChannel
} TgChatType;

typedef enum {
    TgMediaNone,
    TgMediaPhoto,
    TgMediaVideo,
    TgMediaAnimation,
    TgMediaAudio,
    TgMediaDocument,
    TgMediaVoice,
    TgMediaVideoNote
} TgMediaKind;

typedef struct {
    int64_t id;
    char title[TG_TEXT_LENGTH];
    char preview[TG_TEXT_LENGTH];
    int photo_file_id;
    int photo_pending;
    char photo_path[256];
    int archived;
    int type; /* TgChatType */
    int64_t user_id;       /* private chats: the other user */
    int64_t group_id;      /* supergroups/channels: supergroup id */
} TgChat;

typedef struct {
    int64_t id;
    int outgoing;
    int64_t sender_user_id;
    char sender_name[96];
    char text[TG_MESSAGE_TEXT];
    int media_kind; /* TgMediaKind */
    int photo_file_id;
    int media_file_id;
    int photo_pending;
    int media_pending;
    long media_downloaded;
    long media_expected;
    char photo_path[256]; /* completed photo/thumbnail path */
} TgMessage;

typedef struct {
    int64_t id;
    char first_name[64];
    char last_name[64];
    char username[33];
    char phone[24];
    char status[32]; /* "online" / "recently" / ... localized by UI */
    int photo_file_id;
    int photo_pending;
    char photo_path[256];
    int has_info;
} TgUser;

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
int tg_chat_has_photo(int index);
const char* tg_chat_photo_path(int index);
int tg_selected_chat_has_photo(void);
const char* tg_selected_chat_photo_path(void);
void tg_open_archived_chat(int64_t chat_id);
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
int tg_toggle_adb_usb(void);
int tg_logout(void);
int tg_has_saved_account(void);
int tg_restore_saved_account(void);
int tg_json_self_test(void);

/* Connection quality probe for the status bar: RTT of the active TDLib
 * proxy via pingProxy. -1 = not measured / failed. */
int tg_proxy_ping_ms(void);
/* Own account (filled after authorization). */
int64_t tg_me_id(void);
const char* tg_me_name(void);
int tg_me_has_photo(void);
const char* tg_me_photo_path(void);

/* Profile screen data source. The client keeps one "current profile"
 * target; screens open it with the functions below and then switch to
 * ScreenId_Profile. */
void tg_open_profile_for_chat(int index);   /* from the chat list */
void tg_open_profile_for_selected_chat(void); /* from the chat screen */
void tg_open_own_profile(void);             /* from settings */
int tg_profile_is_user(void);               /* 1 = user profile, 0 = chat */
int64_t tg_profile_user_id(void);
int64_t tg_profile_chat_id(void);
const TgUser* tg_profile_user(void);
/* For chat profiles: member count / description (0/"" when unknown). */
int tg_profile_member_count(void);
const char* tg_profile_description(void);
