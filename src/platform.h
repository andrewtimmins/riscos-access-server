/*
  ShareFS Server - Platform Abstraction

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

// Run a shell command without waiting for it to finish.
//
// The server loop is single-threaded, so waiting on a print command stalled
// every connected client for as long as it took. Returns 0 if the command was
// launched, -1 if it could not be. Nothing is reported about its exit status,
// because by design nobody is waiting for it.
int sfs_spawn_detached(const char *command);

#endif
