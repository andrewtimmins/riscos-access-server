// ShareFS Server - Handle Management
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_HANDLE_H
#define SFS_HANDLE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

typedef enum {
    SFS_HANDLE_NONE = 0,
    SFS_HANDLE_FILE = 1,
    SFS_HANDLE_DIR  = 2
} sfs_handle_type;

// One cached directory entry (populated once at ROPENDIR, sorted, reused
// across all RREADDIR pagination calls to eliminate the O(N^2) re-scan).
typedef struct {
    char        *name;      // RISC OS display name (decoded, no ,xxx suffix)
    struct stat  st;        // stat captured at ROPENDIR time
    int          filetype;  // resolved RISC OS filetype
} sfs_dir_entry;

typedef struct {
    int id;
    int token;
    sfs_handle_type type;
    int fd;
    uint32_t seq_ptr;           // Sequential pointer
    uint32_t load_addr;         // RISC OS load address
    uint32_t exec_addr;         // RISC OS exec address
    uint32_t length;            // File length at open time
    uint32_t attrs;             // RISC OS attributes
    int open_flags;             // Flags used to open the file (for reopening)
    char *path;                 // Host path for directory handles
    sfs_dir_entry *dir_entries; // Sorted cached listing (dir handles only)
    size_t dir_entry_count;
    char client_addr[64];       // IP address of the client that opened this handle
    unsigned short client_port; // Port of the client that opened this handle
} sfs_handle;

typedef struct {
    sfs_handle *items;
    size_t count;
    int next_id;
    int *dead_handles;     // Recently closed handle IDs for RDEADHANDLES
    size_t dead_count;
} sfs_handle_table;

int sfs_handles_init(sfs_handle_table *t);
void sfs_handles_free(sfs_handle_table *t);
int sfs_handles_add(sfs_handle_table *t, sfs_handle_type type, int fd, int *out_id, int *out_token);
int sfs_handles_add_ex(sfs_handle_table *t, sfs_handle_type type, int fd, const char *path,
                       uint32_t load, uint32_t exec, uint32_t len, uint32_t attrs,
                       int *out_id, int *out_token);
int sfs_handles_close(sfs_handle_table *t, int id, int token);
int sfs_handles_get(sfs_handle_table *t, int id, sfs_handle **out);
int sfs_handles_get_for_client(sfs_handle_table *t, int id,
                               const char *addr, unsigned short port,
                               sfs_handle **out);
void sfs_handle_set_client(sfs_handle_table *t, int id,
                           const char *addr, unsigned short port);
int sfs_handles_remove(sfs_handle_table *t, int id);
sfs_handle *sfs_handles_lookup(sfs_handle_table *t, int id, int token);
void sfs_handles_clear_dead(sfs_handle_table *t);
const int *sfs_handles_get_dead(sfs_handle_table *t, size_t *out_count);

// Attach a pre-built, sorted directory listing to a handle.
// Takes ownership of entries[].name strings and the entries array itself.
// Any previously attached listing is freed first.
void sfs_handle_set_dir_listing(sfs_handle_table *t, int id,
                                sfs_dir_entry *entries, size_t count);

#endif
