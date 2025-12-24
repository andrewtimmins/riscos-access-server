// RISC OS Access/ShareFS Server - Platform Abstraction
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <direct.h>
#include <sys/utime.h>

int ras_platform_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

void ras_platform_shutdown(void) {
    WSACleanup();
}

void ras_sleep_ms(int ms) {
    Sleep((DWORD)ms);
}

int ras_mkdir(const char *path) {
    if (!path) return -1;
    return _mkdir(path);
}

int ras_get_fsinfo(const char *path, ras_fsinfo *info) {
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

int ras_set_mtime(const char *path, time_t mtime) {
    if (!path) return -1;
    struct _utimbuf ut;
    ut.actime = mtime;
    ut.modtime = mtime;
    return _utime(path, &ut);
}

#else
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <utime.h>

int ras_platform_init(void) {
    return 0;
}

void ras_platform_shutdown(void) {
    // No-op on POSIX
}

void ras_sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int ras_mkdir(const char *path) {
    if (!path) return -1;
    return mkdir(path, 0775);
}

int ras_get_fsinfo(const char *path, ras_fsinfo *info) {
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

int ras_set_mtime(const char *path, time_t mtime) {
    if (!path) return -1;
    struct utimbuf ut;
    ut.actime = mtime;
    ut.modtime = mtime;
    return utime(path, &ut);
}

#endif
