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

#ifndef SFS_LOG_H
#define SFS_LOG_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
  SFS_LOG_NONE = 0,
  SFS_LOG_ERROR,
  SFS_LOG_INFO,
  SFS_LOG_DEBUG,
  SFS_LOG_PROTOCOL
} sfs_log_level;

// Choose where the log is written, before sfs_log_init().
//
// Pass the `log_file` setting from sharefs.conf, or NULL to use the platform
// default. A relative path is taken as relative to the working directory.
void sfs_log_set_path(const char *path);

// Initialize logging - opens the log file.
//
// Uses sfs_log_set_path() if one was given, otherwise the platform default:
//   Linux    /var/log/sharefs/sharefs.log, else /tmp/sharefs.log
//   macOS    /var/log/sharefs/sharefs.log, else ~/Library/Logs/ShareFS/
//   Windows  %ProgramData%\ShareFS\sharefs.log, else the executable's own
//            directory. It is never forced to C:\ShareFS.
//
// Returns 0 on success. Failure is not fatal; logging falls back to stderr.
int sfs_log_init(void);

// Where the log actually ended up, for reporting to the user. Valid after
// sfs_log_init(); returns NULL if logging fell back to stderr.
const char *sfs_log_get_path(void);

// The two paths the platform default resolves to, without opening anything:
// `preferred` is tried first and `fallback` is where the log goes when that
// cannot be written. Either pointer may be NULL.
//
// Exposed so that a window reporting "where does the log go" asks the code that
// decides rather than keeping a second copy of the rules, which is how the
// configuration search order came to disagree with itself.
void sfs_log_default_paths(char *preferred, size_t preferred_sz, char *fallback,
                           size_t fallback_sz);

// Shutdown logging - closes log file
void sfs_log_shutdown(void);

void sfs_log_set_level(sfs_log_level level);
void sfs_log_set_stream(FILE *stream);
void sfs_log(sfs_log_level level, const char *fmt, ...);
sfs_log_level sfs_log_level_from_string(const char *s);

// Optional second destination for log lines, so a host process can display
// them as they happen instead of tailing the file. The line passed in is the
// fully formatted message without its trailing newline.
//
// Called on whichever thread emitted the message, which for an embedded server
// is the server thread rather than the host's main thread: a GUI sink must
// marshal to its own thread before touching any widget. Pass NULL to detach.
typedef void (*sfs_log_sink)(sfs_log_level level, const char *line,
                             void *user_data);
void sfs_log_set_sink(sfs_log_sink sink, void *user_data);

#endif
