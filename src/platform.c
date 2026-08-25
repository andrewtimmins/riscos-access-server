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

#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <direct.h>
#include <stdio.h>
#include <string.h>
#include <sys/utime.h>

int sfs_platform_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

void sfs_platform_shutdown(void) {
    WSACleanup();
}

void sfs_sleep_ms(int ms) {
    Sleep((DWORD)ms);
}

int sfs_mkdir(const char *path) {
    if (!path) return -1;
    return _mkdir(path);
}

int sfs_get_fsinfo(const char *path, sfs_fsinfo *info) {
    if (!path || !info) return -1;

    ULARGE_INTEGER free_avail, total_bytes, free_bytes;
    if (!GetDiskFreeSpaceExA(path, &free_avail, &total_bytes, &free_bytes)) {
        return -1;
    }

    info->free_bytes = free_bytes.QuadPart;
    info->total_bytes = total_bytes.QuadPart;
    info->block_size = 4096;  // Approximate
    return 0;
}

int sfs_set_mtime(const char *path, time_t mtime) {
    if (!path) return -1;
    struct _utimbuf ut;
    ut.actime = mtime;
    ut.modtime = mtime;
    return _utime(path, &ut);
}

int sfs_spawn_detached(const char *command) {
    if (!command || !command[0]) return -1;

    char cmdline[2048];
    if (snprintf(cmdline, sizeof(cmdline), "cmd.exe /C %s", command)
            >= (int)sizeof(cmdline))
        return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return -1;
    }

    // Nothing waits on it, so release the handles immediately.
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

#else
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

int sfs_platform_init(void) {
    return 0;
}

void sfs_platform_shutdown(void) {
    // No-op on POSIX
}

void sfs_sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int sfs_mkdir(const char *path) {
    if (!path) return -1;
    return mkdir(path, 0775);
}

int sfs_get_fsinfo(const char *path, sfs_fsinfo *info) {
    if (!path || !info) return -1;

    struct statvfs svfs;
    if (statvfs(path, &svfs) != 0) {
        return -1;
    }

    info->free_bytes = (uint64_t)svfs.f_bfree * svfs.f_bsize;
    info->total_bytes = (uint64_t)svfs.f_blocks * svfs.f_bsize;
    info->block_size = (uint32_t)svfs.f_bsize;
    return 0;
}

int sfs_set_mtime(const char *path, time_t mtime) {
    if (!path) return -1;
    struct utimbuf ut;
    ut.actime = mtime;
    ut.modtime = mtime;
    return utime(path, &ut);
}

int sfs_spawn_detached(const char *command) {
    if (!command || !command[0]) return -1;

    // Double fork: the intermediate child exits at once and is reaped here,
    // so the grandchild that actually runs the command is orphaned and
    // reaped by init. That leaves no zombie for a server that never waits.
    pid_t first = fork();
    if (first < 0) return -1;

    if (first == 0) {
        pid_t second = fork();
        if (second < 0) _exit(127);
        if (second == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }

    int status;
    while (waitpid(first, &status, 0) < 0 && errno == EINTR) {
        // Interrupted by a signal; keep waiting for the intermediate child.
    }
    return 0;
}

#endif
