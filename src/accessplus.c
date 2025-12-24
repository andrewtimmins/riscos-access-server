// RISC OS Access/ShareFS Server - Access+ Authentication
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "accessplus.h"
#include "log.h"

#include <ctype.h>
#include <string.h>

// Encode a single character: digits 0-9 -> 1-10, letters A-Z -> 11-36
static int encode_char(char c) {
    c = (char)toupper((unsigned char)c);
    if (c >= '0' && c <= '9') return (c - '0') + 1;
    if (c >= 'A' && c <= 'Z') return (c - 'A') + 11;
    return 0;
}

int ras_password_to_pin(const char *password) {
    if (!password) return 0;
    // Password max 6 chars
    unsigned int pin = 0;
    int len = 0;
    for (const char *p = password; *p && len < 6; ++p, ++len) {
        pin = (pin * 0x25) + (unsigned int)encode_char(*p);
    }
    return (int)pin;
}

void ras_auth_init(ras_auth_state *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

void ras_auth_add(ras_auth_state *state, const char *client_ip, const char *share_name) {
    if (!state || !client_ip || !share_name) return;
    
    // Check if already exists and update expiry
    for (size_t i = 0; i < state->count; ++i) {
        if (strcmp(state->entries[i].client_ip, client_ip) == 0 &&
            strcmp(state->entries[i].share_name, share_name) == 0) {
            state->entries[i].expiry = time(NULL) + 600;  // 10 min expiry
            return;
        }
    }
    
    // Add new entry
    if (state->count < RAS_MAX_AUTH_CLIENTS) {
        ras_auth_entry *e = &state->entries[state->count++];
        strncpy(e->client_ip, client_ip, sizeof(e->client_ip) - 1);
        strncpy(e->share_name, share_name, sizeof(e->share_name) - 1);
        e->expiry = time(NULL) + 600;
        ras_log(RAS_LOG_INFO, "Auth: client %s authenticated for share '%s'", client_ip, share_name);
    }
}

int ras_auth_check(ras_auth_state *state, const char *client_ip, const char *share_name) {
    if (!state || !client_ip || !share_name) return 0;
    
    time_t now = time(NULL);
    for (size_t i = 0; i < state->count; ++i) {
        if (strcmp(state->entries[i].client_ip, client_ip) == 0 &&
            strcmp(state->entries[i].share_name, share_name) == 0) {
            if (state->entries[i].expiry > now) {
                // Refresh expiry on access
                state->entries[i].expiry = now + 600;
                return 1;  // Authenticated
            }
            // Expired - could clean up but leave for now
            return 0;
        }
    }
    return 0;  // Not authenticated
}

static unsigned int read_u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void write_u32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

// Freeway protocol message types (major << 16 | minor)
#define FW_DISCS_STARTUP    0x00010001  // Client asking for shares
#define FW_DISCS_AVAILABLE  0x00010002  // Share available broadcast
#define FW_DISCS_REMOVED    0x00010003  // Share removed
#define FW_DISCS_PERIODIC   0x00010004  // Periodic share broadcast (protected)

// Share attributes for response
#define ATTR_PROTECTED  0x01
#define ATTR_READONLY   0x02
#define ATTR_HIDDEN     0x04
#define ATTR_SUBDIR     0x08
#define ATTR_CDROM      0x10

int ras_accessplus_handle(const unsigned char *buf, size_t len,
                          const char *addr, unsigned short port,
                          const ras_config *cfg, ras_net *net,
                          ras_auth_state *auth) {
    if (!buf || len < 8 || !net || !cfg) return -1;

    unsigned int msg_type = read_u32(buf);
    unsigned int share_type = read_u32(buf + 4);

    ras_log(RAS_LOG_PROTOCOL, "Access+ type=%08x share_type=%08x from %s:%u",
            msg_type, share_type, addr ? addr : "?", port);

    // Handle Freeway-style authentication request
    // Client sends: 0x00010001, 0x00010001, key
    if (msg_type == FW_DISCS_STARTUP && share_type == 0x00010001 && len >= 12) {
        unsigned int client_key = read_u32(buf + 8);
        ras_log(RAS_LOG_DEBUG, "Access+ share request with key=%08x", client_key);

        // Find a protected share matching this key
        for (size_t i = 0; i < cfg->share_count; ++i) {
            const ras_share_config *s = &cfg->shares[i];
            if (!s->name || !s->password) continue;
            if (!(s->attributes & RAS_ATTR_PROTECTED)) continue;

            int share_key = ras_password_to_pin(s->password);
            if ((unsigned int)share_key == client_key) {
                // Record this client as authenticated for this share
                if (auth) {
                    ras_auth_add(auth, addr, s->name);
                }
                
                // Send the protected share info
                // Format: 0x00010004, 0x00010001, len | 0x00010000, key, name + attr
                size_t name_len = strlen(s->name);
                size_t pkt_len = 16 + name_len + 2;  // +1 for attr, +1 for null
                unsigned char reply[256];
                if (pkt_len > sizeof(reply)) continue;

                write_u32(reply, FW_DISCS_PERIODIC);
                write_u32(reply + 4, 0x00010001);
                write_u32(reply + 8, (unsigned int)(0x00010000 | name_len));
                write_u32(reply + 12, (unsigned int)share_key);
                memcpy(reply + 16, s->name, name_len);
                reply[16 + name_len] = (unsigned char)s->attributes;
                reply[16 + name_len + 1] = '\0';

                ras_log(RAS_LOG_DEBUG, "Access+ sending protected share '%s'", s->name);
                ras_net_sendto(net->auth, reply, pkt_len, addr, port);
            }
        }
        return 0;
    }

    // Handle general Freeway messages - just log and ignore for now
    if ((msg_type >> 16) == 0x0001) {
        unsigned int minor = msg_type & 0xFFFF;
        ras_log(RAS_LOG_DEBUG, "Access+ Freeway disc message minor=%u", minor);
        return 0;
    }

    ras_log(RAS_LOG_DEBUG, "Unknown Access+ message type %08x", msg_type);
    return 0;
}
