// RISC OS Access/ShareFS Server - RISC OS Type Conversion
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_RISCOS_H
#define RAS_RISCOS_H

#include <stdint.h>
#include <time.h>
#include "config.h"

// RISC OS file attributes
#define RAS_ATTR_R  0x01  // Owner readable
#define RAS_ATTR_W  0x02  // Owner writable
#define RAS_ATTR_L  0x08  // Locked
#define RAS_ATTR_r  0x10  // Public readable
#define RAS_ATTR_w  0x20  // Public writable

// Object types
#define RAS_TYPE_NOTFOUND 0
#define RAS_TYPE_FILE     1
#define RAS_TYPE_DIR      2

// Default filetype for unknown
#define RAS_FILETYPE_DATA 0xFFD
#define RAS_FILETYPE_TEXT 0xFFF
#define RAS_FILETYPE_DIR  0x1000

// RISC OS epoch: 1900-01-01 00:00:00
// Unix epoch: 1970-01-01 00:00:00
// Difference in seconds: 2208988800
#define RAS_EPOCH_DIFF 2208988800ULL
// Centiseconds per second
#define RAS_CS_PER_SEC 100ULL

// Convert Unix time_t to RISC OS 5-byte centiseconds
static inline uint64_t ras_time_to_riscos(time_t t) {
    uint64_t cs = ((uint64_t)t + RAS_EPOCH_DIFF) * RAS_CS_PER_SEC;
    return cs;
}

// Convert RISC OS centiseconds to Unix time_t
static inline time_t ras_time_from_riscos(uint64_t cs) {
    return (time_t)((cs / RAS_CS_PER_SEC) - RAS_EPOCH_DIFF);
}

// Build load address from filetype and timestamp
static inline uint32_t ras_make_load_addr(uint32_t filetype, uint64_t cs) {
    return 0xFFF00000u | ((filetype & 0xFFF) << 8) | ((cs >> 32) & 0xFF);
}

// Build exec address (low 4 bytes of timestamp)
static inline uint32_t ras_make_exec_addr(uint64_t cs) {
    return (uint32_t)(cs & 0xFFFFFFFFu);
}

// Extract filetype from load address
static inline uint32_t ras_get_filetype(uint32_t load) {
    if ((load & 0xFFF00000u) != 0xFFF00000u) return RAS_FILETYPE_DATA;
    return (load >> 8) & 0xFFF;
}

// Convert Unix mode to RISC OS attributes
static inline uint32_t ras_mode_to_attrs(unsigned int mode) {
    uint32_t attrs = 0;
    if (mode & 0400) attrs |= RAS_ATTR_R;
    if (mode & 0200) attrs |= RAS_ATTR_W;
    if (mode & 0004) attrs |= RAS_ATTR_r;
    if (mode & 0002) attrs |= RAS_ATTR_w;
    return attrs;
}

// Filetype from extension (basic mapping)
uint32_t ras_filetype_from_ext(const char *filename, const ras_config *cfg);

// Extract filetype from ,xxx suffix (returns -1 if not present)
// e.g., "myfile,fff" returns 0xFFF
int ras_filetype_from_suffix(const char *filename);

// Strip ,xxx suffix from filename for display
// Returns a pointer into the filename string, or the original if no suffix
// Writes the stripped name to out_buf if provided (max out_sz chars)
void ras_strip_type_suffix(const char *filename, char *out_buf, size_t out_sz);

// Append ,xxx suffix to a path based on filetype
// out must have space for path + ",xxx" (4 extra chars + null)
void ras_append_type_suffix(const char *path, uint32_t filetype, char *out, size_t out_sz);

// Check path for traversal attacks
int ras_path_is_safe(const char *path);

#endif
