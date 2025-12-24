// RISC OS Access/ShareFS Server - Logging
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_LOG_H
#define RAS_LOG_H

#include <stdio.h>

typedef enum {
    RAS_LOG_NONE = 0,
    RAS_LOG_ERROR,
    RAS_LOG_INFO,
    RAS_LOG_DEBUG,
    RAS_LOG_PROTOCOL
} ras_log_level;

void ras_log_set_level(ras_log_level level);
void ras_log_set_stream(FILE *stream);
void ras_log(ras_log_level level, const char *fmt, ...);
ras_log_level ras_log_level_from_string(const char *s);

#endif
