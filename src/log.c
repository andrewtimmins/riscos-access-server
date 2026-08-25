/*
  ShareFS Server - Logging

  Copyright (C) 2025-2026 Andy Timmins

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static sfs_log_level g_level = SFS_LOG_INFO;
static FILE *g_stream = NULL;
static FILE *g_log_file = NULL;

// Where the caller asked the log to go, and where it actually went.
static char g_configured_path[1024];
static char g_active_path[1024];

static const char *level_names[] = {"NONE", "ERROR", "INFO", "DEBUG", "PROTO"};

static sfs_log_sink g_sink = NULL;
static void *g_sink_user = NULL;

void sfs_log_set_sink(sfs_log_sink sink, void *user_data) {
  g_sink = sink;
  g_sink_user = user_data;
}

void sfs_log_set_path(const char *path) {
  if (path && path[0])
    snprintf(g_configured_path, sizeof(g_configured_path), "%s", path);
  else
    g_configured_path[0] = '\0';
}

const char *sfs_log_get_path(void) {
  return g_active_path[0] ? g_active_path : NULL;
}

// Create the directory a path lives in, best effort.
static void ensure_parent_dir(const char *path) {
  char dir[1024];
  snprintf(dir, sizeof(dir), "%s", path);

  char *slash = strrchr(dir, '/');
#ifdef _WIN32
  char *back = strrchr(dir, '\\');
  if (back && (!slash || back > slash))
    slash = back;
#endif
  if (!slash || slash == dir)
    return;
  *slash = '\0';

#ifdef _WIN32
  _mkdir(dir);
#else
  struct stat st;
  if (stat(dir, &st) != 0)
    mkdir(dir, 0755);
#endif
}

// Fill `out` with the preferred log path for this platform, and `fallback`
// with the one to use when the preferred path is not writable.
static void default_log_paths(char *out, size_t out_sz, char *fallback,
                              size_t fb_sz) {
#ifdef _WIN32
  // Issue #16: this used to be hard-coded to C:\ShareFS, which is not an
  // acceptable place for a Windows service to write. Use ProgramData, and
  // fall back beside the executable rather than to the current directory.
  const char *program_data = getenv("ProgramData");
  if (program_data && program_data[0])
    snprintf(out, out_sz, "%s\\ShareFS\\sharefs.log", program_data);
  else
    snprintf(out, out_sz, "C:\\ProgramData\\ShareFS\\sharefs.log");

  char exe[512];
  DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
  if (n > 0 && n < sizeof(exe)) {
    char *slash = strrchr(exe, '\\');
    if (slash) {
      *slash = '\0';
      snprintf(fallback, fb_sz, "%s\\sharefs.log", exe);
      return;
    }
  }
  snprintf(fallback, fb_sz, ".\\sharefs.log");
#else
  snprintf(out, out_sz, "/var/log/sharefs/sharefs.log");
#ifdef __APPLE__
  // An unprivileged server cannot write to /var/log, and /tmp is swept
  // periodically, so use the standard per-user location.
  const char *home = getenv("HOME");
  if (home && home[0])
    snprintf(fallback, fb_sz, "%s/Library/Logs/ShareFS/sharefs.log", home);
  else
    snprintf(fallback, fb_sz, "/tmp/sharefs.log");
#else
  snprintf(fallback, fb_sz, "/tmp/sharefs.log");
#endif
#endif
}

int sfs_log_init(void) {
  char preferred[1024];
  char fallback[1024];
  default_log_paths(preferred, sizeof(preferred), fallback, sizeof(fallback));

  // An explicitly configured path is used as given, with no second guess:
  // if the administrator named a file, silently writing somewhere else would
  // be worse than saying so.
  if (g_configured_path[0]) {
    ensure_parent_dir(g_configured_path);
    g_log_file = fopen(g_configured_path, "a");
    if (g_log_file) {
      snprintf(g_active_path, sizeof(g_active_path), "%s", g_configured_path);
      g_stream = g_log_file;
      return 0;
    }
    fprintf(stderr, "Warning: could not open configured log file %s: %s\n",
            g_configured_path, strerror(errno));
    fprintf(stderr, "Falling back to the default location.\n");
  }

  const char *chosen = preferred;
  ensure_parent_dir(preferred);
  g_log_file = fopen(preferred, "a");

  if (!g_log_file) {
    chosen = fallback;
    ensure_parent_dir(fallback);
    g_log_file = fopen(fallback, "a");
    if (g_log_file)
      fprintf(stderr, "Logging to %s\n", fallback);
  }

  if (!g_log_file) {
    fprintf(stderr, "Warning: could not open a log file (tried %s and %s).\n",
            preferred, fallback);
    fprintf(stderr, "Logging to stderr instead.\n");
    g_active_path[0] = '\0';
    return 0; // Not fatal
  }

  snprintf(g_active_path, sizeof(g_active_path), "%s", chosen);
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

  // Format once into a buffer so the file and any sink see the same text.
  char message[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap);

  char line[1200];
  snprintf(line, sizeof(line), "[%s] %s: %s", timestamp, level_name, message);

  fprintf(out, "%s\n", line);
  fflush(out);

  // Read the pointer once: a host may detach the sink while this runs, and
  // calling through a pointer fetched twice risks using a stale one.
  sfs_log_sink sink = g_sink;
  if (sink)
    sink(level, line, g_sink_user);
}
