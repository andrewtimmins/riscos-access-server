// ShareFS Server - Handle Management
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "handle.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int make_token(void) {
    return (rand() & 0x7fff) + 1;
}

int sfs_handles_init(sfs_handle_table *t) {
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    t->next_id = 1; // 0 reserved for root
    return 0;
}

static void free_dir_entries(sfs_handle *h) {
    if (!h || !h->dir_entries) return;
    for (size_t i = 0; i < h->dir_entry_count; i++)
        free(h->dir_entries[i].name);
    free(h->dir_entries);
    h->dir_entries = NULL;
    h->dir_entry_count = 0;
}

void sfs_handles_free(sfs_handle_table *t) {
    if (!t) return;
    for (size_t i = 0; i < t->count; ++i) {
        free(t->items[i].path);
        free_dir_entries(&t->items[i]);
    }
    free(t->items);
    free(t->dead_handles);
    memset(t, 0, sizeof(*t));
}

int sfs_handles_add(sfs_handle_table *t, sfs_handle_type type, int fd, int *out_id, int *out_token) {
    return sfs_handles_add_ex(t, type, fd, NULL, 0, 0, 0, 0, out_id, out_token);
}

int sfs_handles_add_ex(sfs_handle_table *t, sfs_handle_type type, int fd, const char *path,
                       uint32_t load, uint32_t exec, uint32_t len, uint32_t attrs,
                       int *out_id, int *out_token) {
    if (!t) return -1;
    size_t n = t->count + 1;
    sfs_handle *p = (sfs_handle *)realloc(t->items, n * sizeof(sfs_handle));
    if (!p) return -1;
    t->items = p;
    sfs_handle *h = &t->items[n - 1];
    memset(h, 0, sizeof(*h));
    h->id = t->next_id++;
    h->token = make_token();
    h->type = type;
    h->fd = fd;
    h->seq_ptr = 0;
    h->load_addr = load;
    h->exec_addr = exec;
    h->length = len;
    h->attrs = attrs;
    h->dir_entries = NULL;
    h->dir_entry_count = 0;
    h->client_addr[0] = '\0';
    h->client_port = 0;
    if (path) {
        h->path = (char *)malloc(strlen(path) + 1);
        if (h->path) strcpy(h->path, path);
    }
    t->count = n;
    if (out_id) *out_id = h->id;
    if (out_token) *out_token = h->token;
    return 0;
}

int sfs_handles_close(sfs_handle_table *t, int id, int token) {
    if (!t) return -1;
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].id == id && t->items[i].token == token) {
            // Track dead handle
            int *d = (int *)realloc(t->dead_handles, (t->dead_count + 1) * sizeof(int));
            if (d) {
                t->dead_handles = d;
                t->dead_handles[t->dead_count++] = id;
            }
            free(t->items[i].path);
            free_dir_entries(&t->items[i]);
            t->items[i] = t->items[t->count - 1];
            t->count -= 1;
            if (t->count == 0) {
                free(t->items);
                t->items = NULL;
            }
            return 0;
        }
    }
    return -1;
}

sfs_handle *sfs_handles_lookup(sfs_handle_table *t, int id, int token) {
    if (!t) return NULL;
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].id == id && t->items[i].token == token) {
            return &t->items[i];
        }
    }
    return NULL;
}

void sfs_handle_set_client(sfs_handle_table *t, int id,
                           const char *addr, unsigned short port) {
    if (!t || !addr) return;
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].id == id) {
            strncpy(t->items[i].client_addr, addr,
                    sizeof(t->items[i].client_addr) - 1);
            t->items[i].client_addr[sizeof(t->items[i].client_addr) - 1] = '\0';
            t->items[i].client_port = port;
            return;
        }
    }
}

int sfs_handles_get_for_client(sfs_handle_table *t, int id,
                               const char *addr, unsigned short port,
                               sfs_handle **out) {
    if (!t || !out || !addr) return -1;
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].id == id) {
            if (t->items[i].client_port != port ||
                strcmp(t->items[i].client_addr, addr) != 0) {
                *out = NULL;
                return -1;
            }
            *out = &t->items[i];
            return 0;
        }
    }
    *out = NULL;
    return -1;
}

// Lookup by ID only (no token check)
int sfs_handles_get(sfs_handle_table *t, int id, sfs_handle **out) {
    if (!t || !out) return -1;
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].id == id) {
            *out = &t->items[i];
            return 0;
        }
    }
    *out = NULL;
    return -1;
}

// Close by ID only (no token check)
int sfs_handles_remove(sfs_handle_table *t, int id) {
    if (!t) return -1;
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].id == id) {
            // Track dead handle
            int *d = (int *)realloc(t->dead_handles, (t->dead_count + 1) * sizeof(int));
            if (d) {
                t->dead_handles = d;
                t->dead_handles[t->dead_count++] = id;
            }
            if (t->items[i].fd >= 0) close(t->items[i].fd);
            free(t->items[i].path);
            free_dir_entries(&t->items[i]);
            t->items[i] = t->items[t->count - 1];
            t->count -= 1;
            if (t->count == 0) {
                free(t->items);
                t->items = NULL;
            }
            return 0;
        }
    }
    return -1;
}

void sfs_handles_clear_dead(sfs_handle_table *t) {
    if (!t) return;
    free(t->dead_handles);
    t->dead_handles = NULL;
    t->dead_count = 0;
}

const int *sfs_handles_get_dead(sfs_handle_table *t, size_t *out_count) {
    if (!t) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    if (out_count) *out_count = t->dead_count;
    return t->dead_handles;
}

void sfs_handle_set_dir_listing(sfs_handle_table *t, int id,
                                sfs_dir_entry *entries, size_t count) {
    if (!t) return;
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].id == id) {
            free_dir_entries(&t->items[i]);
            t->items[i].dir_entries = entries;
            t->items[i].dir_entry_count = count;
            return;
        }
    }
    // Handle not found: caller owns the memory, free it
    if (entries) {
        for (size_t i = 0; i < count; i++)
            free(entries[i].name);
        free(entries);
    }
}
