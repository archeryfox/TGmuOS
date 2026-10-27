#define _GNU_SOURCE

#include "telegram.h"
#include "preferences.h"
#include "qr.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef void (*TdSend)(int, const char*);
typedef const char* (*TdReceive)(double);
typedef int (*TdCreateClientId)(void);
typedef const char* (*TdExecute)(const char*);

typedef enum {
    JsonObject,
    JsonArray,
    JsonString,
    JsonPrimitive
} JsonType;

typedef struct {
    int start;
    int end;
    int next;
    JsonType type;
} JsonToken;

#define JSON_TOKEN_LIMIT 4096

static struct {
    void* library;
    TdSend send;
    TdReceive receive;
    TdCreateClientId create_client_id;
    TdExecute execute;
    int client_id;
    int running;
    int closed;
    int requested_chats;
    int older_history_pending;
    int older_history_exhausted;
    int proxy_setup_pending;
    int proxy_apply_pending;
    int awaiting_initial_proxy;
    char proxy_server[PREFERENCES_PROXY_SERVER_LENGTH];
    int proxy_port;
    char proxy_username[PREFERENCES_PROXY_CREDENTIAL_LENGTH];
    char proxy_password[PREFERENCES_PROXY_CREDENTIAL_LENGTH];
    int64_t selected_chat;
    TgState state;
    TgState pending_auth_state;
    char prompt[TG_TEXT_LENGTH];
    char qr_link[192];
    unsigned char qr_matrix[37][37];
    int qr_matrix_status;
    int qr_request_pending;
    TgState qr_return_state;
    char qr_return_status[TG_TEXT_LENGTH];
    char qr_return_prompt[TG_TEXT_LENGTH];
    char status[TG_TEXT_LENGTH];
    char pending_auth_prompt[TG_TEXT_LENGTH];
    char pending_auth_status[TG_TEXT_LENGTH];
    char error[TG_TEXT_LENGTH];
    char api_hash[33];
    char database_key[65];
    int api_id;
    TgChat chats[TG_MAX_CHATS];
    int chat_count;
    TgMessage messages[TG_MAX_MESSAGES];
    int message_count;
} client;

static const char* json_source;
static JsonToken json_tokens[JSON_TOKEN_LIMIT];
static int json_token_count;
static int json_position;
static int next_request_id = 1;
static struct {
    char id[32];
    char method[64];
} pending_requests[64];
static unsigned pending_request_index;
static char self_test_request[8192];

static void self_test_send(int client_id, const char* request) {
    (void)client_id;
    snprintf(self_test_request, sizeof(self_test_request), "%s", request);
}

static void set_error(const char* message) {
    snprintf(client.error, sizeof(client.error), "%s", message);
    snprintf(client.status, sizeof(client.status), "Telegram client error");
    client.state = TgStateError;
    fprintf(stderr, "TDLib: %s\n", message);
}

static void set_status(TgState state, const char* status, const char* prompt) {
    client.state = state;
    snprintf(client.status, sizeof(client.status), "%s", status);
    snprintf(client.prompt, sizeof(client.prompt), "%s", prompt ? prompt : "");
}

static int parse_json_value(void) {
    while (json_source[json_position] == ' ' || json_source[json_position] == '\n' ||
           json_source[json_position] == '\r' || json_source[json_position] == '\t') {
        ++json_position;
    }
    if (!json_source[json_position] || json_token_count >= JSON_TOKEN_LIMIT) return -1;

    int token = json_token_count++;
    JsonToken* item = &json_tokens[token];
    item->start = json_position;
    item->next = token + 1;
    char first = json_source[json_position];
    if (first == '{' || first == '[') {
        item->type = first == '{' ? JsonObject : JsonArray;
        char close = first == '{' ? '}' : ']';
        ++json_position;
        for (;;) {
            while (json_source[json_position] == ' ' || json_source[json_position] == '\n' ||
                   json_source[json_position] == '\r' || json_source[json_position] == '\t' ||
                   json_source[json_position] == ',' || json_source[json_position] == ':') {
                ++json_position;
            }
            if (json_source[json_position] == close) {
                ++json_position;
                break;
            }
            if (!json_source[json_position]) return -1;
            if (parse_json_value() < 0) return -1;
        }
    } else if (first == '"') {
        item->type = JsonString;
        ++json_position;
        while (json_source[json_position]) {
            if (json_source[json_position] == '\\') {
                json_position += json_source[json_position + 1] ? 2 : 1;
            } else if (json_source[json_position++] == '"') {
                break;
            }
        }
        if (json_source[json_position - 1] != '"') return -1;
    } else {
        item->type = JsonPrimitive;
        while (json_source[json_position] && json_source[json_position] != ',' &&
               json_source[json_position] != '}' && json_source[json_position] != ']' &&
               json_source[json_position] != ' ' && json_source[json_position] != '\n' &&
               json_source[json_position] != '\r' && json_source[json_position] != '\t') {
            ++json_position;
        }
    }
    item->end = json_position;
    item->next = json_token_count;
    return token;
}

static int json_parse(const char* source) {
    json_source = source;
    json_position = 0;
    json_token_count = 0;
    if (parse_json_value() != 0) return 0;
    while (json_source[json_position] == ' ' || json_source[json_position] == '\n' ||
           json_source[json_position] == '\r' || json_source[json_position] == '\t') {
        ++json_position;
    }
    return json_source[json_position] == '\0';
}

static int token_equals(int token, const char* value) {
    if (token < 0 || json_tokens[token].type != JsonString) return 0;
    int start = json_tokens[token].start + 1;
    int end = json_tokens[token].end - 1;
    return end - start == (int)strlen(value) &&
           memcmp(json_source + start, value, (size_t)(end - start)) == 0;
}

static int json_member(int object, const char* name) {
    if (object < 0 || json_tokens[object].type != JsonObject) return -1;
    int token = object + 1;
    while (token < json_tokens[object].next) {
        int value = token + 1;
        if (token_equals(token, name)) return value;
        token = json_tokens[value].next;
    }
    return -1;
}

static int json_array_item(int array, int index) {
    if (array < 0 || json_tokens[array].type != JsonArray) return -1;
    int token = array + 1;
    while (index-- > 0 && token < json_tokens[array].next) {
        token = json_tokens[token].next;
    }
    return token < json_tokens[array].next ? token : -1;
}

static int json_array_size(int array) {
    if (array < 0 || json_tokens[array].type != JsonArray) return 0;
    int count = 0;
    for (int token = array + 1; token < json_tokens[array].next;
         token = json_tokens[token].next) ++count;
    return count;
}

static const char* request_method_for_response(void) {
    int extra = json_member(0, "@extra");
    if (extra < 0) return NULL;
    const char* id = json_source + json_tokens[extra].start;
    size_t id_length = (size_t)(json_tokens[extra].end -
                                json_tokens[extra].start);
    for (size_t i = 0; i < sizeof(pending_requests) /
                            sizeof(pending_requests[0]); ++i) {
        if (strlen(pending_requests[i].id) == id_length &&
            memcmp(pending_requests[i].id, id, id_length) == 0) {
            return pending_requests[i].method;
        }
    }
    return NULL;
}

static void token_string(int token, char* output, size_t capacity) {
    if (capacity == 0) return;
    output[0] = '\0';
    if (token < 0 || json_tokens[token].type != JsonString) return;

    int position = json_tokens[token].start + 1;
    int end = json_tokens[token].end - 1;
    size_t written = 0;
    while (position < end && written + 1 < capacity) {
        unsigned char value = (unsigned char)json_source[position++];
        if (value == '\\' && position < end) {
            char escaped = json_source[position++];
            if (escaped == 'n') value = '\n';
            else if (escaped == 'r') value = '\r';
            else if (escaped == 't') value = '\t';
            else if (escaped == 'b') value = '\b';
            else if (escaped == 'f') value = '\f';
            else if (escaped == 'u' && position + 4 <= end) {
                unsigned codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    char digit = json_source[position++];
                    codepoint = codepoint * 16u + (digit >= '0' && digit <= '9'
                        ? (unsigned)(digit - '0')
                        : digit >= 'a' && digit <= 'f' ? (unsigned)(digit - 'a' + 10)
                        : digit >= 'A' && digit <= 'F' ? (unsigned)(digit - 'A' + 10) : 0);
                }
                if (codepoint <= 0x7f && written + 1 < capacity) {
                    output[written++] = (char)codepoint;
                } else if (codepoint <= 0x7ff && written + 2 < capacity) {
                    output[written++] = (char)(0xc0 | (codepoint >> 6));
                    output[written++] = (char)(0x80 | (codepoint & 0x3f));
                } else if (written + 3 < capacity) {
                    output[written++] = (char)(0xe0 | (codepoint >> 12));
                    output[written++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
                    output[written++] = (char)(0x80 | (codepoint & 0x3f));
                }
                continue;
            } else {
                value = (unsigned char)escaped;
            }
        }
        output[written++] = (char)value;
    }
    output[written] = '\0';
}

static int64_t token_integer(int token) {
    if (token < 0) return 0;
    char value[32];
    int length = json_tokens[token].end - json_tokens[token].start;
    int start = json_tokens[token].start;
    if (json_tokens[token].type == JsonString) {
        ++start;
        length -= 2;
    }
    if (length <= 0 || length >= (int)sizeof(value)) return 0;
    memcpy(value, json_source + start, (size_t)length);
    value[length] = '\0';
    return strtoll(value, NULL, 10);
}

static int json_escape(const char* input, char* output, size_t capacity) {
    size_t position = 0;
    for (const unsigned char* p = (const unsigned char*)input; *p; ++p) {
        const char* escaped = NULL;
        char control[7];
        if (*p == '"' || *p == '\\') {
            control[0] = '\\';
            control[1] = (char)*p;
            control[2] = '\0';
            escaped = control;
        } else if (*p < 0x20) {
            snprintf(control, sizeof(control), "\\u%04x", *p);
            escaped = control;
        }
        const char* part = escaped ? escaped : (const char*)p;
        size_t length = escaped ? strlen(escaped) : 1;
        if (position + length >= capacity) return 0;
        memcpy(output + position, part, length);
        position += length;
    }
    if (position >= capacity) return 0;
    output[position] = '\0';
    return 1;
}

static int base64_encode(const unsigned char* input, size_t length,
                         char* output, size_t capacity) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t needed = ((length + 2) / 3) * 4;
    if (needed + 1 > capacity) return 0;
    size_t out = 0;
    for (size_t i = 0; i < length; i += 3) {
        unsigned value = (unsigned)input[i] << 16;
        if (i + 1 < length) value |= (unsigned)input[i + 1] << 8;
        if (i + 2 < length) value |= input[i + 2];
        output[out++] = alphabet[(value >> 18) & 0x3f];
        output[out++] = alphabet[(value >> 12) & 0x3f];
        output[out++] = i + 1 < length ? alphabet[(value >> 6) & 0x3f] : '=';
        output[out++] = i + 2 < length ? alphabet[value & 0x3f] : '=';
    }
    output[out] = '\0';
    return 1;
}

static void send_request(const char* request) {
    if (!client.send || client.client_id <= 0 || !request) return;
    const char* method_start = strstr(request, "\"@type\":\"");
    const char* method_end = method_start ? strchr(method_start + 9, '"') : NULL;
    char method[64] = "unknown";
    if (method_start && method_end) {
        size_t length = (size_t)(method_end - (method_start + 9));
        if (length >= sizeof(method)) length = sizeof(method) - 1;
        memcpy(method, method_start + 9, length);
        method[length] = '\0';
    }

    const char* extra_start = strstr(request, "\"@extra\":");
    if (extra_start) {
        extra_start += sizeof("\"@extra\":") - 1;
        const char* extra_end = extra_start;
        while (*extra_end && *extra_end != ',' && *extra_end != '}') ++extra_end;
        size_t length = (size_t)(extra_end - extra_start);
        while (length > 0 && extra_start[length - 1] == ' ') --length;
        if (length >= sizeof(pending_requests[0].id)) {
            length = sizeof(pending_requests[0].id) - 1;
        }
        unsigned index = pending_request_index++ %
                         (sizeof(pending_requests) / sizeof(pending_requests[0]));
        memcpy(pending_requests[index].id, extra_start, length);
        pending_requests[index].id[length] = '\0';
        snprintf(pending_requests[index].method,
                 sizeof(pending_requests[index].method), "%s", method);
        client.send(client.client_id, request);
        return;
    }

    char tracked_request[8192];
    size_t length = strlen(request);
    if (length + 40 >= sizeof(tracked_request) ||
        length == 0 || request[length - 1] != '}') {
        fprintf(stderr, "TDLib request %s was not sent: request is too large\n",
                method);
        return;
    }
    int request_id = next_request_id++;
    char extra[32];
    snprintf(extra, sizeof(extra), "\"tgmuos-%d\"", request_id);
    memcpy(tracked_request, request, length - 1);
    int added = snprintf(tracked_request + length - 1,
                         sizeof(tracked_request) - (length - 1),
                         ",\"@extra\":%s}", extra);
    if (added < 0 || (size_t)added >= sizeof(tracked_request) - (length - 1)) {
        fprintf(stderr, "TDLib request %s was not sent: cannot track request\n",
                method);
        return;
    }
    unsigned index = pending_request_index++ %
                     (sizeof(pending_requests) / sizeof(pending_requests[0]));
    snprintf(pending_requests[index].id, sizeof(pending_requests[index].id),
             "%s", extra);
    snprintf(pending_requests[index].method,
             sizeof(pending_requests[index].method), "%s", method);
    client.send(client.client_id, tracked_request);
    memset(tracked_request, 0, length + (size_t)added);
}

static int prepare_proxy(void) {
    const char* server = preferences_proxy_server();
    const char* username = preferences_proxy_username();
    const char* password = preferences_proxy_password();
    int port = preferences_proxy_port();
    fprintf(stderr, "TDLib: prepare_proxy server=%s port=%d username=%s\n",
            server, port, username);
    if (!server[0] || port < 1 || port > 65535 ||
        strlen(server) >= sizeof(client.proxy_server) ||
        strlen(username) >= sizeof(client.proxy_username) ||
        strlen(password) >= sizeof(client.proxy_password)) {
        snprintf(client.error, sizeof(client.error),
                 "Set a valid SOCKS5 server and port first");
        return 0;
    }
    snprintf(client.proxy_server, sizeof(client.proxy_server), "%s", server);
    snprintf(client.proxy_username, sizeof(client.proxy_username), "%s", username);
    snprintf(client.proxy_password, sizeof(client.proxy_password), "%s", password);
    client.proxy_port = port;
    client.proxy_setup_pending = 1;
    send_request("{\"@type\":\"getProxies\"}");
    return 1;
}

static void configure_proxy(int proxy_id) {
    char escaped_server[PREFERENCES_PROXY_SERVER_LENGTH * 6];
    char escaped_username[PREFERENCES_PROXY_CREDENTIAL_LENGTH * 6];
    char escaped_password[PREFERENCES_PROXY_CREDENTIAL_LENGTH * 6];
    if (!json_escape(client.proxy_server, escaped_server, sizeof(escaped_server)) ||
        !json_escape(client.proxy_username, escaped_username, sizeof(escaped_username)) ||
        !json_escape(client.proxy_password, escaped_password, sizeof(escaped_password))) {
        client.proxy_setup_pending = 0;
        snprintf(client.error, sizeof(client.error),
                 "SOCKS5 proxy settings contain invalid text");
        return;
    }
    char request[4096];
    if (proxy_id > 0) {
        snprintf(request, sizeof(request),
                 "{\"@type\":\"editProxy\",\"proxy_id\":%d,\"server\":\"%s\","
                 "\"port\":%d,\"enable\":true,\"type\":{\"@type\":\"proxyTypeSocks5\","
                 "\"username\":\"%s\",\"password\":\"%s\"}}",
                 proxy_id, escaped_server, client.proxy_port,
                 escaped_username, escaped_password);
    } else {
        snprintf(request, sizeof(request),
                 "{\"@type\":\"addProxy\",\"server\":\"%s\",\"port\":%d,"
                 "\"enable\":true,\"type\":{\"@type\":\"proxyTypeSocks5\","
                 "\"username\":\"%s\",\"password\":\"%s\"}}",
                 escaped_server, client.proxy_port, escaped_username, escaped_password);
    }
    client.proxy_setup_pending = 0;
    client.proxy_apply_pending = 1;
    send_request(request);
}

#define MTPROXY_MAX 8
typedef struct {
    char name[64];
    char server[PREFERENCES_PROXY_SERVER_LENGTH];
    int port;
    char secret[64];
} MtprotoProxy;
static MtprotoProxy mtproto_proxies[MTPROXY_MAX];
static int mtproto_proxy_count = -1;

/* Lines: name<TAB>server<TAB>port<TAB>secret, secret in base64url form
 * (as in t.me/webproxy links). Secrets are never logged. */
static void load_mtproto_proxies(void) {
    if (mtproto_proxy_count >= 0) return;
    mtproto_proxy_count = 0;
    const char* path = getenv("TGMUOS_MTPROTO_CONFIG");
    if (!path || !*path) path = "/root/.config/tgmuos/mtproto.conf";
    FILE* file = fopen(path, "r");
    if (!file) return;
    char line[768];
    while (mtproto_proxy_count < MTPROXY_MAX && fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) *newline = '\0';
        char* name = line;
        char* server = strchr(name, '\t');
        if (!server) continue;
        *server++ = '\0';
        char* port = strchr(server, '\t');
        if (!port) continue;
        *port++ = '\0';
        char* secret = strchr(port, '\t');
        if (!secret) continue;
        *secret++ = '\0';
        if (!*name || !*server || !*secret) continue;
        char* end = NULL;
        long value = strtol(port, &end, 10);
        if (!end || *end || value < 1 || value > 65535) continue;
        size_t length = strlen(secret);
        if (length + 4 >= sizeof(mtproto_proxies[0].secret)) continue;
        char base64[sizeof(mtproto_proxies[0].secret)];
        size_t out = 0;
        int valid = 1;
        for (size_t i = 0; i < length; ++i) {
            char c = secret[i];
            if (c == '-') c = '+';
            else if (c == '_') c = '/';
            else if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'z') &&
                     !(c >= 'A' && c <= 'Z')) {
                valid = 0;
                break;
            }
            base64[out++] = c;
        }
        if (!valid) continue;
        while (out % 4) base64[out++] = '=';
        base64[out] = '\0';
        MtprotoProxy* item = &mtproto_proxies[mtproto_proxy_count++];
        snprintf(item->name, sizeof(item->name), "%.*s",
                 (int)sizeof(item->name) - 1, name);
        snprintf(item->server, sizeof(item->server), "%.*s",
                 (int)sizeof(item->server) - 1, server);
        item->port = (int)value;
        snprintf(item->secret, sizeof(item->secret), "%s", base64);
    }
    fclose(file);
}

/* Offer MTProto proxies from mtproto.conf to TDLib. The first one is enabled
 * only when the SOCKS subscription proxy is not active. */
static void add_mtproto_proxies(void) {
    load_mtproto_proxies();
    if (mtproto_proxy_count <= 0) return;
    int enable_first = !preferences_proxy_enabled();
    for (int i = 0; i < mtproto_proxy_count; ++i) {
        MtprotoProxy* item = &mtproto_proxies[i];
        char escaped_server[PREFERENCES_PROXY_SERVER_LENGTH * 6];
        char escaped_secret[sizeof(item->secret) * 6];
        if (!json_escape(item->server, escaped_server, sizeof(escaped_server)) ||
            !json_escape(item->secret, escaped_secret, sizeof(escaped_secret))) {
            continue;
        }
        int enable = enable_first && i == 0;
        char request[2048];
        snprintf(request, sizeof(request),
                 "{\"@type\":\"addProxy\",\"server\":\"%s\",\"port\":%d,"
                 "\"enable\":%s,\"type\":{\"@type\":\"proxyTypeMtproto\","
                 "\"secret\":\"%s\"}}",
                 escaped_server, item->port, enable ? "true" : "false",
                 escaped_secret);
        send_request(request);
        fprintf(stderr, "TDLib: MTProto proxy added: %s (%s:%d) enable=%d\n",
                item->name, item->server, item->port, enable);
    }
}

static int read_credentials(void) {
    const char* api_id = getenv("TG_API_ID");
    const char* api_hash = getenv("TG_API_HASH");
    char file_api_id[32] = {0};
    char file_api_hash[33] = {0};
    if (!api_id || !api_hash) {
        const char* config_path = getenv("TGMUOS_CONFIG");
        if (!config_path || !*config_path) config_path = "telegram.conf";
        FILE* file = fopen(config_path, "r");
        if (!file) return 0;
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            char* newline = strpbrk(line, "\r\n");
            if (newline) *newline = '\0';
            if (strncmp(line, "api_id=", 7) == 0) {
                size_t length = strlen(line + 7);
                if (length >= sizeof(file_api_id)) {
                    fclose(file);
                    return 0;
                }
                memcpy(file_api_id, line + 7, length + 1);
            } else if (strncmp(line, "api_hash=", 9) == 0) {
                size_t length = strlen(line + 9);
                if (length >= sizeof(file_api_hash)) {
                    fclose(file);
                    return 0;
                }
                memcpy(file_api_hash, line + 9, length + 1);
            }
        }
        fclose(file);
        if (!api_id) api_id = file_api_id;
        if (!api_hash) api_hash = file_api_hash;
    }
    if (!api_id || !api_hash || strlen(api_hash) != 32) return 0;
    for (size_t i = 0; i < 32; ++i) {
        char value = api_hash[i];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
              (value >= 'A' && value <= 'F'))) return 0;
    }
    char* end = NULL;
    long value = strtol(api_id, &end, 10);
    if (!end || *end || value <= 0 || value > 2147483647) return 0;
    client.api_id = (int)value;
    snprintf(client.api_hash, sizeof(client.api_hash), "%s", api_hash);
    return 1;
}

static int load_database_key(void) {
    int fd = open("data/tdlib.key", O_RDONLY);
    if (fd >= 0) {
        if (fchmod(fd, 0600) < 0) {
            close(fd);
            set_error("Cannot secure data/tdlib.key permissions");
            return 0;
        }
        ssize_t length = read(fd, client.database_key, sizeof(client.database_key));
        close(fd);
        if (length != 64) {
            set_error("Invalid data/tdlib.key; refusing to replace the session key");
            return 0;
        }
        client.database_key[64] = '\0';
        for (int i = 0; i < 64; ++i) {
            char value = client.database_key[i];
            if (!((value >= '0' && value <= '9') ||
                  (value >= 'a' && value <= 'f'))) {
                set_error("Invalid data/tdlib.key; refusing to replace the session key");
                return 0;
            }
        }
        return 1;
    }
    if (errno != ENOENT) {
        set_error("Cannot read data/tdlib.key");
        return 0;
    }

    unsigned char random_bytes[32];
    int random_fd = open("/dev/urandom", O_RDONLY);
    if (random_fd < 0) {
        set_error("Cannot open /dev/urandom for the local database key");
        return 0;
    }
    size_t bytes_read = 0;
    while (bytes_read < sizeof(random_bytes)) {
        ssize_t count = read(random_fd, random_bytes + bytes_read,
                             sizeof(random_bytes) - bytes_read);
        if (count <= 0) {
            close(random_fd);
            set_error("Cannot generate local database key");
            return 0;
        }
        bytes_read += (size_t)count;
    }
    close(random_fd);

    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        client.database_key[i * 2] = hex[random_bytes[i] >> 4];
        client.database_key[i * 2 + 1] = hex[random_bytes[i] & 0x0f];
    }
    client.database_key[64] = '\0';

    fd = open("data/tdlib.key", O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0 && errno == EEXIST) return load_database_key();
    if (fd < 0) {
        set_error("Cannot create private data/tdlib.key");
        return 0;
    }
    size_t bytes_written = 0;
    while (bytes_written < 64) {
        ssize_t count = write(fd, client.database_key + bytes_written,
                              64 - bytes_written);
        if (count <= 0) {
            close(fd);
            set_error("Cannot save local database key");
            return 0;
        }
        bytes_written += (size_t)count;
    }
    int sync_result = fsync(fd);
    int close_result = close(fd);
    if (sync_result < 0 || close_result < 0) {
        set_error("Cannot persist local database key");
        return 0;
    }
    return 1;
}

static void load_library(void) {
    const char* override = getenv("TGMUOS_TDLIB");
    if (override && *override) client.library = dlopen(override, RTLD_NOW | RTLD_LOCAL);
    if (!client.library) client.library = dlopen("./lib/libtdjson.so", RTLD_NOW | RTLD_LOCAL);
    if (!client.library) client.library = dlopen("libtdjson.so", RTLD_NOW | RTLD_LOCAL);
    if (!client.library) {
        snprintf(client.error, sizeof(client.error), "TDLib not installed (libtdjson.so)");
        set_status(TgStateUnavailable, "TDLib library not found", "");
        fprintf(stderr, "%s\n", client.error);
        return;
    }

    void* symbol = dlsym(client.library, "td_send");
    memcpy(&client.send, &symbol, sizeof(client.send));
    symbol = dlsym(client.library, "td_receive");
    memcpy(&client.receive, &symbol, sizeof(client.receive));
    symbol = dlsym(client.library, "td_create_client_id");
    memcpy(&client.create_client_id, &symbol, sizeof(client.create_client_id));
    symbol = dlsym(client.library, "td_execute");
    memcpy(&client.execute, &symbol, sizeof(client.execute));
    if (!client.send || !client.receive || !client.create_client_id || !client.execute) {
        set_error("TDLib JSON C API symbols are missing");
        return;
    }
}

void tg_start(void) {
    memset(&client, 0, sizeof(client));
    set_status(TgStateUnavailable, "Not connected", "");
    load_library();
    if (!client.library || client.state == TgStateError) return;
    if (!read_credentials()) {
        set_status(TgStateUnavailable, "API credentials missing", "");
        snprintf(client.error, sizeof(client.error),
                 "Set api_id and api_hash in telegram.conf");
        return;
    }

    if (mkdir("data", 0700) < 0 && errno != EEXIST) {
        set_error("Cannot create data directory");
        return;
    }
    if (mkdir("data/tdlib", 0700) < 0 && errno != EEXIST) {
        set_error("Cannot create TDLib database directory");
        return;
    }
    if (mkdir("data/tdlib/files", 0700) < 0 && errno != EEXIST) {
        set_error("Cannot create TDLib files directory");
        return;
    }
    if (!load_database_key()) return;

    client.client_id = client.create_client_id();
    if (client.client_id <= 0) {
        set_error("Cannot create TDLib client");
        return;
    }
    client.running = 1;
    set_status(TgStateInitializing, "Connecting to Telegram…", "");
    if (client.execute) client.execute("{\"@type\":\"setLogVerbosityLevel\",\"new_verbosity_level\":1}");
    if (preferences_proxy_enabled()) {
        client.awaiting_initial_proxy = 1;
        if (!prepare_proxy()) {
            set_error("SOCKS5 proxy is enabled but its settings are invalid");
            return;
        }
    } else {
        send_request("{\"@type\":\"getAuthorizationState\"}");
    }
}

static void request_chats(void) {
    send_request("{\"@type\":\"loadChats\",\"chat_list\":{\"@type\":\"chatListMain\"},\"limit\":50}");
    client.requested_chats = 1;
}

static void handle_authorization(const char* state) {
    client.pending_auth_state = TgStateUnavailable;
    client.error[0] = '\0';
    client.qr_request_pending = 0;
    if (strcmp(state, "authorizationStateWaitTdlibParameters") == 0) {
        char request[1024];
        char encoded_database_key[128];
        if (!base64_encode((const unsigned char*)client.database_key,
                           strlen(client.database_key), encoded_database_key,
                           sizeof(encoded_database_key))) {
            set_error("Cannot encode local database key");
            return;
        }
        snprintf(request, sizeof(request),
                 "{\"@type\":\"setTdlibParameters\",\"parameters\":{"
                 "\"@type\":\"tdlibParameters\",\"use_test_dc\":false,"
                 "\"database_directory\":\"data/tdlib\",\"files_directory\":\"data/tdlib/files\","
                 "\"database_encryption_key\":\"%s\",\"use_message_database\":true,"
                 "\"use_file_database\":true,\"use_chat_info_database\":true,"
                 "\"use_secret_chats\":false,\"api_id\":%d,\"api_hash\":\"%s\","
                 "\"system_language_code\":\"%s\",\"device_model\":\"muOS handheld\","
                 "\"system_version\":\"Linux\",\"application_version\":\"tgmuos\"}}",
                 encoded_database_key, client.api_id, client.api_hash,
                 preferences_is_russian() ? "ru" : "en");
        send_request(request);
        set_status(TgStateInitializing, "Initializing Telegram database…", "");
    } else if (strcmp(state, "authorizationStateWaitEncryptionKey") == 0) {
        char request[1024];
        char encoded_database_key[128];
        if (!base64_encode((const unsigned char*)client.database_key,
                           strlen(client.database_key), encoded_database_key,
                           sizeof(encoded_database_key))) {
            set_error("Cannot encode local database key for encryption");
            return;
        }
        snprintf(request, sizeof(request),
                 "{\"@type\":\"checkDatabaseEncryptionKey\",\"encryption_key\":\"%s\"}",
                 encoded_database_key);
        send_request(request);
        set_status(TgStateInitializing, "Initializing Telegram database…", "");
        fprintf(stderr, "TDLib: sent checkDatabaseEncryptionKey\n");
    } else if (strcmp(state, "authorizationStateWaitPhoneNumber") == 0) {
        set_status(TgStatePhone, "Sign in to Telegram", "Enter your phone number");
    } else if (strcmp(state, "authorizationStateWaitCode") == 0) {
        set_status(TgStateCode, "Telegram login", "Enter the code sent by Telegram");
    } else if (strcmp(state, "authorizationStateWaitPassword") == 0) {
        set_status(TgStatePassword, "Two-step verification", "Enter your Telegram password");
    } else if (strcmp(state, "authorizationStateWaitOtherDeviceConfirmation") == 0) {
        int link = json_member(json_member(0, "authorization_state"), "link");
        token_string(link, client.qr_link, sizeof(client.qr_link));
        client.qr_matrix_status = 0;
        if (client.qr_link[0]) {
            set_status(TgStateQr, "Confirm login on another device", "");
        } else {
            set_error("Telegram did not provide a QR login link");
        }
    } else if (strcmp(state, "authorizationStateReady") == 0) {
        set_status(TgStateReady, "Connected to Telegram", "");
        if (!client.requested_chats) request_chats();
    } else if (strcmp(state, "authorizationStateLoggingOut") == 0) {
        set_status(TgStateInitializing, "Signing out…", "");
    } else if (strcmp(state, "authorizationStateClosing") == 0) {
        set_status(TgStateInitializing, "Closing Telegram session…", "");
    } else if (strcmp(state, "authorizationStateClosed") == 0) {
        client.closed = 1;
        set_status(TgStateUnavailable, "Telegram session closed", "");
    }
}

static void update_chat(int object) {
    int64_t id = token_integer(json_member(object, "id"));
    if (!id) return;
    int index = 0;
    while (index < client.chat_count && client.chats[index].id != id) ++index;
    if (index == client.chat_count) {
        if (client.chat_count >= TG_MAX_CHATS) return;
        ++client.chat_count;
    }
    TgChat* chat = &client.chats[index];
    chat->id = id;
    token_string(json_member(object, "title"), chat->title, sizeof(chat->title));
    int last = json_member(object, "last_message");
    int content = json_member(last, "content");
    int text = json_member(content, "text");
    token_string(json_member(text, "text"), chat->preview, sizeof(chat->preview));
}

static void update_message(int object) {
    int64_t chat_id = token_integer(json_member(object, "chat_id"));
    if (!chat_id || chat_id != client.selected_chat) return;
    int content = json_member(object, "content");
    int text = json_member(content, "text");
    int text_value = json_member(text, "text");
    if (text_value < 0) return;

    int64_t id = token_integer(json_member(object, "id"));
    for (int i = 0; i < client.message_count; ++i) {
        if (client.messages[i].id == id) return;
    }
    if (client.message_count == TG_MAX_MESSAGES) {
        memmove(client.messages, client.messages + 1,
                sizeof(client.messages[0]) * (TG_MAX_MESSAGES - 1));
        --client.message_count;
    }
    TgMessage* message = &client.messages[client.message_count++];
    message->id = id;
    int outgoing = json_member(object, "is_outgoing");
    message->outgoing = outgoing >= 0 &&
                        json_source[json_tokens[outgoing].start] == 't';
    token_string(text_value, message->text, sizeof(message->text));
}

static void handle_response(const char* response) {
    if (!json_parse(response)) return;
    int type = json_member(0, "@type");
    char type_name[64];
    token_string(type, type_name, sizeof(type_name));
    const char* request_method = request_method_for_response();
    int proxy_request = request_method &&
        (strcmp(request_method, "addProxy") == 0 ||
         strcmp(request_method, "editProxy") == 0 ||
         strcmp(request_method, "disableProxy") == 0);

    if (strcmp(type_name, "updateAuthorizationState") == 0) {
        int state = json_member(json_member(0, "authorization_state"), "@type");
        char state_name[64];
        token_string(state, state_name, sizeof(state_name));
        handle_authorization(state_name);
    } else if (strcmp(type_name, "ok") == 0 && request_method &&
               strcmp(request_method, "setTdlibParameters") == 0) {
        add_mtproto_proxies();
    } else if (strcmp(type_name, "proxies") == 0 && client.proxy_setup_pending) {
        int proxies = json_member(0, "proxies");
        int proxy_id = 0;
        for (int i = 0; i < json_array_size(proxies); ++i) {
            int proxy = json_array_item(proxies, i);
            int server = json_member(proxy, "server");
            int port = (int)token_integer(json_member(proxy, "port"));
            if (server >= 0 && token_equals(server, client.proxy_server) &&
                port == client.proxy_port) {
                proxy_id = (int)token_integer(json_member(proxy, "id"));
                break;
            }
        }
        fprintf(stderr, "TDLib: getProxies response, found proxy_id=%d\n", proxy_id);
        configure_proxy(proxy_id);
    } else if (strcmp(type_name, "proxy") == 0 ||
               strcmp(type_name, "ok") == 0) {
        int extra = json_member(0, "@extra");
        if (token_equals(extra, "load_messages")) {
            client.message_count = 0;
        }
        if (client.proxy_apply_pending && proxy_request) {
            client.proxy_apply_pending = 0;
            client.error[0] = '\0';
            if (!client.awaiting_initial_proxy) {
                snprintf(client.status, sizeof(client.status),
                         "Telegram proxy updated");
            }
            fprintf(stderr, "TDLib: proxy request succeeded\n");
        }
        if (client.awaiting_initial_proxy && proxy_request) {
            client.awaiting_initial_proxy = 0;
            send_request("{\"@type\":\"getAuthorizationState\"}");
        }
    } else if (strcmp(type_name, "updateNewChat") == 0) {
        update_chat(json_member(0, "chat"));
    } else if (strcmp(type_name, "updateChatTitle") == 0) {
        int64_t id = token_integer(json_member(0, "chat_id"));
        for (int i = 0; i < client.chat_count; ++i) {
            if (client.chats[i].id == id) {
                token_string(json_member(0, "title"), client.chats[i].title,
                             sizeof(client.chats[i].title));
            }
        }
    } else if (strcmp(type_name, "updateChatLastMessage") == 0) {
        int64_t id = token_integer(json_member(0, "chat_id"));
        for (int i = 0; i < client.chat_count; ++i) {
            if (client.chats[i].id == id) {
                int content = json_member(json_member(0, "last_message"), "content");
                int text = json_member(content, "text");
                token_string(json_member(text, "text"), client.chats[i].preview,
                             sizeof(client.chats[i].preview));
            }
        }
    } else if (strcmp(type_name, "updateNewMessage") == 0) {
        update_message(json_member(0, "message"));
    } else if (strcmp(type_name, "chats") == 0) {
        int chats = json_member(0, "chat_ids");
        int count = json_array_size(chats);
        if (count > TG_MAX_CHATS) count = TG_MAX_CHATS;
        for (int i = 0; i < count; ++i) {
            char request[128];
            snprintf(request, sizeof(request),             "{\"@type\":\"getChat\",\"chat_id\":%lld}",
                     (long long)token_integer(json_array_item(chats, i)));
            send_request(request);
        }
    } else if (strcmp(type_name, "messages") == 0) {
        int messages = json_member(0, "messages");
        int count = json_array_size(messages);
        if (client.older_history_pending) {
            int previous_count = client.message_count;
            for (int i = 0; i < count; ++i) {
                int object = json_array_item(messages, i);
                int64_t id = token_integer(json_member(object, "id"));
                int64_t chat_id = token_integer(json_member(object, "chat_id"));
                if (!id || chat_id != client.selected_chat) continue;
                int content = json_member(object, "content");
                int text = json_member(content, "text");
                int value = json_member(text, "text");
                if (value < 0) continue;
                int duplicate = 0;
                for (int j = 0; j < client.message_count; ++j) {
                    if (client.messages[j].id == id) duplicate = 1;
                }
                if (duplicate) continue;
                if (client.message_count == TG_MAX_MESSAGES) {
                    continue;
                }
                memmove(client.messages + 1, client.messages,
                        sizeof(client.messages[0]) * (size_t)client.message_count);
                TgMessage* message = &client.messages[0];
                message->id = id;
                int outgoing = json_member(object, "is_outgoing");
                message->outgoing = outgoing >= 0 &&
                                    json_source[json_tokens[outgoing].start] == 't';
                token_string(value, message->text, sizeof(message->text));
                ++client.message_count;
            }
            if (count < 30 || client.message_count == previous_count) {
                client.older_history_exhausted = 1;
            }
            client.older_history_pending = 0;
        } else {
        for (int i = count - 1; i >= 0; --i) {
            update_message(json_array_item(messages, i));
        }
        }
    } else if (strcmp(type_name, "error") == 0) {
        int code = (int)token_integer(json_member(0, "code"));
        char message[TG_TEXT_LENGTH];
        token_string(json_member(0, "message"), message, sizeof(message));
        if (client.proxy_setup_pending || client.proxy_apply_pending ||
            client.awaiting_initial_proxy) {
            fprintf(stderr, "TDLib proxy request %s failed: %d: %s\n",
                    request_method ? request_method : "unknown", code, message);
            client.proxy_setup_pending = 0;
            client.proxy_apply_pending = 0;
            client.awaiting_initial_proxy = 0;
            char detail[TG_TEXT_LENGTH];
            snprintf(detail, sizeof(detail), "%s %d: %.200s",
                     request_method ? request_method : "Proxy error", code, message);
            if (client.running && client.state == TgStateInitializing) {
                set_error(detail);
            } else {
                snprintf(client.error, sizeof(client.error), "%s", detail);
                snprintf(client.status, sizeof(client.status), "%s", detail);
            }
            return;
        }
        if (client.qr_request_pending) {
            snprintf(client.error, sizeof(client.error), "%d: %.240s", code, message);
            fprintf(stderr, "TDLib QR request error: %s\n", client.error);
            client.qr_request_pending = 0;
            set_status(client.qr_return_state, client.qr_return_status,
                       client.qr_return_prompt);
            return;
        }
        if (code == 406) {
            snprintf(client.error, sizeof(client.error),
                     "Telegram authorization rejected (406)");
            snprintf(client.status, sizeof(client.status), "%s", client.error);
            fprintf(stderr, "%s\n", client.error);
            if (client.pending_auth_state == TgStatePhone ||
                client.pending_auth_state == TgStateCode ||
                client.pending_auth_state == TgStatePassword) {
                set_status(client.pending_auth_state, client.pending_auth_status,
                           client.pending_auth_prompt);
            }
            return;
        }
        if (request_method) {
            snprintf(client.error, sizeof(client.error), "%s %d: %.180s",
                     request_method, code, message);
        } else {
            snprintf(client.error, sizeof(client.error), "%d: %.240s", code, message);
        }
        snprintf(client.status, sizeof(client.status), "%s", message);
        fprintf(stderr, "TDLib request %s error: %s\n",
                request_method ? request_method : "unknown", client.error);
        if (client.pending_auth_state == TgStatePhone ||
            client.pending_auth_state == TgStateCode ||
            client.pending_auth_state == TgStatePassword) {
            set_status(client.pending_auth_state, client.pending_auth_status,
                       client.pending_auth_prompt);
        }
    }
}

void tg_poll(void) {
    if (!client.running || !client.receive) return;
    for (int i = 0; i < 8; ++i) {
        const char* response = client.receive(i == 0 ? 0.0 : 0.0);
        if (!response) break;
        handle_response(response);
    }
}

void tg_stop(void) {
    int was_running = client.running;
    if (client.running) {
        send_request("{\"@type\":\"close\"}");
        if (client.receive) {
            for (int i = 0; i < 20 && !client.closed; ++i) {
                const char* response = client.receive(0.2);
                if (response) handle_response(response);
            }
        }
    }
    client.running = 0;
    /* Keep TDLib mapped until process exit if it didn't finish closing in time. */
    if ((!was_running || client.closed) && client.library) {
        dlclose(client.library);
        client.library = NULL;
    }
    memset(client.api_hash, 0, sizeof(client.api_hash));
    memset(client.database_key, 0, sizeof(client.database_key));
}

int tg_json_self_test(void) {
    memset(&client, 0, sizeof(client));
    const char* chat =
        "{\"@type\":\"updateNewChat\",\"chat\":{\"id\":42,\"title\":\"A \\\"test\\\"\","
        "\"last_message\":{\"content\":{\"@type\":\"messageText\","
        "\"text\":{\"text\":\"hello\"}}}}}";
    if (!json_parse(chat)) return 0;
    handle_response(chat);
    if (client.chat_count != 1 || client.chats[0].id != 42 ||
        strcmp(client.chats[0].title, "A \"test\"") != 0 ||
        strcmp(client.chats[0].preview, "hello") != 0) return 0;

    const char* message =
        "{\"@type\":\"updateNewMessage\",\"message\":{\"id\":7,\"chat_id\":42,"
        "\"is_outgoing\":true,\"content\":{\"@type\":\"messageText\","
        "\"text\":{\"text\":\"hi \\\\u263a\"}}}}";
    client.selected_chat = 42;
    handle_response(message);
    if (client.message_count != 1 || !client.messages[0].outgoing) return 0;
    if (strcmp(client.messages[0].text, "hi \\u263a") != 0) return 0;

    client.pending_auth_state = TgStateCode;
    snprintf(client.pending_auth_status, sizeof(client.pending_auth_status), "Telegram login");
    snprintf(client.pending_auth_prompt, sizeof(client.pending_auth_prompt), "Enter code");
    handle_response("{\"@type\":\"error\",\"code\":406,"
                    "\"message\":\"must not be shown to the user\"}");
    if (client.state != TgStateCode ||
        strstr(client.error, "must not be shown") != NULL) return 0;

    client.send = self_test_send;
    client.client_id = 1;
    client.api_id = 12345;
    snprintf(client.api_hash, sizeof(client.api_hash), "%s",
             "0123456789abcdef0123456789abcdef");
    memset(client.database_key, 'a', sizeof(client.database_key) - 1);
    client.database_key[sizeof(client.database_key) - 1] = '\0';
    handle_authorization("authorizationStateWaitTdlibParameters");
    if (!json_parse(self_test_request)) return 0;
    int parameters = json_member(0, "parameters");
    if (parameters < 0 ||
        !token_equals(json_member(parameters, "@type"), "tdlibParameters") ||
        json_member(parameters, "api_id") < 0 ||
        json_member(parameters, "api_hash") < 0) return 0;

    send_request("{\"@type\":\"getProxies\"}");
    if (!json_parse(self_test_request)) return 0;
    int extra = json_member(0, "@extra");
    if (extra < 0) return 0;
    char tracked_extra[64];
    size_t extra_length = (size_t)(json_tokens[extra].end -
                                   json_tokens[extra].start);
    if (extra_length >= sizeof(tracked_extra)) return 0;
    memcpy(tracked_extra, json_source + json_tokens[extra].start, extra_length);
    tracked_extra[extra_length] = '\0';
    char proxy_error[256];
    snprintf(proxy_error, sizeof(proxy_error),
             "{\"@type\":\"error\",\"code\":400,"
             "\"message\":\"Parameters aren't specified\","
             "\"@extra\":%s}", tracked_extra);
    client.proxy_setup_pending = 1;
    handle_response(proxy_error);
    if (strstr(client.error, "getProxies 400") == NULL ||
        client.proxy_setup_pending) return 0;
    client.send = NULL;
    client.client_id = 0;

    const char* qr_state =
        "{\"@type\":\"updateAuthorizationState\",\"authorization_state\":{"
        "\"@type\":\"authorizationStateWaitOtherDeviceConfirmation\","
        "\"link\":\"tg://login?token=sample\"}}";
    set_status(TgStatePhone, "Sign in to Telegram", "Enter your phone number");
    tg_request_qr_auth();
    handle_response("{\"@type\":\"error\",\"code\":400,\"message\":\"test error\"}");
    if (client.state != TgStatePhone || !client.error[0]) return 0;
    handle_response(qr_state);
    unsigned char matrix[37][37];
    return client.state == TgStateQr &&
           strcmp(client.qr_link, "tg://login?token=sample") == 0 &&
           tg_qr_matrix(matrix);
}

TgState tg_state(void) {
    return client.state;
}

const char* tg_status(void) {
    if (preferences_is_russian()) {
        switch (client.state) {
            case TgStatePhone: return "Вход в Telegram";
            case TgStateCode: return "Вход в Telegram";
            case TgStatePassword: return "Двухэтапная проверка";
            case TgStateQr: return "Подтвердите вход на другом устройстве";
            case TgStateReady: return "Подключено к Telegram";
            case TgStateError: return "Ошибка подключения";
            case TgStateInitializing:
                if (strstr(client.status, "QR")) return "Запрос QR-входа…";
                return "Подключение к Telegram…";
            case TgStateUnavailable:
                if (strstr(client.status, "library")) return "Библиотека TDLib не найдена";
                if (strstr(client.status, "credentials")) return "Не заданы API-реквизиты";
                return "Нет подключения";
            default: return client.status;
        }
    }
    return client.status;
}

const char* tg_prompt(void) {
    if (preferences_is_russian()) {
        switch (client.state) {
            case TgStatePhone: return "Введите номер телефона";
            case TgStateCode: return "Введите код из Telegram";
            case TgStatePassword: return "Введите пароль Telegram";
            default: break;
        }
    }
    return client.prompt;
}

const char* tg_qr_link(void) {
    return client.qr_link;
}

int tg_qr_matrix(unsigned char matrix[37][37]) {
    if (!matrix || !client.qr_link[0]) return 0;
    if (client.qr_matrix_status == 0) {
        client.qr_matrix_status =
            qr_encode(client.qr_link, client.qr_matrix) ? 1 : -1;
    }
    if (client.qr_matrix_status < 0) return 0;
    memcpy(matrix, client.qr_matrix, sizeof(client.qr_matrix));
    return 1;
}

void tg_request_qr_auth(void) {
    if (client.state != TgStatePhone && client.state != TgStateCode &&
        client.state != TgStatePassword) return;
    client.qr_link[0] = '\0';
    client.qr_matrix_status = 0;
    client.error[0] = '\0';
    client.qr_return_state = client.state;
    snprintf(client.qr_return_status, sizeof(client.qr_return_status), "%s", client.status);
    snprintf(client.qr_return_prompt, sizeof(client.qr_return_prompt), "%s", client.prompt);
    client.qr_request_pending = 1;
    send_request("{\"@type\":\"requestQrCodeAuthentication\",\"other_user_ids\":[]}");
    set_status(TgStateInitializing, "Requesting QR login…", "");
}

const char* tg_last_error(void) {
    return client.error;
}

int tg_chat_count(void) {
    return client.chat_count;
}

const TgChat* tg_chat_at(int index) {
    return index >= 0 && index < client.chat_count ? &client.chats[index] : NULL;
}

int64_t tg_selected_chat_id(void) {
    return client.selected_chat;
}

const char* tg_selected_chat_title(void) {
    for (int i = 0; i < client.chat_count; ++i) {
        if (client.chats[i].id == client.selected_chat) return client.chats[i].title;
    }
    return "Telegram";
}

int tg_message_count(void) {
    return client.message_count;
}

const TgMessage* tg_message_at(int index) {
    return index >= 0 && index < client.message_count ? &client.messages[index] : NULL;
}

void tg_select_chat(int index) {
    const TgChat* chat = tg_chat_at(index);
    if (!chat || client.state != TgStateReady || client.selected_chat == chat->id) return;
    client.selected_chat = chat->id;
    client.message_count = 0;
    client.older_history_pending = 0;
    client.older_history_exhausted = 0;
    char request[192];
    snprintf(request, sizeof(request),
             "{\"@type\":\"getChatHistory\",\"chat_id\":%lld,\"from_message_id\":0,"
             "\"offset\":0,\"limit\":30,\"only_local\":false,\"@extra\":\"load_messages\"}",
             (long long)chat->id);
    send_request(request);
}

void tg_load_older_messages(void) {
    if (client.state != TgStateReady || !client.selected_chat ||
        client.message_count == 0 || client.older_history_pending ||
        client.older_history_exhausted) return;
    char request[192];
    snprintf(request, sizeof(request),
             "{\"@type\":\"getChatHistory\",\"chat_id\":%lld,\"from_message_id\":%lld,"
             "\"offset\":0,\"limit\":30,\"only_local\":false}",
             (long long)client.selected_chat, (long long)client.messages[0].id);
    client.older_history_pending = 1;
    send_request(request);
}

int tg_submit_auth(const char* value) {
    if (!value || !*value) return 0;
    char escaped[TG_TEXT_LENGTH * 6];
    char request[TG_TEXT_LENGTH * 6 + 512];
    if (!json_escape(value, escaped, sizeof(escaped))) return 0;
    if (client.state == TgStatePhone) {
        snprintf(request, sizeof(request),
                 "{\"@type\":\"setAuthenticationPhoneNumber\",\"phone_number\":\"%s\","
                 "\"settings\":{\"@type\":\"phoneNumberAuthenticationSettings\","
                 "\"allow_flash_call\":false,\"allow_missed_call\":false,"
                 "\"is_current_phone_number\":false,\"has_unknown_phone_number\":false,"
                 "\"allow_sms_retriever_api\":false,\"firebase_authentication_settings\":null,"
                 "\"authentication_tokens\":[]}}",
                 escaped);
    } else if (client.state == TgStateCode) {
        snprintf(request, sizeof(request),
                 "{\"@type\":\"checkAuthenticationCode\",\"code\":\"%s\"}", escaped);
    } else if (client.state == TgStatePassword) {
        snprintf(request, sizeof(request),
                 "{\"@type\":\"checkAuthenticationPassword\",\"password\":\"%s\"}", escaped);
    } else {
        return 0;
    }
    client.pending_auth_state = client.state;
    snprintf(client.pending_auth_status, sizeof(client.pending_auth_status), "%s",
             client.status);
    snprintf(client.pending_auth_prompt, sizeof(client.pending_auth_prompt), "%s",
             client.prompt);
    client.error[0] = '\0';
    send_request(request);
    memset(escaped, 0, sizeof(escaped));
    memset(request, 0, sizeof(request));
    set_status(TgStateInitializing, "Checking Telegram credentials…", "");
    return 1;
}

int tg_send_message(const char* text) {
    if (client.state != TgStateReady || !client.selected_chat || !text || !*text) return 0;
    char escaped[TG_TEXT_LENGTH * 6];
    char request[TG_TEXT_LENGTH * 6 + 512];
    if (!json_escape(text, escaped, sizeof(escaped))) return 0;
    snprintf(request, sizeof(request),
             "{\"@type\":\"sendMessage\",\"chat_id\":%lld,\"topic_id\":null,"
             "\"reply_to\":null,\"options\":null,\"reply_markup\":null,\"input_message_content\":"
             "{\"@type\":\"inputMessageText\",\"text\":{\"@type\":\"formattedText\","
             "\"text\":\"%s\",\"entities\":[]},\"link_preview_options\":null,"
             "\"clear_draft\":true}}",
             (long long)client.selected_chat, escaped);
    send_request(request);
    return 1;
}

int tg_apply_proxy(int enabled) {
    int previous = preferences_proxy_enabled();
    if (enabled && (!preferences_proxy_server()[0] ||
                    preferences_proxy_port() < 1 ||
                    preferences_proxy_port() > 65535)) {
        snprintf(client.error, sizeof(client.error),
                 "Enter a SOCKS5 server and valid port first");
        return 0;
    }
    if (!preferences_set_proxy_enabled(enabled)) {
        snprintf(client.error, sizeof(client.error),
                 "Cannot save SOCKS5 proxy settings");
        return 0;
    }
    if (!client.running) return 1;
    if (!enabled) {
        client.proxy_apply_pending = 1;
        send_request("{\"@type\":\"disableProxy\"}");
        client.error[0] = '\0';
        return 1;
    }
    client.error[0] = '\0';
    if (prepare_proxy()) return 1;
    preferences_set_proxy_enabled(previous);
    return 0;
}

int tg_apply_subscription(int enabled) {
    if (enabled && !preferences_subscription_configured()) {
        snprintf(client.error, sizeof(client.error),
                 "VLESS subscription is not configured");
        return 0;
    }
    int previous = preferences_subscription_enabled();
    if (!tg_apply_proxy(enabled)) return 0;
    if (!preferences_set_subscription_enabled(enabled)) {
        tg_apply_proxy(previous);
        snprintf(client.error, sizeof(client.error),
                 "Cannot save subscription settings");
        return 0;
    }
    return 1;
}
