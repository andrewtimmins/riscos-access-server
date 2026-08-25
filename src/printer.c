/*
  ShareFS Server - Printer Support

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

#include "printer.h"
#include "platform.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int ensure_dir(const char *path) {
    if (!path) return -1;
    // Attempt create; ignore if exists.
    if (sfs_mkdir(path) == 0) return 0;
    return 0; // best effort; no stat check to keep minimal dependencies
}

// Wrap s in single quotes for POSIX shell, escaping embedded single quotes.
// E.g. "foo'bar" -> "'foo'\\''bar'"
static int shell_quote(const char *s, char *out, size_t out_sz) {
    size_t o = 0;
    if (o >= out_sz) return -1;
    out[o++] = '\'';
    for (; *s; s++) {
        if (*s == '\'') {
            // End current quote, emit \', restart quote
            if (o + 4 >= out_sz) return -1;
            out[o++] = '\'';
            out[o++] = '\\';
            out[o++] = '\'';
            out[o++] = '\'';
        } else {
            if (o + 1 >= out_sz) return -1;
            out[o++] = *s;
        }
    }
    if (o + 2 > out_sz) return -1;
    out[o++] = '\'';
    out[o] = '\0';
    return 0;
}

static int replace_cmd(const char *tmpl, const char *filepath, char *out, size_t out_sz) {
    if (!tmpl || !filepath || !out || out_sz == 0) return -1;
    const char *needle = strstr(tmpl, "%f");
    if (!needle) {
        snprintf(out, out_sz, "%s", tmpl);
        return 0;
    }

    // Shell-quote the filepath before substituting it so that paths containing
    // spaces or other metacharacters are not misinterpreted by the shell.
    char quoted[2048];
    if (shell_quote(filepath, quoted, sizeof(quoted)) != 0) return -1;

    size_t prefix = (size_t)(needle - tmpl);
    size_t suffix = strlen(needle + 2);
    size_t quoted_len = strlen(quoted);
    if (prefix + quoted_len + suffix + 1 > out_sz) return -1;
    memcpy(out, tmpl, prefix);
    memcpy(out + prefix, quoted, quoted_len);
    memcpy(out + prefix + quoted_len, needle + 2, suffix + 1);
    return 0;
}

#ifdef _WIN32
#define SFS_DELETE_CMD "del /q"
#else
#define SFS_DELETE_CMD "rm -f"
#endif

static int process_spool(const sfs_printer_config *p) {
    char spool_dir[512];
    snprintf(spool_dir, sizeof(spool_dir), "%s/RemSpool", p->path);
    DIR *d = opendir(spool_dir);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char src[512];
        snprintf(src, sizeof(src), "%s/%s", spool_dir, ent->d_name);

        char queue[512];
        snprintf(queue, sizeof(queue), "%s/RemQueue/%s", p->path, ent->d_name);

        rename(src, queue);

        char cmd[1024];
        if (replace_cmd(p->command, queue, cmd, sizeof(cmd)) != 0) {
            sfs_log(SFS_LOG_ERROR, "printer %s command too long", p->name);
            remove(queue);
            continue;
        }

        // Launched detached: waiting here blocked every other client for as
        // long as the print command took to run. Because nothing waits for it,
        // the job file cannot be deleted here either, so the cleanup is
        // appended to the command and runs once it has finished.
        char quoted_queue[1024];
        char full[2048];
        if (shell_quote(queue, quoted_queue, sizeof(quoted_queue)) != 0) {
            sfs_log(SFS_LOG_ERROR, "printer %s spool path too long", p->name);
            remove(queue);
            continue;
        }

        int written = snprintf(full, sizeof(full), "%s ; %s %s", cmd,
                               SFS_DELETE_CMD, quoted_queue);
        if (written < 0 || written >= (int)sizeof(full)) {
            sfs_log(SFS_LOG_ERROR, "printer %s command too long", p->name);
            remove(queue);
            continue;
        }

        if (sfs_spawn_detached(full) != 0) {
            sfs_log(SFS_LOG_ERROR, "printer %s: could not run print command",
                    p->name);
            remove(queue);
        }
    }

    closedir(d);
    return 0;
}

static time_t *g_next_poll = NULL;
static size_t g_poll_count = 0;

void sfs_printers_poll(const sfs_config *cfg) {
    if (!cfg) return;
    if (!g_next_poll) {
        g_poll_count = cfg->printer_count;
        if (g_poll_count) {
            g_next_poll = (time_t *)calloc(g_poll_count, sizeof(time_t));
            time_t now = time(NULL);
            for (size_t i = 0; i < g_poll_count; ++i) {
                g_next_poll[i] = now;
            }
        }
    }

    time_t now = time(NULL);
    for (size_t i = 0; i < cfg->printer_count; ++i) {
        const sfs_printer_config *p = &cfg->printers[i];
        int interval = p->poll_interval > 0 ? p->poll_interval : 5;
        if (g_next_poll && now >= g_next_poll[i]) {
            process_spool(p);
            g_next_poll[i] = now + interval;
        }
    }
}

void sfs_printers_shutdown(void) {
    free(g_next_poll);
    g_next_poll = NULL;
    g_poll_count = 0;
}

int sfs_printers_setup(const sfs_config *cfg) {
    if (!cfg) return -1;
    for (size_t i = 0; i < cfg->printer_count; ++i) {
        const sfs_printer_config *p = &cfg->printers[i];
        if (!p->name || !p->path || !p->definition) {
            sfs_log(SFS_LOG_ERROR, "printer missing fields");
            continue;
        }

        ensure_dir(p->path);

        char defn_path[512];
        snprintf(defn_path, sizeof(defn_path), "%s/%s.fc6", p->path, p->name);
        if (copy_file(p->definition, defn_path) != 0) {
            sfs_log(SFS_LOG_ERROR, "failed to copy printer definition for %s", p->name);
        }

        char queue_path[512];
        snprintf(queue_path, sizeof(queue_path), "%s/RemQueue", p->path);
        ensure_dir(queue_path);

        char spool_path[512];
        snprintf(spool_path, sizeof(spool_path), "%s/RemSpool", p->path);
        ensure_dir(spool_path);
    }
    return 0;
}
