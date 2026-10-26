#define _GNU_SOURCE

#include "preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int russian = 1;
static int animations = 1;
static int loaded;
static int proxy_loaded;
static int proxy_enabled;
static int subscription_loaded;
static int subscription_enabled;
static int subscription_configured;
static char subscription_url[2048];
static char subscription_selected_server[PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH];
static struct {
    char id[PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH];
    char name[PREFERENCES_SUBSCRIPTION_SERVER_NAME_LENGTH];
    int ping;
} subscription_servers[PREFERENCES_SUBSCRIPTION_SERVER_COUNT];
static int subscription_server_count;
static int subscription_servers_loaded;
static int proxy_port = 1080;
static char proxy_server[PREFERENCES_PROXY_SERVER_LENGTH];
static char proxy_username[PREFERENCES_PROXY_CREDENTIAL_LENGTH];
static char proxy_password[PREFERENCES_PROXY_CREDENTIAL_LENGTH];

static const char* proxy_config_path(void) {
    const char* path = getenv("TGMUOS_PROXY_CONFIG");
    return path && *path ? path : "proxy.conf";
}

static const char* subscription_config_path(void) {
    const char* path = getenv("TGMUOS_SUBSCRIPTION_CONFIG");
    return path && *path ? path : "/root/.config/tgmuos/subscription.conf";
}

static const char* subscription_servers_path(void) {
    const char* path = getenv("TGMUOS_SERVERS_FILE");
    return path && *path ? path : "/root/.config/tgmuos/servers.tsv";
}

static void load_preferences(void) {
    if (loaded) return;
    loaded = 1;
    FILE* file = fopen("settings.conf", "r");
    if (!file) return;
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "language=en", 11) == 0) russian = 0;
        else if (strncmp(line, "language=ru", 11) == 0) russian = 1;
        else if (strncmp(line, "animations=0", 12) == 0) animations = 0;
        else if (strncmp(line, "animations=1", 12) == 0) animations = 1;
    }
    fclose(file);
}

static void save_preferences(void) {
    FILE* file = fopen("settings.conf", "w");
    if (!file) {
        perror("open settings.conf");
        return;
    }
    if (fchmod(fileno(file), 0600) < 0) {
        perror("secure settings.conf");
        fclose(file);
        return;
    }
    if (fprintf(file, "language=%s\nanimations=%d\n",
                russian ? "ru" : "en", animations) < 0) {
        perror("write settings.conf");
    }
    if (fclose(file) != 0) perror("close settings.conf");
}

static void load_proxy_preferences(void) {
    if (proxy_loaded) return;
    proxy_loaded = 1;
    FILE* file = fopen(proxy_config_path(), "r");
    if (!file) return;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) *newline = '\0';
        if (strncmp(line, "enabled=", 8) == 0) {
            proxy_enabled = strcmp(line + 8, "1") == 0;
        } else if (strncmp(line, "server=", 7) == 0) {
            snprintf(proxy_server, sizeof(proxy_server), "%.*s",
                     (int)sizeof(proxy_server) - 1, line + 7);
        } else if (strncmp(line, "port=", 5) == 0) {
            char* end = NULL;
            long value = strtol(line + 5, &end, 10);
            if (end && *end == '\0' && value > 0 && value <= 65535) {
                proxy_port = (int)value;
            }
        } else if (strncmp(line, "username=", 9) == 0) {
            snprintf(proxy_username, sizeof(proxy_username), "%.*s",
                     (int)sizeof(proxy_username) - 1, line + 9);
        } else if (strncmp(line, "password=", 9) == 0) {
            snprintf(proxy_password, sizeof(proxy_password), "%.*s",
                     (int)sizeof(proxy_password) - 1, line + 9);
        }
    }
    fclose(file);
}

static void load_subscription_preferences(void) {
    if (subscription_loaded) return;
    subscription_loaded = 1;
    FILE* file = fopen(subscription_config_path(), "r");
    if (!file) return;
    char line[4096];
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) *newline = '\0';
        if (strncmp(line, "enabled=", 8) == 0) {
            subscription_enabled = strcmp(line + 8, "1") == 0;
        } else if (strncmp(line, "url=", 4) == 0) {
            snprintf(subscription_url, sizeof(subscription_url), "%.*s",
                     (int)sizeof(subscription_url) - 1, line + 4);
        } else if (strncmp(line, "selected=", 9) == 0) {
            snprintf(subscription_selected_server,
                     sizeof(subscription_selected_server), "%.*s",
                     (int)sizeof(subscription_selected_server) - 1, line + 9);
        }
    }
    fclose(file);
    subscription_configured = subscription_url[0] != '\0';
}

static void load_subscription_servers(void) {
    if (subscription_servers_loaded) return;
    subscription_servers_loaded = 1;
    FILE* file = fopen(subscription_servers_path(), "r");
    if (!file) return;
    char line[512];
    while (subscription_server_count < PREFERENCES_SUBSCRIPTION_SERVER_COUNT &&
           fgets(line, sizeof(line), file)) {
        char* id = line;
        char* id_end = strchr(id, '\t');
        if (!id_end) continue;
        *id_end++ = '\0';
        char* ping = id_end;
        char* ping_end = strchr(ping, '\t');
        if (!ping_end) continue;
        *ping_end++ = '\0';
        char* newline = strpbrk(ping_end, "\r\n");
        if (newline) *newline = '\0';
        if (strlen(id) != PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH - 1) continue;
        char* end = NULL;
        long latency = strtol(ping, &end, 10);
        if (!end || *end || latency < -1 || latency > 60000) continue;
        int index = subscription_server_count++;
        snprintf(subscription_servers[index].id,
                 sizeof(subscription_servers[index].id), "%s", id);
        snprintf(subscription_servers[index].name,
                 sizeof(subscription_servers[index].name), "%.*s",
                 (int)sizeof(subscription_servers[index].name) - 1, ping_end);
        subscription_servers[index].ping = (int)latency;
    }
    fclose(file);
}

static int save_subscription_preferences(void) {
    FILE* file = fopen(subscription_config_path(), "w");
    if (!file) {
        perror("open subscription config");
        return 0;
    }
    if (fchmod(fileno(file), 0600) < 0) {
        perror("secure subscription config");
        fclose(file);
        return 0;
    }
    int result = fprintf(file, "enabled=%d\nurl=%s\nselected=%s\n",
                         subscription_enabled, subscription_url,
                         subscription_selected_server);
    if (result < 0) perror("write subscription config");
    if (fclose(file) != 0) {
        perror("close subscription config");
        return 0;
    }
    return result >= 0;
}

static int save_proxy_preferences(void) {
    const char* path = proxy_config_path();
    FILE* file = fopen(path, "w");
    if (!file) {
        perror("open proxy config");
        return 0;
    }
    if (fchmod(fileno(file), 0600) < 0) {
        perror("secure proxy config");
        fclose(file);
        return 0;
    }
    int result = fprintf(file, "enabled=%d\nserver=%s\nport=%d\nusername=%s\npassword=%s\n",
                         proxy_enabled, proxy_server, proxy_port,
                         proxy_username, proxy_password);
    if (result < 0) perror("write proxy config");
    if (fclose(file) != 0) {
        perror("close proxy config");
        return 0;
    }
    return result >= 0;
}

int preferences_is_russian(void) {
    load_preferences();
    return russian;
}

int preferences_animations_enabled(void) {
    load_preferences();
    return animations;
}

void preferences_toggle_language(void) {
    load_preferences();
    russian = !russian;
    save_preferences();
}

void preferences_toggle_animations(void) {
    load_preferences();
    animations = !animations;
    save_preferences();
}

int preferences_proxy_enabled(void) {
    load_proxy_preferences();
    return proxy_enabled;
}

const char* preferences_proxy_server(void) {
    load_proxy_preferences();
    return proxy_server;
}

int preferences_proxy_port(void) {
    load_proxy_preferences();
    return proxy_port;
}

const char* preferences_proxy_username(void) {
    load_proxy_preferences();
    return proxy_username;
}

const char* preferences_proxy_password(void) {
    load_proxy_preferences();
    return proxy_password;
}

int preferences_set_proxy_enabled(int enabled) {
    load_proxy_preferences();
    int previous = proxy_enabled;
    proxy_enabled = enabled != 0;
    if (save_proxy_preferences()) return 1;
    proxy_enabled = previous;
    return 0;
}

int preferences_set_proxy_details(const char* server, int port,
                                  const char* username, const char* password) {
    load_proxy_preferences();
    if (!server || !username || !password ||
        strlen(server) >= sizeof(proxy_server) ||
        strlen(username) >= sizeof(proxy_username) ||
        strlen(password) >= sizeof(proxy_password) ||
        port < 1 || port > 65535) return 0;
    char previous_server[sizeof(proxy_server)];
    char previous_username[sizeof(proxy_username)];
    char previous_password[sizeof(proxy_password)];
    memcpy(previous_server, proxy_server, sizeof(proxy_server));
    memcpy(previous_username, proxy_username, sizeof(proxy_username));
    memcpy(previous_password, proxy_password, sizeof(proxy_password));
    int previous_port = proxy_port;
    snprintf(proxy_server, sizeof(proxy_server), "%s", server);
    snprintf(proxy_username, sizeof(proxy_username), "%s", username);
    snprintf(proxy_password, sizeof(proxy_password), "%s", password);
    proxy_port = port;
    if (save_proxy_preferences()) return 1;
    memcpy(proxy_server, previous_server, sizeof(proxy_server));
    memcpy(proxy_username, previous_username, sizeof(proxy_username));
    memcpy(proxy_password, previous_password, sizeof(proxy_password));
    proxy_port = previous_port;
    return 0;
}

int preferences_subscription_enabled(void) {
    load_subscription_preferences();
    return subscription_enabled;
}

int preferences_subscription_configured(void) {
    load_subscription_preferences();
    return subscription_configured;
}

int preferences_subscription_server_count(void) {
    load_subscription_servers();
    return subscription_server_count;
}

const char* preferences_subscription_server_id(int index) {
    load_subscription_servers();
    return index >= 0 && index < subscription_server_count
        ? subscription_servers[index].id : "";
}

const char* preferences_subscription_server_name(int index) {
    load_subscription_servers();
    return index >= 0 && index < subscription_server_count
        ? subscription_servers[index].name : "";
}

int preferences_subscription_server_ping(int index) {
    load_subscription_servers();
    return index >= 0 && index < subscription_server_count
        ? subscription_servers[index].ping : -1;
}

const char* preferences_subscription_selected_server(void) {
    load_subscription_preferences();
    return subscription_selected_server;
}

int preferences_set_subscription_server(const char* server_id) {
    if (!server_id || strlen(server_id) !=
            PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH - 1) return 0;
    load_subscription_preferences();
    load_subscription_servers();
    int found = 0;
    for (int i = 0; i < subscription_server_count; ++i) {
        if (strcmp(subscription_servers[i].id, server_id) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) return 0;
    char previous[PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH];
    memcpy(previous, subscription_selected_server, sizeof(previous));
    snprintf(subscription_selected_server, sizeof(subscription_selected_server),
             "%s", server_id);
    if (save_subscription_preferences()) return 1;
    memcpy(subscription_selected_server, previous, sizeof(previous));
    return 0;
}

int preferences_set_subscription_enabled(int enabled) {
    load_subscription_preferences();
    if (enabled && !subscription_configured) return 0;
    int previous = subscription_enabled;
    subscription_enabled = enabled != 0;
    if (save_subscription_preferences()) return 1;
    subscription_enabled = previous;
    return 0;
}

const char* tr(const char* english, const char* russian_text) {
    return preferences_is_russian() ? russian_text : english;
}
