// ShareFS Server - Platform Abstraction
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_PLATFORM_H
#define SFS_PLATFORM_H

#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET sfs_socket;
#define SFS_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef int sfs_socket;
#define SFS_INVALID_SOCKET (-1)
#endif

int sfs_platform_init(void);
void sfs_platform_shutdown(void);
void sfs_sleep_ms(int ms);
int sfs_mkdir(const char *path);

// Cross-platform filesystem info
typedef struct {
  uint64_t free_bytes;
  uint64_t total_bytes;
  uint32_t block_size;
} sfs_fsinfo;

int sfs_get_fsinfo(const char *path, sfs_fsinfo *info);

// Cross-platform utime
int sfs_set_mtime(const char *path, time_t mtime);

#endif
