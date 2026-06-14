// ShareFS Server - Access+ Authentication
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_ACCESSPLUS_H
#define SFS_ACCESSPLUS_H

#include "config.h"
#include "net.h"
#include <time.h>

// Maximum number of authenticated clients to track
#define SFS_MAX_AUTH_CLIENTS 64

// Authentication entry - tracks a client authenticated to a share
typedef struct {
    char client_ip[64];
    char share_name[32];
    time_t expiry;        // When this auth expires (for cleanup)
} sfs_auth_entry;

// Authentication state
typedef struct {
    sfs_auth_entry entries[SFS_MAX_AUTH_CLIENTS];
    size_t count;
} sfs_auth_state;

// Initialize auth state
void sfs_auth_init(sfs_auth_state *state);

// Record that a client is authenticated for a share
void sfs_auth_add(sfs_auth_state *state, const char *client_ip, const char *share_name);

// Check if a client is authenticated for a share
int sfs_auth_check(sfs_auth_state *state, const char *client_ip, const char *share_name);

// Password encoding: maps char to 0-36 (0=invalid, 1-10=digits, 11-36=letters)
int sfs_password_to_pin(const char *password);

// Handle Access+ authentication packet on port 32771
int sfs_accessplus_handle(const unsigned char *buf, size_t len,
                          const char *addr, unsigned short port,
                          const sfs_config *cfg, sfs_net *net,
                          sfs_auth_state *auth);

#endif // SFS_ACCESSPLUS_H
