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

/*
 * ★ The field after the name is an ATTRIBUTE DESCRIPTOR, not a description.
 *
 * A RISC OS server registers a share with one byte of flags after the name -
 * protected 0x01, read only 0x02, hidden 0x04, subdirectory 0x08, CD-ROM 0x10 -
 * and that byte is how a client learns a shared disc is read only before
 * anybody tries to write to it. This used to send an empty string there, so
 * every share announced itself as ordinary and writable whatever its
 * configuration said.
 *
 * The values are the same as the SFS_ATTR_* bits, so the configured attributes
 * go out as they are.
 */
static int send_broadcast(sfs_net *net, unsigned int word0, const char *name,
                          const void *data, size_t data_len) {
    if (!net || net->broadcast == SFS_INVALID_SOCKET || !name) return -1;
    if (!data) data_len = 0;

    // The name includes its null terminator; the descriptor is raw bytes.
    size_t name_len = strlen(name) + 1;
    size_t desc_len = data_len;

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
    if (desc_len)
        memcpy(packet + 12 + name_len, data, desc_len);

    size_t total = 12 + name_len + desc_len;

    sfs_log(SFS_LOG_PROTOCOL, "Broadcast: %s attrs=%02x %zu bytes", name,
            desc_len ? ((const unsigned char *)data)[0] : 0, total);

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
        /* The attribute descriptor: read only, hidden and the rest, as the
           client expects to be told rather than to discover by being refused. */
        unsigned char descriptor =
            (unsigned char)(cfg->shares[i].attributes & 0xFF);
        unsigned int word0 = 0x00010002; // discs add (type=1, minor=2)
        if (send_broadcast(net, word0, name, &descriptor, sizeof(descriptor)) != 0) {
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
        /* Printers keep their description string, terminator included. Whether a
           printer announcement should carry a descriptor byte like a share does
           is not something this change had evidence for, so it is left alone. */
        unsigned int word0 = 0x00020002; // printers add (type=2, minor=2)
        if (send_broadcast(net, word0, name, desc, strlen(desc) + 1) != 0) {
            sfs_log(SFS_LOG_ERROR, "broadcast printer failed: %s", name);
        }
    }
    return 0;
}
