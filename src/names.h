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

#ifndef SFS_NAMES_H
#define SFS_NAMES_H

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
int sfs_encode_host_name(const char *ros_name, char *out, size_t out_sz);

// Decode a host filesystem filename component back to a RISC OS display name.
// Reverses sfs_encode_host_name:
//   '.'  -> '/'    (dot becomes RISC OS slash-in-name)
//   0xA0 -> ' '    (non-breaking space becomes space)
//   %XX  -> byte   (percent-decoded)
//   All other bytes pass through unchanged.
//
// Returns 0 on success, -1 if out buffer is too small (output undefined).
// out_sz must be at least 1.
int sfs_decode_host_name(const char *host_name, char *out, size_t out_sz);

#endif
