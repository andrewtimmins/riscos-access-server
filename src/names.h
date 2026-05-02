// RISC OS Access/ShareFS Server - Filename Encoding
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_NAMES_H
#define RAS_NAMES_H

#include <stddef.h>

// Encode a single RISC OS filename component for storage on the host filesystem.
//
// Character mapping applied:
//   RISC OS '/'  -> '.'          (slash-in-name becomes dot)
//   RISC OS ' '  -> 0xA0         (space becomes non-breaking space)
//   '%'          -> '%25'        (escape introducer, lossless)
//   '<' '>' ':' '"' '|' '?' '*' '\\'  -> %3C %3E %3A %22 %7C %3F %2A %5C
//   0x01-0x1F, 0x7F -> %XX       (control chars, forbidden on NTFS)
//   ','          -> '%2C'        (only when name ends in ,XXX 3 hex digits,
//                                 to avoid collision with ,xxx filetype suffix)
//   All other bytes pass through unchanged (incl. high-bit Latin-1).
//
// Returns 0 on success, -1 if out buffer is too small (output undefined).
// out_sz must be at least 1.
int ras_encode_host_name(const char *ros_name, char *out, size_t out_sz);

// Decode a host filesystem filename component back to a RISC OS display name.
// Reverses ras_encode_host_name:
//   '.'  -> '/'    (dot becomes RISC OS slash-in-name)
//   0xA0 -> ' '    (non-breaking space becomes space)
//   %XX  -> byte   (percent-decoded)
//   All other bytes pass through unchanged.
//
// Returns 0 on success, -1 if out buffer is too small (output undefined).
// out_sz must be at least 1.
int ras_decode_host_name(const char *host_name, char *out, size_t out_sz);

#endif
