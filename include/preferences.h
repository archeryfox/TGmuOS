#pragma once

#include <stddef.h>

#define PREFERENCES_PROXY_SERVER_LENGTH 256
#define PREFERENCES_PROXY_CREDENTIAL_LENGTH 128
#define PREFERENCES_SUBSCRIPTION_SERVER_COUNT 64
#define PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH 17
#define PREFERENCES_SUBSCRIPTION_SERVER_NAME_LENGTH 128
#define PREFERENCES_SUBSCRIPTION_SERVER_HOST_LENGTH 256

int preferences_is_russian(void);
int preferences_animations_enabled(void);
void preferences_toggle_language(void);
void preferences_toggle_animations(void);
int preferences_proxy_enabled(void);
const char* preferences_proxy_server(void);
int preferences_proxy_port(void);
const char* preferences_proxy_username(void);
const char* preferences_proxy_password(void);
int preferences_set_proxy_enabled(int enabled);
int preferences_set_proxy_details(const char* server, int port,
                                  const char* username, const char* password);
int preferences_subscription_enabled(void);
int preferences_subscription_configured(void);
int preferences_set_subscription_enabled(int enabled);
int preferences_subscription_server_count(void);
const char* preferences_subscription_server_id(int index);
const char* preferences_subscription_server_name(int index);
int preferences_subscription_server_ping(int index);
const char* preferences_subscription_server_host(int index);
int preferences_subscription_server_port(int index);
int preferences_ping_all_servers(int timeout_ms, int* reachable);
const char* preferences_subscription_selected_server(void);
int preferences_set_subscription_server(const char* server_id);
int preferences_adb_usb_enabled(void);
int preferences_set_adb_usb_enabled(int enabled);
const char* tr(const char* english, const char* russian);
