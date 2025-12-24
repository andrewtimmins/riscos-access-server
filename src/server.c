// RISC OS Access/ShareFS Server - Core Server Loop
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "server.h"
#include "broadcast.h"
#include "log.h"
#include "printer.h"
#include "ops.h"
#include "accessplus.h"

#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/select.h>
#endif
#include <sys/stat.h>

// Send RDEADHANDLES broadcast to all clients
static void broadcast_dead_handles(ras_handle_table *handles, ras_net *net) {
    size_t count = 0;
    const int *dead = ras_handles_get_dead(handles, &count);
    if (!dead || count == 0) return;

    // Format: op(1) + padding(3) + count(4) + handle_ids...
    unsigned char pkt[512];
    pkt[0] = 19;  // RDEADHANDLES
    pkt[1] = pkt[2] = pkt[3] = 0;

    size_t max_ids = (sizeof(pkt) - 8) / 4;
    if (count > max_ids) count = max_ids;

    pkt[4] = (unsigned char)(count & 0xFF);
    pkt[5] = (unsigned char)((count >> 8) & 0xFF);
    pkt[6] = (unsigned char)((count >> 16) & 0xFF);
    pkt[7] = (unsigned char)((count >> 24) & 0xFF);

    for (size_t i = 0; i < count; ++i) {
        unsigned int id = (unsigned int)dead[i];
        pkt[8 + i * 4] = (unsigned char)(id & 0xFF);
        pkt[9 + i * 4] = (unsigned char)((id >> 8) & 0xFF);
        pkt[10 + i * 4] = (unsigned char)((id >> 16) & 0xFF);
        pkt[11 + i * 4] = (unsigned char)((id >> 24) & 0xFF);
    }

    // Broadcast on RPC port
    ras_net_sendto(net->rpc, pkt, 8 + count * 4, "255.255.255.255", 49171);
    ras_log(RAS_LOG_DEBUG, "Broadcast %zu dead handles", count);

    ras_handles_clear_dead(handles);
}

int ras_server_run(ras_config *cfg, ras_net *net, ras_handle_table *handles) {
    if (!cfg || !net || !handles) return -1;

    // Initialize auth state for tracking authenticated clients
    ras_auth_state auth;
    ras_auth_init(&auth);

    // Validate share/printer paths
    for (size_t i = 0; i < cfg->share_count; ++i) {
        struct stat st;
        const char *p = cfg->shares[i].path;
        if (!p || stat(p, &st) != 0) {
            ras_log(RAS_LOG_ERROR, "share %s path missing", cfg->shares[i].name ? cfg->shares[i].name : "?");
        }
    }
    for (size_t i = 0; i < cfg->printer_count; ++i) {
        struct stat st;
        const char *p = cfg->printers[i].path;
        if (!p || stat(p, &st) != 0) {
            ras_log(RAS_LOG_ERROR, "printer %s path missing", cfg->printers[i].name ? cfg->printers[i].name : "?");
        }
    }

    // Prepare printer spool dirs and definition files
    ras_printers_setup(cfg);

    // Initial broadcasts
    ras_broadcast_shares(cfg, net);
    ras_broadcast_printers(cfg, net);

    time_t last_bcast = time(NULL);

    ras_log(RAS_LOG_INFO, "Server running, %zu shares, %zu printers",
            cfg->share_count, cfg->printer_count);

    // Main loop
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(net->rpc, &fds);
        ras_socket maxfd = net->rpc;

        // Also listen on auth port for Access+ if enabled
        if (cfg->server.access_plus && net->auth != (ras_socket)-1) {
            FD_SET(net->auth, &fds);
            if (net->auth > maxfd) maxfd = net->auth;
        }

        // Also listen on freeway port for announcements
        if (net->freeway != (ras_socket)-1) {
            FD_SET(net->freeway, &fds);
            if (net->freeway > maxfd) maxfd = net->freeway;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ready = select((int)(maxfd + 1), &fds, NULL, NULL, &tv);

        if (ready > 0) {
            // Handle RPC packets
            if (FD_ISSET(net->rpc, &fds)) {
                unsigned char buf[4096];
                char addr[64];
                unsigned short port = 0;
                ssize_t n = ras_net_recvfrom(net->rpc, buf, sizeof(buf), addr, sizeof(addr), &port);
                if (n > 0) {
                    ras_log(RAS_LOG_PROTOCOL, "RPC %zd bytes from %s:%u", n, addr, port);
                    ras_rpc_handle(buf, (size_t)n, addr, port, cfg, net, handles, &auth);
                }
            }

            // Handle Access+ auth packets
            if (cfg->server.access_plus && net->auth != (ras_socket)-1 && FD_ISSET(net->auth, &fds)) {
                unsigned char buf[1024];
                char addr[64];
                unsigned short port = 0;
                ssize_t n = ras_net_recvfrom(net->auth, buf, sizeof(buf), addr, sizeof(addr), &port);
                if (n > 0) {
                    ras_log(RAS_LOG_PROTOCOL, "Auth %zd bytes from %s:%u", n, addr, port);
                    ras_accessplus_handle(buf, (size_t)n, addr, port, cfg, net, &auth);
                }
            }

            // Handle Freeway packets (client announcements)
            if (net->freeway != (ras_socket)-1 && FD_ISSET(net->freeway, &fds)) {
                unsigned char buf[1024];
                char addr[64];
                unsigned short port = 0;
                ssize_t n = ras_net_recvfrom(net->freeway, buf, sizeof(buf), addr, sizeof(addr), &port);
                if (n > 0) {
                    ras_log(RAS_LOG_PROTOCOL, "Freeway %zd bytes from %s:%u", n, addr, port);
                    // Could process client announcements here
                }
            }
        }

        time_t now = time(NULL);
        if (cfg->server.broadcast_interval > 0 && (now - last_bcast) >= cfg->server.broadcast_interval) {
            ras_broadcast_shares(cfg, net);
            ras_broadcast_printers(cfg, net);
            broadcast_dead_handles(handles, net);
            last_bcast = now;
        }

        ras_printers_poll(cfg);
    }

    return 0; // unreachable for now
}
