// RISC OS Access/ShareFS Server - RISC OS Type Conversion
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "riscos.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Built-in extension to filetype mapping
static const struct { const char *ext; uint32_t type; } builtin_map[] = {
    { "txt", 0xFFF },
    { "text", 0xFFF },
    { "bas", 0xFFB },
    { "c", 0xFFD },
    { "h", 0xFFD },
    { "s", 0xFFF },
    { "o", 0xFFE },
    { "pdf", 0xADF },
    { "png", 0xB60 },
    { "jpg", 0xC85 },
    { "jpeg", 0xC85 },
    { "gif", 0x695 },
    { "zip", 0xA91 },
    { "html", 0xFAF },
    { "htm", 0xFAF },
    { "css", 0xF79 },
    { "js", 0xF81 },
    { "json", 0xF79 },
    { "xml", 0xF80 },
    { "csv", 0xDFE },
    { "sprite", 0xFF9 },
    { "draw", 0xAFF },
    { "ff9", 0xFF9 },
    { "aff", 0xAFF },
    { NULL, 0 }
};

uint32_t ras_filetype_from_ext(const char *filename, const ras_config *cfg) {
    if (!filename) return RAS_FILETYPE_DATA;

    // Check for ,xxx suffix first (takes priority)
    int suffix_type = ras_filetype_from_suffix(filename);
    if (suffix_type >= 0) {
        return (uint32_t)suffix_type;
    }

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return RAS_FILETYPE_DATA;
    dot++;

    char ext_lower[16];
    size_t i;
    for (i = 0; i < sizeof(ext_lower) - 1 && dot[i]; ++i) {
        ext_lower[i] = (char)tolower((unsigned char)dot[i]);
    }
    ext_lower[i] = '\0';

    // Check config mimemap first
    if (cfg) {
        for (size_t j = 0; j < cfg->mimemap_count; ++j) {
            if (cfg->mimemap[j].ext && strcmp(cfg->mimemap[j].ext, ext_lower) == 0) {
                return (uint32_t)strtoul(cfg->mimemap[j].filetype, NULL, 16);
            }
        }
    }

    // Check builtin map
    for (int j = 0; builtin_map[j].ext; ++j) {
        if (strcmp(builtin_map[j].ext, ext_lower) == 0) {
            return builtin_map[j].type;
        }
    }

    return RAS_FILETYPE_DATA;
}

int ras_filetype_from_suffix(const char *filename) {
    if (!filename) return -1;
    
    // Look for ,xxx at the end (comma followed by exactly 3 hex digits)
    size_t len = strlen(filename);
    if (len < 4) return -1;
    
    const char *suffix = filename + len - 4;
    if (suffix[0] != ',') return -1;
    
    // Check for 3 hex digits
    for (int i = 1; i <= 3; i++) {
        char c = (char)tolower((unsigned char)suffix[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return -1;
        }
    }
    
    // Parse the hex value
    return (int)strtol(suffix + 1, NULL, 16);
}

void ras_strip_type_suffix(const char *filename, char *out_buf, size_t out_sz) {
    if (!filename || !out_buf || out_sz == 0) return;
    
    size_t len = strlen(filename);
    
    // Check if there's a ,xxx suffix
    if (len >= 4 && ras_filetype_from_suffix(filename) >= 0) {
        // Strip the suffix
        size_t copy_len = len - 4;
        if (copy_len >= out_sz) copy_len = out_sz - 1;
        memcpy(out_buf, filename, copy_len);
        out_buf[copy_len] = '\0';
    } else {
        // No suffix, copy as-is
        size_t copy_len = len;
        if (copy_len >= out_sz) copy_len = out_sz - 1;
        memcpy(out_buf, filename, copy_len);
        out_buf[copy_len] = '\0';
    }
}

void ras_append_type_suffix(const char *path, uint32_t filetype, char *out, size_t out_sz) {
    if (!path || !out || out_sz == 0) return;
    
    // Strip any existing suffix first
    size_t path_len = strlen(path);
    size_t base_len = path_len;
    
    if (path_len >= 4 && ras_filetype_from_suffix(path) >= 0) {
        base_len = path_len - 4;
    }
    
    // Build new path with suffix
    if (base_len + 5 >= out_sz) {
        // Not enough room, just copy what we can
        size_t copy_len = out_sz - 1;
        memcpy(out, path, copy_len);
        out[copy_len] = '\0';
        return;
    }
    
    memcpy(out, path, base_len);
    snprintf(out + base_len, out_sz - base_len, ",%03x", filetype & 0xFFF);
}

int ras_path_is_safe(const char *path) {
    if (!path) return 0;

    // Reject absolute paths
    if (path[0] == '/') return 0;

    // Reject .. components
    const char *p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.') {
            if (p[2] == '\0' || p[2] == '/' || p[2] == '\\') return 0;
        }
        // Skip to next component
        while (*p && *p != '/' && *p != '\\') p++;
        while (*p == '/' || *p == '\\') p++;
    }

    return 1;
}
