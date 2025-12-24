// RISC OS Access/ShareFS Server - Configuration
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUF 512

static char *ras_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = (char *)malloc(len + 1);
    if (p) {
        memcpy(p, s, len + 1);
    }
    return p;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static void free_share(ras_share_config *s) {
    if (!s) return;
    free(s->name);
    free(s->path);
    free(s->password);
    free(s->default_type);
}

static void free_printer(ras_printer_config *p) {
    if (!p) return;
    free(p->name);
    free(p->path);
    free(p->definition);
    free(p->description);
    free(p->command);
}

static void free_mime(ras_mime_entry *m) {
    if (!m) return;
    free(m->ext);
    free(m->filetype);
}

static int grow_shares(ras_config *cfg) {
    size_t n = cfg->share_count + 1;
    ras_share_config *p = (ras_share_config *)realloc(cfg->shares, n * sizeof(ras_share_config));
    if (!p) return -1;
    cfg->shares = p;
    memset(&cfg->shares[n - 1], 0, sizeof(ras_share_config));
    cfg->share_count = n;
    return 0;
}

static int grow_printers(ras_config *cfg) {
    size_t n = cfg->printer_count + 1;
    ras_printer_config *p = (ras_printer_config *)realloc(cfg->printers, n * sizeof(ras_printer_config));
    if (!p) return -1;
    cfg->printers = p;
    memset(&cfg->printers[n - 1], 0, sizeof(ras_printer_config));
    cfg->printer_count = n;
    return 0;
}

static int grow_mime(ras_config *cfg) {
    size_t n = cfg->mimemap_count + 1;
    ras_mime_entry *p = (ras_mime_entry *)realloc(cfg->mimemap, n * sizeof(ras_mime_entry));
    if (!p) return -1;
    cfg->mimemap = p;
    memset(&cfg->mimemap[n - 1], 0, sizeof(ras_mime_entry));
    cfg->mimemap_count = n;
    return 0;
}

static int parse_int(const char *s, int *out) {
    if (!s || !out) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

static int str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static uint32_t parse_share_attrs(const char *val) {
    uint32_t attrs = 0;
    if (!val) return attrs;
    char *dup = ras_strdup(val);
    if (!dup) return attrs;
    char *tok = strtok(dup, ", \t");
    while (tok) {
        if (str_ieq(tok, "protected")) attrs |= RAS_ATTR_PROTECTED;
        else if (str_ieq(tok, "readonly")) attrs |= RAS_ATTR_READONLY;
        else if (str_ieq(tok, "hidden")) attrs |= RAS_ATTR_HIDDEN;
        else if (str_ieq(tok, "subdir")) attrs |= RAS_ATTR_SUBDIR;
        else if (str_ieq(tok, "cdrom")) attrs |= RAS_ATTR_CDROM;
        tok = strtok(NULL, ", \t");
    }
    free(dup);
    return attrs;
}

static int parse_section(const char *label, char *kind, size_t kind_sz, char *name, size_t name_sz) {
    const char *colon = strchr(label, ':');
    if (colon) {
        size_t klen = (size_t)(colon - label);
        size_t nlen = strlen(colon + 1);
        if (klen == 0 || klen + 1 > kind_sz || nlen + 1 > name_sz) return -1;
        memcpy(kind, label, klen);
        kind[klen] = '\0';
        memcpy(name, colon + 1, nlen + 1);
    } else {
        size_t klen = strlen(label);
        if (klen + 1 > kind_sz) return -1;
        memcpy(kind, label, klen + 1);
        name[0] = '\0';
    }
    return 0;
}

int ras_config_load(const char *path, ras_config *out) {
    if (!path || !out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->server.log_level = ras_strdup("info");
    out->server.broadcast_interval = 30;
    out->server.access_plus = 1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        ras_config_unload(out);
        return -1;
    }

    char line[LINE_BUF];
    char section_kind[32] = {0};
    char section_name[64] = {0};
    int status = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *s = trim(line);
        if (*s == '\0' || *s == '#' || *s == ';') continue;

        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) continue;
            *end = '\0';
            if (parse_section(s + 1, section_kind, sizeof(section_kind), section_name, sizeof(section_name)) != 0) {
                continue;
            }

            if (strcmp(section_kind, "share") == 0) {
                if (grow_shares(out) != 0) { status = -1; break; }
                out->shares[out->share_count - 1].name = ras_strdup(section_name);
            } else if (strcmp(section_kind, "printer") == 0) {
                if (grow_printers(out) != 0) { status = -1; break; }
                out->printers[out->printer_count - 1].name = ras_strdup(section_name);
                out->printers[out->printer_count - 1].poll_interval = 5;
            }
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcmp(section_kind, "server") == 0) {
            if (strcmp(key, "log_level") == 0) {
                free(out->server.log_level);
                out->server.log_level = ras_strdup(val);
            } else if (strcmp(key, "broadcast_interval") == 0) {
                parse_int(val, &out->server.broadcast_interval);
            } else if (strcmp(key, "access_plus") == 0) {
                out->server.access_plus = (str_ieq(val, "true") || strcmp(val, "1") == 0) ? 1 : 0;
            }
        } else if (strcmp(section_kind, "share") == 0 && out->share_count > 0) {
            ras_share_config *c = &out->shares[out->share_count - 1];
            if (strcmp(key, "path") == 0) {
                free(c->path);
                c->path = ras_strdup(val);
            } else if (strcmp(key, "attributes") == 0) {
                c->attributes = parse_share_attrs(val);
            } else if (strcmp(key, "password") == 0) {
                free(c->password);
                c->password = ras_strdup(val);
            } else if (strcmp(key, "default_filetype") == 0 || strcmp(key, "default_type") == 0) {
                free(c->default_type);
                c->default_type = ras_strdup(val);
            }
        } else if (strcmp(section_kind, "printer") == 0 && out->printer_count > 0) {
            ras_printer_config *p = &out->printers[out->printer_count - 1];
            if (strcmp(key, "path") == 0) {
                free(p->path);
                p->path = ras_strdup(val);
            } else if (strcmp(key, "definition") == 0) {
                free(p->definition);
                p->definition = ras_strdup(val);
            } else if (strcmp(key, "description") == 0) {
                free(p->description);
                p->description = ras_strdup(val);
            } else if (strcmp(key, "poll_interval") == 0) {
                parse_int(val, &p->poll_interval);
            } else if (strcmp(key, "command") == 0) {
                free(p->command);
                p->command = ras_strdup(val);
            }
        } else if (strcmp(section_kind, "mimemap") == 0) {
            if (grow_mime(out) != 0) { status = -1; break; }
            ras_mime_entry *m = &out->mimemap[out->mimemap_count - 1];
            m->ext = ras_strdup(key);
            m->filetype = ras_strdup(val);
        }
    }

    fclose(fp);
    if (status != 0) {
        ras_config_unload(out);
    }
    return status;
}

void ras_config_unload(ras_config *cfg) {
    if (!cfg) return;

    for (size_t i = 0; i < cfg->share_count; ++i) {
        free_share(&cfg->shares[i]);
    }
    free(cfg->shares);

    for (size_t i = 0; i < cfg->printer_count; ++i) {
        free_printer(&cfg->printers[i]);
    }
    free(cfg->printers);

    for (size_t i = 0; i < cfg->mimemap_count; ++i) {
        free_mime(&cfg->mimemap[i]);
    }
    free(cfg->mimemap);

    free(cfg->server.log_level);
    memset(cfg, 0, sizeof(*cfg));
}

int ras_config_validate(const ras_config *cfg) {
    if (!cfg) return -1;

    for (size_t i = 0; i < cfg->share_count; ++i) {
        const ras_share_config *s = &cfg->shares[i];
        if (!s->name || !s->path || s->name[0] == '\0' || s->path[0] == '\0') {
            return -1;
        }
    }

    for (size_t i = 0; i < cfg->printer_count; ++i) {
        const ras_printer_config *p = &cfg->printers[i];
        if (!p->name || !p->path || !p->definition || !p->command ||
            p->name[0] == '\0' || p->path[0] == '\0' || p->definition[0] == '\0' || p->command[0] == '\0') {
            return -1;
        }
    }

    return 0;
}
