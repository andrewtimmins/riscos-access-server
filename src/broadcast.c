// RISC OS Access/ShareFS Server - Freeway Broadcasts
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "broadcast.h"
#include "log.h"
#include "riscos.h"

#include <string.h>

static int send_broadcast(ras_net *net, unsigned int word0, const char *name, const char *desc) {
    if (!net || net->broadcast == RAS_INVALID_SOCKET || !name) return -1;
    if (!desc) desc = "";

    // Lengths include null terminators
    size_t name_len = strlen(name) + 1;
    size_t desc_len = strlen(desc) + 1;

    // Build header
    unsigned int header[3];
    header[0] = word0;
    header[1] = 0x00010000;  // Version/flags
    header[2] = ((unsigned int)desc_len << 16) | (unsigned int)name_len;

    // Build packet: header + name (null-term) + desc (null-term)
    unsigned char packet[512];
    if (12 + name_len + desc_len > sizeof(packet)) return -1;

    memcpy(packet, header, 12);
    memcpy(packet + 12, name, name_len);
    memcpy(packet + 12 + name_len, desc, desc_len);

    size_t total = 12 + name_len + desc_len;

    ras_log(RAS_LOG_PROTOCOL, "Broadcast: %s (%s) %zu bytes", name, desc, total);

    ssize_t sent = ras_net_sendto(net->broadcast, packet, total, NULL, RAS_PORT_BROADCAST);
    if (sent < 0) {
        ras_log(RAS_LOG_ERROR, "Broadcast sendto failed");
        return -1;
    }
    return 0;
}

int ras_broadcast_shares(const ras_config *cfg, ras_net *net) {
    if (!cfg || !net) return -1;
    for (size_t i = 0; i < cfg->share_count; ++i) {
        // Skip protected shares - they're only announced via Access+ (port 32771)
        if (cfg->shares[i].attributes & RAS_ATTR_PROTECTED) continue;

        const char *name = cfg->shares[i].name ? cfg->shares[i].name : "";
        // Description shown to user - use share name or empty
        const char *desc = "";
        unsigned int word0 = 0x00010002; // discs add (type=1, minor=2)
        if (send_broadcast(net, word0, name, desc) != 0) {
            ras_log(RAS_LOG_ERROR, "broadcast share failed: %s", name);
        }
    }
    return 0;
}

int ras_broadcast_printers(const ras_config *cfg, ras_net *net) {
    if (!cfg || !net) return -1;
    for (size_t i = 0; i < cfg->printer_count; ++i) {
        const char *name = cfg->printers[i].name ? cfg->printers[i].name : "";
        const char *desc = cfg->printers[i].description ? cfg->printers[i].description : "";
        unsigned int word0 = 0x00020002; // printers add (type=2, minor=2)
        if (send_broadcast(net, word0, name, desc) != 0) {
            ras_log(RAS_LOG_ERROR, "broadcast printer failed: %s", name);
        }
    }
    return 0;
}
