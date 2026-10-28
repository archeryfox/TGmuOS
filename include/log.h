#pragma once

/* Structured logging helpers. Everything goes to stderr; the muOS launcher
 * redirects stderr to log.txt, so keep the output diagnostic-friendly:
 * timestamped, tagged by component, no secrets. */

void log_msg(const char* component, const char* level, const char* format, ...);

#define log_info(component, ...)  log_msg(component, "INFO", __VA_ARGS__)
#define log_warn(component, ...)  log_msg(component, "WARN", __VA_ARGS__)
#define log_error(component, ...) log_msg(component, "ERROR", __VA_ARGS__)
