// RISC OS Access/ShareFS Server - Platform Abstraction
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_PLATFORM_H
#define RAS_PLATFORM_H

#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET ras_socket;
#define RAS_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

typedef int ras_socket;
#define RAS_INVALID_SOCKET (-1)
#endif

int ras_platform_init(void);
void ras_platform_shutdown(void);
void ras_sleep_ms(int ms);
int ras_mkdir(const char *path);

// Cross-platform filesystem info
typedef struct {
    uint64_t free_bytes;
    uint64_t total_bytes;
    uint32_t block_size;
} ras_fsinfo;

int ras_get_fsinfo(const char *path, ras_fsinfo *info);

// Cross-platform utime
int ras_set_mtime(const char *path, time_t mtime);

#endif
