// ShareFS Server - Logging
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define LOG_PATH "C:/ShareFS/sharefs.log"
#define FALLBACK_LOG_PATH "./sharefs.log"
#else
#include <sys/stat.h>
#define LOG_DIR "/var/log/sharefs"
#define LOG_PATH "/var/log/sharefs/sharefs.log"
#define FALLBACK_LOG_PATH "/tmp/sharefs.log"
#endif

static sfs_log_level g_level = SFS_LOG_INFO;
static FILE *g_stream = NULL;
static FILE *g_log_file = NULL;

static const char *level_names[] = {"NONE", "ERROR", "INFO", "DEBUG", "PROTO"};

int sfs_log_init(void) {
#ifdef _WIN32
  // Ensure log directory exists (best effort)
  _mkdir("C:/ShareFS");
#else
  // Create log directory if it doesn't exist
  struct stat st;
  if (stat(LOG_DIR, &st) != 0) {
    if (mkdir(LOG_DIR, 0755) != 0 && errno != EEXIST) {
      fprintf(stderr, "Warning: Could not create log directory %s: %s\n",
              LOG_DIR, strerror(errno));
    }
  }
#endif

  // Open log file for append (fall back to tmp if permission denied)
  g_log_file = fopen(LOG_PATH, "a");
  if (!g_log_file) {
    fprintf(stderr, "Warning: Could not open log file %s: %s\n", LOG_PATH,
            strerror(errno));
    fprintf(stderr, "Logging to %s instead.\n", FALLBACK_LOG_PATH);
    g_log_file = fopen(FALLBACK_LOG_PATH, "a");
  }

  if (!g_log_file) {
    fprintf(stderr, "Warning: Could not open fallback log file %s: %s\n",
            FALLBACK_LOG_PATH, strerror(errno));
    fprintf(stderr, "Logging to stderr instead.\n");
    return 0; // Not fatal, continue with stderr
  }

  g_stream = g_log_file;
  return 0;
}

void sfs_log_shutdown(void) {
  if (g_log_file) {
    fclose(g_log_file);
    g_log_file = NULL;
    g_stream = NULL;
  }
}

sfs_log_level sfs_log_level_from_string(const char *s) {
  if (!s)
    return SFS_LOG_INFO;
  if (strcmp(s, "none") == 0)
    return SFS_LOG_NONE;
  if (strcmp(s, "error") == 0)
    return SFS_LOG_ERROR;
  if (strcmp(s, "info") == 0)
    return SFS_LOG_INFO;
  if (strcmp(s, "debug") == 0)
    return SFS_LOG_DEBUG;
  if (strcmp(s, "protocol") == 0)
    return SFS_LOG_PROTOCOL;
  return SFS_LOG_INFO;
}

void sfs_log_set_level(sfs_log_level level) { g_level = level; }

void sfs_log_set_stream(FILE *stream) { g_stream = stream; }

void sfs_log(sfs_log_level level, const char *fmt, ...) {
  if (level > g_level || level == SFS_LOG_NONE) {
    return;
  }

  FILE *out = g_stream ? g_stream : stderr;

  // Get current time
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);

  // Format timestamp: [YYYY-MM-DD HH:MM:SS]
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

  // Get level name
  const char *level_name =
      (level >= 0 && level <= SFS_LOG_PROTOCOL) ? level_names[level] : "?";

  // Print timestamp and level prefix
  fprintf(out, "[%s] %s: ", timestamp, level_name);

  // Print the actual message
  va_list ap;
  va_start(ap, fmt);
  vfprintf(out, fmt, ap);
  va_end(ap);

  fputc('\n', out);
  fflush(out);
}
