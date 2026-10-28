#define _GNU_SOURCE

#include "preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>

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
typedef struct {
    char id[PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH];
    char name[PREFERENCES_SUBSCRIPTION_SERVER_NAME_LENGTH];
    char host[PREFERENCES_SUBSCRIPTION_SERVER_HOST_LENGTH];
    int port;
    int ping;
} SubscriptionServer;
static SubscriptionServer subscription_servers[PREFERENCES_SUBSCRIPTION_SERVER_COUNT];
static int subscription_server_count;
static int subscription_servers_loaded;
static int proxy_port = 1080;
static char proxy_server[PREFERENCES_PROXY_SERVER_LENGTH];
static char proxy_username[PREFERENCES_PROXY_CREDENTIAL_LENGTH];
static char proxy_password[PREFERENCES_PROXY_CREDENTIAL_LENGTH];
static int adb_usb_enabled;
static int adb_loaded;

static void load_adb_preferences(void) {
    if (adb_loaded) return;
    adb_loaded = 1;
    FILE* file = fopen("settings.conf", "r");
    if (!file) return;
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "adb_usb=1", 10) == 0) adb_usb_enabled = 1;
        else if (strncmp(line, "adb_usb=0", 10) == 0) adb_usb_enabled = 0;
    }
    fclose(file);
}

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
        else if (strncmp(line, "adb_usb=1", 10) == 0) adb_usb_enabled = 1;
        else if (strncmp(line, "adb_usb=0", 10) == 0) adb_usb_enabled = 0;
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
    if (fprintf(file, "language=%s\nanimations=%d\nadb_usb=%d\n",
                russian ? "ru" : "en", animations, adb_usb_enabled) < 0) {
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
    char line[768];
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
        char* name = ping_end;
        char* name_end = strchr(name, '\t');
        char* host = NULL;
        char* port = NULL;
        if (name_end) {
            *name_end++ = '\0';
            host = name_end;
            char* host_end = strchr(host, '\t');
            if (host_end) {
                *host_end++ = '\0';
                port = host_end;
                char* port_end = strpbrk(port, "\t\r\n");
                if (port_end) *port_end = '\0';
            }
        }
        char* newline = strpbrk(name, "\r\n");
        if (newline) *newline = '\0';
        if (strlen(id) != PREFERENCES_SUBSCRIPTION_SERVER_ID_LENGTH - 1) continue;
        char* end = NULL;
        long latency = strtol(ping, &end, 10);
        if (!end || *end || latency < -1 || latency > 60000) continue;
        int index = subscription_server_count++;
        snprintf(subscription_servers[index].id,
                 sizeof(subscription_servers[index].id), "%.*s",
                 (int)sizeof(subscription_servers[index].id) - 1, id);
        snprintf(subscription_servers[index].name,
                 sizeof(subscription_servers[index].name), "%.*s",
                 (int)sizeof(subscription_servers[index].name) - 1, name);
        subscription_servers[index].host[0] = '\0';
        subscription_servers[index].port = 0;
        if (host && *host) {
            snprintf(subscription_servers[index].host,
                     sizeof(subscription_servers[index].host), "%.*s",
                     (int)sizeof(subscription_servers[index].host) - 1, host);
            if (port && *port) {
                long value = strtol(port, &end, 10);
                if (end && *end == '\0' && value > 0 && value <= 65535) {
                    subscription_servers[index].port = (int)value;
                }
            }
        }
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

const char* preferences_subscription_server_host(int index) {
    load_subscription_servers();
    return index >= 0 && index < subscription_server_count
        ? subscription_servers[index].host : "";
}

int preferences_subscription_server_port(int index) {
    load_subscription_servers();
    return index >= 0 && index < subscription_server_count
        ? subscription_servers[index].port : 0;
}

/* TCP connect latency in milliseconds, or -1 when unreachable. */
static int tcp_connect_ping_ms(const char* host, int port, int timeout_ms) {
    if (!host || !*host || port <= 0 || port > 65535) return -1;
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo* addresses = NULL;
    if (getaddrinfo(host, port_text, &hints, &addresses) != 0) return -1;
    int latency = -1;
    for (struct addrinfo* item = addresses; item && latency < 0;
         item = item->ai_next) {
        int fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            continue;
        }
        struct timespec started, finished;
        clock_gettime(CLOCK_MONOTONIC, &started);
        int in_progress = 0;
        if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
            latency = 0;
        } else if (errno == EINPROGRESS) {
            in_progress = 1;
        }
        if (in_progress) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(fd, &writable);
            struct timeval timeout = {
                timeout_ms / 1000,
                (long)(timeout_ms % 1000) * 1000,
            };
            if (select(fd + 1, NULL, &writable, NULL, &timeout) > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
                        error == 0) {
                    latency = 0;
                }
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &finished);
        close(fd);
        if (latency == 0) {
            long elapsed = (finished.tv_sec - started.tv_sec) * 1000 +
                           (finished.tv_nsec - started.tv_nsec) / 1000000;
            latency = elapsed < 1 ? 1 : (int)elapsed;
        }
    }
    freeaddrinfo(addresses);
    return latency;
}

/* Stable insertion sort: reachable servers by ascending ping, offline last. */
static void sort_subscription_servers_by_ping(void) {
    for (int i = 1; i < subscription_server_count; ++i) {
        SubscriptionServer current = subscription_servers[i];
        int j = i - 1;
        while (j >= 0) {
            int a = subscription_servers[j].ping;
            int b = current.ping;
            int swap = (a < 0 && b >= 0) ||
                       (a >= 0 && b >= 0 && a > b);
            if (!swap) break;
            subscription_servers[j + 1] = subscription_servers[j];
            --j;
        }
        subscription_servers[j + 1] = current;
    }
}

static int save_subscription_servers(void) {
    const char* path = subscription_servers_path();
    char temporary[512];
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE* file = fopen(temporary, "w");
    if (!file) {
        perror("open servers list");
        return 0;
    }
    if (fchmod(fileno(file), 0600) < 0) {
        perror("secure servers list");
        fclose(file);
        return 0;
    }
    for (int i = 0; i < subscription_server_count; ++i) {
        if (fprintf(file, "%s\t%d\t%s\t%s\t%d\n",
                    subscription_servers[i].id,
                    subscription_servers[i].ping,
                    subscription_servers[i].name,
                    subscription_servers[i].host,
                    subscription_servers[i].port) < 0) {
            perror("write servers list");
            fclose(file);
            return 0;
        }
    }
    if (fclose(file) != 0) {
        perror("close servers list");
        return 0;
    }
    if (rename(temporary, path) != 0) {
        perror("replace servers list");
        return 0;
    }
    return 1;
}

int preferences_ping_all_servers(int timeout_ms, int* reachable) {
    load_subscription_servers();
    int count = 0;
    if (reachable) *reachable = 0;
    for (int i = 0; i < subscription_server_count; ++i) {
        int latency = tcp_connect_ping_ms(subscription_servers[i].host,
                                          subscription_servers[i].port,
                                          timeout_ms);
        subscription_servers[i].ping = latency;
        if (latency >= 0) {
            ++count;
        }
    }
    sort_subscription_servers_by_ping();
    if (!save_subscription_servers()) return -1;
    if (reachable) *reachable = count;
    return subscription_server_count;
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

int preferences_adb_usb_enabled(void) {
    load_preferences();
    load_adb_preferences();
    return adb_usb_enabled;
}

int preferences_set_adb_usb_enabled(int enabled) {
    load_preferences();
    load_adb_preferences();
    adb_usb_enabled = enabled != 0;
    save_preferences();
    return 1;
}
