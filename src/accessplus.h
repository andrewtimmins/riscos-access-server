// RISC OS Access/ShareFS Server - Access+ Authentication
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_ACCESSPLUS_H
#define RAS_ACCESSPLUS_H

#include "config.h"
#include "net.h"
#include <time.h>

// Maximum number of authenticated clients to track
#define RAS_MAX_AUTH_CLIENTS 64

// Authentication entry - tracks a client authenticated to a share
typedef struct {
    char client_ip[64];
    char share_name[32];
    time_t expiry;        // When this auth expires (for cleanup)
} ras_auth_entry;

// Authentication state
typedef struct {
    ras_auth_entry entries[RAS_MAX_AUTH_CLIENTS];
    size_t count;
} ras_auth_state;

// Initialize auth state
void ras_auth_init(ras_auth_state *state);

// Record that a client is authenticated for a share
void ras_auth_add(ras_auth_state *state, const char *client_ip, const char *share_name);

// Check if a client is authenticated for a share
int ras_auth_check(ras_auth_state *state, const char *client_ip, const char *share_name);

// Password encoding: maps char to 0-36 (0=invalid, 1-10=digits, 11-36=letters)
int ras_password_to_pin(const char *password);

// Handle Access+ authentication packet on port 32771
int ras_accessplus_handle(const unsigned char *buf, size_t len,
                          const char *addr, unsigned short port,
                          const ras_config *cfg, ras_net *net,
                          ras_auth_state *auth);

#endif // RAS_ACCESSPLUS_H
