// RISC OS Access/ShareFS Server - Handle Management
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_HANDLE_H
#define RAS_HANDLE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    RAS_HANDLE_NONE = 0,
    RAS_HANDLE_FILE = 1,
    RAS_HANDLE_DIR  = 2
} ras_handle_type;

typedef struct {
    int id;
    int token;
    ras_handle_type type;
    int fd;
    uint32_t seq_ptr;      // Sequential pointer
    uint32_t load_addr;    // RISC OS load address
    uint32_t exec_addr;    // RISC OS exec address
    uint32_t length;       // File length at open time
    uint32_t attrs;        // RISC OS attributes
    char *path;            // Host path for directory handles
} ras_handle;

typedef struct {
    ras_handle *items;
    size_t count;
    int next_id;
    int *dead_handles;     // Recently closed handle IDs for RDEADHANDLES
    size_t dead_count;
} ras_handle_table;

int ras_handles_init(ras_handle_table *t);
void ras_handles_free(ras_handle_table *t);
int ras_handles_add(ras_handle_table *t, ras_handle_type type, int fd, int *out_id, int *out_token);
int ras_handles_add_ex(ras_handle_table *t, ras_handle_type type, int fd, const char *path,
                       uint32_t load, uint32_t exec, uint32_t len, uint32_t attrs,
                       int *out_id, int *out_token);
int ras_handles_close(ras_handle_table *t, int id, int token);
int ras_handles_get(ras_handle_table *t, int id, ras_handle **out);
int ras_handles_remove(ras_handle_table *t, int id);
ras_handle *ras_handles_lookup(ras_handle_table *t, int id, int token);
void ras_handles_clear_dead(ras_handle_table *t);
const int *ras_handles_get_dead(ras_handle_table *t, size_t *out_count);

#endif
