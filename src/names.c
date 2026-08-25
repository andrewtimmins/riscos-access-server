/*
  ShareFS Server - Filename Encoding

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

#include "names.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sfs_encode_host_name(const char *ros_name, char *out, size_t out_sz) {
    if (!ros_name || !out || out_sz == 0)
        return -1;

    size_t len = strlen(ros_name);
    if (len == 0) {
        out[0] = '\0';
        return 0;
    }

    // If the name ends with ',' followed by exactly 3 hex digits (i.e. it
    // looks like a RISC OS filetype suffix), escape that comma so it doesn't
    // get mistaken for a real ,xxx suffix when listing host-side.
    int suffix_comma_pos = -1;
    if (len >= 4) {
        size_t ci = len - 4;
        if (ros_name[ci] == ',' &&
            isxdigit((unsigned char)ros_name[ci + 1]) &&
            isxdigit((unsigned char)ros_name[ci + 2]) &&
            isxdigit((unsigned char)ros_name[ci + 3])) {
            suffix_comma_pos = (int)ci;
        }
    }

    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)ros_name[i];
        const char *esc = NULL;
        char escbuf[4];

        if (c == '/') {
            // RISC OS slash-in-name -> host dot
            if (o + 1 >= out_sz)
                return -1;
            out[o++] = '.';
            continue;
        }

        if (c == ' ') {
            // Space -> non-breaking space (preserves leading/trailing spaces)
            if (o + 1 >= out_sz)
                return -1;
            out[o++] = '\xa0';
            continue;
        }

        // Characters that need %XX escaping
        switch (c) {
        case '%':  esc = "%25"; break;
        case '<':  esc = "%3C"; break;
        case '>':  esc = "%3E"; break;
        case ':':  esc = "%3A"; break;
        case '"':  esc = "%22"; break;
        case '|':  esc = "%7C"; break;
        case '?':  esc = "%3F"; break;
        case '*':  esc = "%2A"; break;
        case '\\': esc = "%5C"; break;
        case ',':
            if ((int)i == suffix_comma_pos)
                esc = "%2C";
            break;
        default:
            if (c < 0x20 || c == 0x7F) {
                snprintf(escbuf, sizeof(escbuf), "%%%02X", c);
                esc = escbuf;
            }
            break;
        }

        if (esc) {
            size_t esc_len = strlen(esc);
            if (o + esc_len >= out_sz)
                return -1;
            memcpy(out + o, esc, esc_len);
            o += esc_len;
        } else {
            if (o + 1 >= out_sz)
                return -1;
            out[o++] = (char)c;
        }
    }

    if (o >= out_sz)
        return -1;
    out[o] = '\0';
    return 0;
}

int sfs_decode_host_name(const char *host_name, char *out, size_t out_sz) {
    if (!host_name || !out || out_sz == 0)
        return -1;

    size_t o = 0;
    for (const char *p = host_name; *p; p++) {
        unsigned char c = (unsigned char)*p;

        if (c == '.') {
            // Host dot -> RISC OS slash-in-name
            if (o + 1 >= out_sz)
                return -1;
            out[o++] = '/';
        } else if (c == 0xa0u) {
            // Non-breaking space -> space
            if (o + 1 >= out_sz)
                return -1;
            out[o++] = ' ';
        } else if (c == '%' &&
                   isxdigit((unsigned char)p[1]) &&
                   isxdigit((unsigned char)p[2])) {
            // %XX -> decoded byte
            if (o + 1 >= out_sz)
                return -1;
            char hex[3] = {p[1], p[2], '\0'};
            out[o++] = (char)(unsigned char)strtoul(hex, NULL, 16);
            p += 2;
        } else {
            if (o + 1 >= out_sz)
                return -1;
            out[o++] = (char)c;
        }
    }

    if (o >= out_sz)
        return -1;
    out[o] = '\0';
    return 0;
}
