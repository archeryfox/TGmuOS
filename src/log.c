#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

void log_msg(const char* component, const char* level, const char* format, ...) {
    char message[768];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    char stamp[32] = "........";
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        struct tm local;
        if (localtime_r(&ts.tv_sec, &local)) {
            snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d",
                     local.tm_hour, local.tm_min, local.tm_sec,
                     (int)(ts.tv_nsec / 1000000));
        }
    }
    fprintf(stderr, "%s [%s] %s: %s\n", stamp, level, component, message);
}
