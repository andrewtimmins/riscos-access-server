/*
  ShareFS Server - Freeway Broadcasts

  Copyright (C) 2025-2026 Andy Timmins

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "broadcast.h"
#include "log.h"
#include "riscos.h"

#include <string.h>

static int send_broadcast(sfs_net *net, unsigned int word0, const char *name, const char *desc) {
    if (!net || net->broadcast == SFS_INVALID_SOCKET || !name) return -1;
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

    sfs_log(SFS_LOG_PROTOCOL, "Broadcast: %s (%s) %zu bytes", name, desc, total);

    ssize_t sent = sfs_net_sendto(net->broadcast, packet, total, NULL, SFS_PORT_BROADCAST);
    if (sent < 0) {
        sfs_log(SFS_LOG_ERROR, "Broadcast sendto failed");
        return -1;
    }
    return 0;
}

int sfs_broadcast_shares(const sfs_config *cfg, sfs_net *net) {
    if (!cfg || !net) return -1;
    for (size_t i = 0; i < cfg->share_count; ++i) {
        // Skip protected shares - they're only announced via Access+ (port 32771)
        if (cfg->shares[i].attributes & SFS_ATTR_PROTECTED) continue;

        const char *name = cfg->shares[i].name ? cfg->shares[i].name : "";
        // Description shown to user - use share name or empty
        const char *desc = "";
        unsigned int word0 = 0x00010002; // discs add (type=1, minor=2)
        if (send_broadcast(net, word0, name, desc) != 0) {
            sfs_log(SFS_LOG_ERROR, "broadcast share failed: %s", name);
        }
    }
    return 0;
}

int sfs_broadcast_printers(const sfs_config *cfg, sfs_net *net) {
    if (!cfg || !net) return -1;
    for (size_t i = 0; i < cfg->printer_count; ++i) {
        const char *name = cfg->printers[i].name ? cfg->printers[i].name : "";
        const char *desc = cfg->printers[i].description ? cfg->printers[i].description : "";
        unsigned int word0 = 0x00020002; // printers add (type=2, minor=2)
        if (send_broadcast(net, word0, name, desc) != 0) {
            sfs_log(SFS_LOG_ERROR, "broadcast printer failed: %s", name);
        }
    }
    return 0;
}
