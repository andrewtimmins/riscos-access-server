/*
  ShareFS Server - RISC OS Type Conversion

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

#ifndef SFS_RISCOS_H
#define SFS_RISCOS_H

#include <stdint.h>
#include <time.h>
#include "config.h"

// RISC OS file attributes
#define SFS_ATTR_R  0x01  // Owner readable
#define SFS_ATTR_W  0x02  // Owner writable
#define SFS_ATTR_L  0x08  // Locked
#define SFS_ATTR_r  0x10  // Public readable
#define SFS_ATTR_w  0x20  // Public writable

// Object types
#define SFS_TYPE_NOTFOUND 0
#define SFS_TYPE_FILE     1
#define SFS_TYPE_DIR      2

// Default filetype for unknown
#define SFS_FILETYPE_DATA 0xFFD
#define SFS_FILETYPE_TEXT 0xFFF
#define SFS_FILETYPE_DIR  0x1000

// RISC OS epoch: 1900-01-01 00:00:00
// Unix epoch: 1970-01-01 00:00:00
// Difference in seconds: 2208988800
#define SFS_EPOCH_DIFF 2208988800ULL
// Centiseconds per second
#define SFS_CS_PER_SEC 100ULL

// Convert Unix time_t to RISC OS 5-byte centiseconds
static inline uint64_t sfs_time_to_riscos(time_t t) {
    uint64_t cs = ((uint64_t)t + SFS_EPOCH_DIFF) * SFS_CS_PER_SEC;
    return cs;
}

// Convert RISC OS centiseconds to Unix time_t
static inline time_t sfs_time_from_riscos(uint64_t cs) {
    return (time_t)((cs / SFS_CS_PER_SEC) - SFS_EPOCH_DIFF);
}

// Clamp a file size to the 32-bit length the wire protocol carries.
//
// Takes a fixed-width 64-bit size deliberately. off_t is NOT 64 bits
// everywhere: on Windows it is long, which is 32 bits under LLP64, so the
// obvious `size > (off_t)0xFFFFFFFF` truncates its own limit to -1 and then
// every file compares greater and is reported as 4G. That was issue #22, where
// a 7.5K file arrived at the client claiming to be four gigabytes.
//
// A negative size is not reachable from a successful stat() on a 64-bit off_t,
// but a file above 2GB overflows a signed 32-bit one. Report those as zero: it
// is still wrong, but it does not ask the client to transfer 4GB it cannot.
static inline uint32_t sfs_length_for_wire(int64_t size) {
    if (size <= 0)
        return 0;
    if ((uint64_t)size > 0xFFFFFFFFu)
        return 0xFFFFFFFFu;
    return (uint32_t)size;
}

// Build load address from filetype and timestamp
static inline uint32_t sfs_make_load_addr(uint32_t filetype, uint64_t cs) {
    return 0xFFF00000u | ((filetype & 0xFFF) << 8) | ((cs >> 32) & 0xFF);
}

// Build exec address (low 4 bytes of timestamp)
static inline uint32_t sfs_make_exec_addr(uint64_t cs) {
    return (uint32_t)(cs & 0xFFFFFFFFu);
}

// Extract filetype from load address
static inline uint32_t sfs_get_filetype(uint32_t load) {
    if ((load & 0xFFF00000u) != 0xFFF00000u) return SFS_FILETYPE_DATA;
    return (load >> 8) & 0xFFF;
}

// Convert Unix mode to RISC OS attributes
static inline uint32_t sfs_mode_to_attrs(unsigned int mode) {
    uint32_t attrs = 0;
    if (mode & 0400) attrs |= SFS_ATTR_R;
    if (mode & 0200) attrs |= SFS_ATTR_W;
    if (mode & 0004) attrs |= SFS_ATTR_r;
    if (mode & 0002) attrs |= SFS_ATTR_w;
    return attrs;
}

// Filetype from extension (basic mapping)
uint32_t sfs_filetype_from_ext(const char *filename, const sfs_config *cfg);

// Extract filetype from ,xxx suffix (returns -1 if not present)
// e.g., "myfile,fff" returns 0xFFF
int sfs_filetype_from_suffix(const char *filename);

// Strip ,xxx suffix from filename for display
// Returns a pointer into the filename string, or the original if no suffix
// Writes the stripped name to out_buf if provided (max out_sz chars)
void sfs_strip_type_suffix(const char *filename, char *out_buf, size_t out_sz);

// Append ,xxx suffix to a path based on filetype
// out must have space for path + ",xxx" (4 extra chars + null)
void sfs_append_type_suffix(const char *path, uint32_t filetype, char *out, size_t out_sz);

// Check path for traversal attacks
int sfs_path_is_safe(const char *path);

#endif
