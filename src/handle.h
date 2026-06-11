// RISC OS Access/ShareFS Server - Handle Management
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_HANDLE_H
#define RAS_HANDLE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

typedef enum {
    RAS_HANDLE_NONE = 0,
    RAS_HANDLE_FILE = 1,
    RAS_HANDLE_DIR  = 2
} ras_handle_type;

// One cached directory entry (populated once at ROPENDIR, sorted, reused
// across all RREADDIR pagination calls to eliminate the O(N^2) re-scan).
typedef struct {
    char        *name;      // RISC OS display name (decoded, no ,xxx suffix)
    struct stat  st;        // stat captured at ROPENDIR time
    int          filetype;  // resolved RISC OS filetype
} ras_dir_entry;

typedef struct {
    int id;
    int token;
    ras_handle_type type;
    int fd;
    uint32_t seq_ptr;           // Sequential pointer
    uint32_t load_addr;         // RISC OS load address
    uint32_t exec_addr;         // RISC OS exec address
    uint32_t length;            // File length at open time
    uint32_t attrs;             // RISC OS attributes
    int open_flags;             // Flags used to open the file (for reopening)
    char *path;                 // Host path for directory handles
    ras_dir_entry *dir_entries; // Sorted cached listing (dir handles only)
    size_t dir_entry_count;
    char client_addr[64];       // IP address of the client that opened this handle
    unsigned short client_port; // Port of the client that opened this handle
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
int ras_handles_get_for_client(ras_handle_table *t, int id,
                               const char *addr, unsigned short port,
                               ras_handle **out);
void ras_handle_set_client(ras_handle_table *t, int id,
                           const char *addr, unsigned short port);
int ras_handles_remove(ras_handle_table *t, int id);
ras_handle *ras_handles_lookup(ras_handle_table *t, int id, int token);
void ras_handles_clear_dead(ras_handle_table *t);
const int *ras_handles_get_dead(ras_handle_table *t, size_t *out_count);

// Attach a pre-built, sorted directory listing to a handle.
// Takes ownership of entries[].name strings and the entries array itself.
// Any previously attached listing is freed first.
void ras_handle_set_dir_listing(ras_handle_table *t, int id,
                                ras_dir_entry *entries, size_t count);

#endif
