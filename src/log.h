// ShareFS Server - Logging
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_LOG_H
#define SFS_LOG_H

#include <stdio.h>

typedef enum {
  SFS_LOG_NONE = 0,
  SFS_LOG_ERROR,
  SFS_LOG_INFO,
  SFS_LOG_DEBUG,
  SFS_LOG_PROTOCOL
} sfs_log_level;

// Initialize logging - opens log file
// Linux: /var/log/sharefs/sharefs.log (falls back to /tmp/sharefs.log)
// Windows: C:/ShareFS/sharefs.log (falls back to ./sharefs.log)
int sfs_log_init(void);

// Shutdown logging - closes log file
void sfs_log_shutdown(void);

void sfs_log_set_level(sfs_log_level level);
void sfs_log_set_stream(FILE *stream);
void sfs_log(sfs_log_level level, const char *fmt, ...);
sfs_log_level sfs_log_level_from_string(const char *s);

#endif
