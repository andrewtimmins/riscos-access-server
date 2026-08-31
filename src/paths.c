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

#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <io.h>
#include <windows.h>
#define SFS_SEP '\\'
#else
#include <unistd.h>
#define SFS_SEP '/'
#endif

// Append a fixed path to the caller's array if there is room for it.
static void add_literal(char out[][SFS_PATH_MAX], size_t max, size_t *count,
                        const char *path) {
  if (*count >= max || !path || !path[0])
    return;
  int n = snprintf(out[*count], SFS_PATH_MAX, "%s", path);
  if (n > 0 && (size_t)n < SFS_PATH_MAX)
    (*count)++;
}

// Append a path built from a directory that may not be set. An unset or empty
// environment variable names no location, so the candidate is skipped rather
// than being built from an empty string.
static void add_under(char out[][SFS_PATH_MAX], size_t max, size_t *count,
                      const char *dir, const char *tail) {
  if (*count >= max || !dir || !dir[0])
    return;
  int n = snprintf(out[*count], SFS_PATH_MAX, "%s%s", dir, tail);
  if (n > 0 && (size_t)n < SFS_PATH_MAX)
    (*count)++;
}

// $XDG_CONFIG_HOME, or ~/.config when it is unset or empty. Only the Linux
// paths use it; macOS keeps per-user files under Library instead.
#if !defined(_WIN32) && !defined(__APPLE__)
static const char *xdg_config_home(char *buf, size_t buf_sz) {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0])
    return xdg;
  const char *home = getenv("HOME");
  if (!home || !home[0])
    return NULL;
  int n = snprintf(buf, buf_sz, "%s/.config", home);
  if (n <= 0 || (size_t)n >= buf_sz)
    return NULL;
  return buf;
}
#endif

size_t sfs_paths_config_candidates(char out[][SFS_PATH_MAX], size_t max) {
  size_t count = 0;
  if (!out || max == 0)
    return 0;

#ifdef _WIN32
  // ProgramData is the machine-wide location a service can read; C:\ShareFS is
  // where older versions put it and is kept so an upgrade keeps working.
  add_under(out, max, &count, getenv("ProgramData"), "\\ShareFS\\sharefs.conf");
  add_literal(out, max, &count, "C:\\ProgramData\\ShareFS\\sharefs.conf");
  add_literal(out, max, &count, "C:\\ShareFS\\sharefs.conf");
  add_under(out, max, &count, getenv("APPDATA"), "\\ShareFS\\sharefs.conf");
#elif defined(__APPLE__)
  // Homebrew and /usr/local are listed because they were documented as the
  // place to put the file, so installations already have one there.
  add_literal(out, max, &count, "/opt/homebrew/etc/sharefs.conf");
  add_literal(out, max, &count, "/usr/local/etc/sharefs.conf");
  add_literal(out, max, &count, "/etc/sharefs.conf");
  add_under(out, max, &count, getenv("HOME"),
            "/Library/Application Support/ShareFS/sharefs.conf");
#else
  add_literal(out, max, &count, "/etc/sharefs.conf");
  {
    char xdg_buf[SFS_PATH_MAX];
    const char *xdg = xdg_config_home(xdg_buf, sizeof(xdg_buf));
    add_under(out, max, &count, xdg, "/sharefs/sharefs.conf");
  }
#endif

  // Last resort, and the one that makes an unpacked archive work in place.
  add_literal(out, max, &count, "sharefs.conf");
  return count;
}

int sfs_paths_find_config(char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return -1;

  char candidates[SFS_MAX_CONFIG_CANDIDATES][SFS_PATH_MAX];
  size_t count = sfs_paths_config_candidates(candidates, SFS_MAX_CONFIG_CANDIDATES);

  for (size_t i = 0; i < count; ++i) {
    FILE *f = fopen(candidates[i], "r");
    if (!f)
      continue;
    fclose(f);
    size_t len = strlen(candidates[i]);
    if (len + 1 > out_sz)
      return -1;
    memcpy(out, candidates[i], len + 1);
    return 0;
  }
  return -1;
}

// Whether this process could create a file in the given directory. Tested by
// asking the operating system rather than by looking at the user id, because
// group membership and ACLs both decide it too.
//
// macOS keeps everything under the user's home directory, so nothing there
// needs to ask; the guard keeps the compiler from warning about it.
#if defined(_WIN32) || !defined(__APPLE__)
static int dir_is_writable(const char *dir) {
#ifdef _WIN32
  return _access(dir, 2 /* W_OK */) == 0;
#else
  return access(dir, W_OK) == 0;
#endif
}
#endif

int sfs_paths_default_config(char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return -1;

#ifdef _WIN32
  const char *program_data = getenv("ProgramData");
  char dir[SFS_PATH_MAX];
  snprintf(dir, sizeof(dir), "%s",
           (program_data && program_data[0]) ? program_data : "C:\\ProgramData");
  if (dir_is_writable(dir)) {
    int n = snprintf(out, out_sz, "%s\\ShareFS\\sharefs.conf", dir);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
  const char *appdata = getenv("APPDATA");
  if (appdata && appdata[0]) {
    int n = snprintf(out, out_sz, "%s\\ShareFS\\sharefs.conf", appdata);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
  int n = snprintf(out, out_sz, "sharefs.conf");
  return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
#elif defined(__APPLE__)
  // The app is the product on macOS, so the per-user location is the normal
  // answer: it needs no administrator rights and it is backed up with the
  // user's home directory.
  const char *home = getenv("HOME");
  if (home && home[0]) {
    int n = snprintf(out, out_sz, "%s/Library/Application Support/ShareFS/sharefs.conf",
                     home);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
  int n = snprintf(out, out_sz, "/usr/local/etc/sharefs.conf");
  return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
#else
  // On Linux the packaged install runs as a system service reading /etc, so
  // prefer that when we are the sort of process that can write it, and fall
  // back to the per-user file for a tarball install run by an ordinary user.
  if (dir_is_writable("/etc")) {
    int n = snprintf(out, out_sz, "/etc/sharefs.conf");
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
  char xdg_buf[SFS_PATH_MAX];
  const char *xdg = xdg_config_home(xdg_buf, sizeof(xdg_buf));
  if (xdg) {
    int n = snprintf(out, out_sz, "%s/sharefs/sharefs.conf", xdg);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
  int n = snprintf(out, out_sz, "sharefs.conf");
  return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
#endif
}

int sfs_paths_default_share(char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return -1;

#ifdef _WIN32
  const char *public_dir = getenv("PUBLIC");
  if (public_dir && public_dir[0]) {
    int n = snprintf(out, out_sz, "%s\\ShareFS", public_dir);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
  int n = snprintf(out, out_sz, "C:\\ShareFS\\Public");
  return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
#else
#ifndef __APPLE__
  // The Debian package creates this and the systemd unit already grants write
  // access to it, so a root-run first start should land there.
  if (dir_is_writable("/srv")) {
    int n = snprintf(out, out_sz, "/srv/sharefs/Public");
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
#endif
  const char *home = getenv("HOME");
  if (home && home[0]) {
    int n = snprintf(out, out_sz, "%s/Public/ShareFS", home);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
  }
  int n = snprintf(out, out_sz, "/tmp/ShareFS");
  return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
#endif
}

int sfs_paths_mkdir_p(const char *path) {
  if (!path || !path[0])
    return -1;

  char work[SFS_PATH_MAX];
  size_t len = strlen(path);
  if (len + 1 > sizeof(work))
    return -1;
  memcpy(work, path, len + 1);

  // Walk the string creating each prefix in turn. Starting at index 1 leaves a
  // leading separator, or a Windows drive letter, alone.
  for (size_t i = 1; i <= len; ++i) {
    char c = work[i];
    int at_end = (i == len);
    if (!at_end && c != '/' && c != SFS_SEP)
      continue;
#ifdef _WIN32
    // "C:" on its own is not a directory anyone can create.
    if (i == 2 && work[1] == ':')
      continue;
#endif
    if (!at_end)
      work[i] = '\0';
#ifdef _WIN32
    if (_mkdir(work) != 0) {
#else
    if (mkdir(work, 0755) != 0) {
#endif
      struct stat st;
      if (stat(work, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (!at_end)
          work[i] = c;
        return -1;
      }
    }
    if (!at_end)
      work[i] = c;
  }
  return 0;
}

// Strip the last component from a path, leaving the directory holding it.
static void parent_dir(const char *path, char *out, size_t out_sz) {
  size_t len = strlen(path);
  size_t cut = 0;
  for (size_t i = 0; i < len; ++i) {
    if (path[i] == '/' || path[i] == SFS_SEP)
      cut = i;
  }
  if (cut == 0) {
    snprintf(out, out_sz, ".");
    return;
  }
  if (cut + 1 > out_sz)
    cut = out_sz - 1;
  memcpy(out, path, cut);
  out[cut] = '\0';
}

int sfs_paths_write_default_config(const char *path, const char *share_name,
                                   const char *share_path, char *err,
                                   size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  if (!path || !path[0]) {
    if (err && err_sz)
      snprintf(err, err_sz, "No configuration path was given.");
    return -1;
  }

  FILE *existing = fopen(path, "r");
  if (existing) {
    fclose(existing);
    if (err && err_sz)
      snprintf(err, err_sz, "%s already exists.", path);
    return -1;
  }

  char share_buf[SFS_PATH_MAX];
  if (!share_path || !share_path[0]) {
    if (sfs_paths_default_share(share_buf, sizeof(share_buf)) != 0) {
      if (err && err_sz)
        snprintf(err, err_sz, "Could not work out where to put the first share.");
      return -1;
    }
    share_path = share_buf;
  }
  if (!share_name || !share_name[0])
    share_name = "Public";

  char dir[SFS_PATH_MAX];
  parent_dir(path, dir, sizeof(dir));
  if (sfs_paths_mkdir_p(dir) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not create %s.", dir);
    return -1;
  }

  // A share that does not exist on disk is announced and then fails on the
  // first access, which looks like a protocol fault rather than a missing
  // folder, so create it here.
  if (sfs_paths_mkdir_p(share_path) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not create the share folder %s.", share_path);
    return -1;
  }

  FILE *f = fopen(path, "w");
  if (!f) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not write %s.", path);
    return -1;
  }

  fprintf(f, "# ShareFS configuration, created on first run.\n");
  fprintf(f, "#\n");
  fprintf(f, "# Edit it in the ShareFS window, or by hand. File extensions are\n");
  fprintf(f, "# mapped to RISC OS filetypes from a built-in table unless a\n");
  fprintf(f, "# [mimemap] section overrides it; see sharefs.conf.sample for the\n");
  fprintf(f, "# full set of options.\n\n");
  fprintf(f, "[server]\n");
  fprintf(f, "log_level = info\n");
  fprintf(f, "broadcast_interval = 3\n");
  fprintf(f, "access_plus = true\n\n");
  fprintf(f, "[share:%s]\n", share_name);
  fprintf(f, "path = %s\n", share_path);

  int ok = (fflush(f) == 0);
  if (fclose(f) != 0)
    ok = 0;
  if (!ok) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not finish writing %s.", path);
    return -1;
  }
  return 0;
}
