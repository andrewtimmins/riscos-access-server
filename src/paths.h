/*
  ShareFS Server - Configuration File Locations

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

// Where the configuration lives, answered once for the whole product.
//
// The server and the GUI used to each carry their own search list, and the two
// lists disagreed: the GUI looked in $XDG_CONFIG_HOME first and the server
// never looked there at all, and on Windows they tried ProgramData and
// C:\ShareFS in opposite orders. With a file in both places, saving in the GUI
// changed a file the service never read, and the user was told the setting had
// been saved. Everything that needs to find the configuration now calls in
// here.
//
// The rule is: an explicitly given path always wins, then the system-wide
// location, then the per-user one, then the current directory. System-wide
// comes first because that is what a service started by systemd or the Windows
// SCM reads, and the answer has to be the same one the GUI shows.

#ifndef SFS_PATHS_H
#define SFS_PATHS_H

#include <stddef.h>

// Longest path this module will construct. Config paths are not subject to the
// 512-byte RISC OS path budget, so this is simply generous.
#define SFS_PATH_MAX 1024

// Number of candidates sfs_paths_config_candidates can return.
#define SFS_MAX_CONFIG_CANDIDATES 6

// Fill `out` with the configuration paths to search, most preferred first, and
// return how many were written. Each entry is at most SFS_PATH_MAX bytes.
// `max` is the number of rows in `out`.
size_t sfs_paths_config_candidates(char out[][SFS_PATH_MAX], size_t max);

// Find an existing configuration file. Returns 0 and fills `out` with the
// first candidate that exists and can be read, or -1 if there is none.
int sfs_paths_find_config(char *out, size_t out_sz);

// The path a fresh installation should create, which is the system-wide
// location when this process could write it and the per-user one otherwise.
// Always succeeds; returns 0, or -1 only if `out` is too small.
int sfs_paths_default_config(char *out, size_t out_sz);

// The folder a first run should offer to share: somewhere the user can write
// without becoming root, and named so it is obvious what it is for.
int sfs_paths_default_share(char *out, size_t out_sz);

// Create `path` and any missing parents, as mkdir -p does. Returns 0 if the
// directory exists afterwards, -1 otherwise.
int sfs_paths_mkdir_p(const char *path);

// Write a starter configuration to `path`, creating parent directories and the
// share folder it names. Refuses to overwrite an existing file. Returns 0 on
// success; on failure returns -1 and, if `err` is given, fills it with
// something worth showing the user.
//
// `share_name` and `share_path` may be NULL, in which case the default share
// from sfs_paths_default_share is used.
int sfs_paths_write_default_config(const char *path, const char *share_name,
                                   const char *share_path, char *err,
                                   size_t err_sz);

#endif
