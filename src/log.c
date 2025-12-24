// RISC OS Access/ShareFS Server - Logging
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "log.h"

#include <stdarg.h>
#include <string.h>

static ras_log_level g_level = RAS_LOG_INFO;
static FILE *g_stream = NULL;

ras_log_level ras_log_level_from_string(const char *s) {
    if (!s) return RAS_LOG_INFO;
    if (strcmp(s, "none") == 0) return RAS_LOG_NONE;
    if (strcmp(s, "error") == 0) return RAS_LOG_ERROR;
    if (strcmp(s, "info") == 0) return RAS_LOG_INFO;
    if (strcmp(s, "debug") == 0) return RAS_LOG_DEBUG;
    if (strcmp(s, "protocol") == 0) return RAS_LOG_PROTOCOL;
    return RAS_LOG_INFO;
}

void ras_log_set_level(ras_log_level level) {
    g_level = level;
}

void ras_log_set_stream(FILE *stream) {
    g_stream = stream;
}

void ras_log(ras_log_level level, const char *fmt, ...) {
    if (level > g_level || level == RAS_LOG_NONE) {
        return;
    }

    FILE *out = g_stream ? g_stream : stderr;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    va_end(ap);
}
