// RISC OS Access/ShareFS Server - Printer Support
// Author: Andrew Timmins
// License: GPL-3.0-only

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
    if (ras_mkdir(path) == 0) return 0;
    return 0; // best effort; no stat check to keep minimal dependencies
}

static int replace_cmd(const char *tmpl, const char *filepath, char *out, size_t out_sz) {
    if (!tmpl || !filepath || !out || out_sz == 0) return -1;
    const char *needle = strstr(tmpl, "%f");
    if (!needle) {
        snprintf(out, out_sz, "%s", tmpl);
        return 0;
    }
    size_t prefix = (size_t)(needle - tmpl);
    size_t suffix = strlen(needle + 2);
    size_t file_len = strlen(filepath);
    if (prefix + file_len + suffix + 1 > out_sz) return -1;
    memcpy(out, tmpl, prefix);
    memcpy(out + prefix, filepath, file_len);
    memcpy(out + prefix + file_len, needle + 2, suffix + 1);
    return 0;
}

static int process_spool(const ras_printer_config *p) {
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
            ras_log(RAS_LOG_ERROR, "printer %s command too long", p->name);
            remove(queue);
            continue;
        }

        int rc = system(cmd);
        if (rc != 0) {
            ras_log(RAS_LOG_ERROR, "printer %s command failed rc=%d", p->name, rc);
        }

        remove(queue);
    }

    closedir(d);
    return 0;
}

static time_t *g_next_poll = NULL;
static size_t g_poll_count = 0;

void ras_printers_poll(const ras_config *cfg) {
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
        const ras_printer_config *p = &cfg->printers[i];
        int interval = p->poll_interval > 0 ? p->poll_interval : 5;
        if (g_next_poll && now >= g_next_poll[i]) {
            process_spool(p);
            g_next_poll[i] = now + interval;
        }
    }
}

void ras_printers_shutdown(void) {
    free(g_next_poll);
    g_next_poll = NULL;
    g_poll_count = 0;
}

int ras_printers_setup(const ras_config *cfg) {
    if (!cfg) return -1;
    for (size_t i = 0; i < cfg->printer_count; ++i) {
        const ras_printer_config *p = &cfg->printers[i];
        if (!p->name || !p->path || !p->definition) {
            ras_log(RAS_LOG_ERROR, "printer missing fields");
            continue;
        }

        ensure_dir(p->path);

        char defn_path[512];
        snprintf(defn_path, sizeof(defn_path), "%s/%s.fc6", p->path, p->name);
        if (copy_file(p->definition, defn_path) != 0) {
            ras_log(RAS_LOG_ERROR, "failed to copy printer definition for %s", p->name);
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
