/*
  ShareFS Server - Network Layer

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

#include "net.h"
#include "log.h"

#include <string.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

static sfs_socket open_udp(unsigned short port, const char *bind_addr) {
    sfs_socket s = (sfs_socket)socket(AF_INET, SOCK_DGRAM, 0);
    if (s == SFS_INVALID_SOCKET) {
        return SFS_INVALID_SOCKET;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = bind_addr ? inet_addr(bind_addr) : htonl(INADDR_ANY);

    int yes = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#if defined(SO_REUSEPORT) &&                                                   \
    (defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||      \
     defined(__OpenBSD__))
    // BSD-derived stacks require SO_REUSEPORT, not just SO_REUSEADDR, before
    // two UDP sockets may share a port. Without it the second bind on 32770
    // fails and the Freeway listener is lost. Linux does not need it here, and
    // setting it there would needlessly let other processes share the port.
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
#endif

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return SFS_INVALID_SOCKET;
    }

    return s;
}

int sfs_net_open(sfs_net *net, const char *bind_addr) {
    if (!net) return -1;
    memset(net, 0, sizeof(*net));

    net->broadcast = open_udp(SFS_PORT_BROADCAST, bind_addr);
    net->freeway   = open_udp(SFS_PORT_BROADCAST, bind_addr);  // Listen on same port
    net->auth      = open_udp(SFS_PORT_AUTH, bind_addr);
    net->rpc       = open_udp(SFS_PORT_RPC, bind_addr);

    if (net->broadcast == SFS_INVALID_SOCKET || net->auth == SFS_INVALID_SOCKET || net->rpc == SFS_INVALID_SOCKET) {
        sfs_net_close(net);
        return -1;
    }

    // Freeway socket failure is not fatal - it just shares broadcast socket
    if (net->freeway == SFS_INVALID_SOCKET) {
        net->freeway = net->broadcast;
    }

    int yes = 1;
#ifdef _WIN32
    setsockopt(net->broadcast, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes));
#else
    setsockopt(net->broadcast, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
#endif
    return 0;
}

void sfs_net_close(sfs_net *net) {
    if (!net) return;
#ifdef _WIN32
    if (net->broadcast != SFS_INVALID_SOCKET) closesocket(net->broadcast);
    if (net->freeway != SFS_INVALID_SOCKET && net->freeway != net->broadcast) closesocket(net->freeway);
    if (net->auth != SFS_INVALID_SOCKET) closesocket(net->auth);
    if (net->rpc != SFS_INVALID_SOCKET) closesocket(net->rpc);
#else
    if (net->broadcast != SFS_INVALID_SOCKET) close(net->broadcast);
    if (net->freeway != SFS_INVALID_SOCKET && net->freeway != net->broadcast) close(net->freeway);
    if (net->auth != SFS_INVALID_SOCKET) close(net->auth);
    if (net->rpc != SFS_INVALID_SOCKET) close(net->rpc);
#endif
    net->broadcast = net->freeway = net->auth = net->rpc = SFS_INVALID_SOCKET;
}

ssize_t sfs_net_sendto(sfs_socket s, const void *buf, size_t len, const char *addr, unsigned short port) {
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = addr ? inet_addr(addr) : htonl(INADDR_BROADCAST);
#ifdef _WIN32
    return sendto(s, (const char *)buf, (int)len, 0, (struct sockaddr *)&to, sizeof(to));
#else
    return sendto(s, buf, len, 0, (struct sockaddr *)&to, sizeof(to));
#endif
}

ssize_t sfs_net_recvfrom(sfs_socket s, void *buf, size_t len, char *addr, size_t addr_len, unsigned short *port) {
    struct sockaddr_in from;
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
#ifdef _WIN32
    ssize_t n = recvfrom(s, (char *)buf, (int)len, 0, (struct sockaddr *)&from, &from_len);
#else
    ssize_t n = recvfrom(s, buf, len, 0, (struct sockaddr *)&from, &from_len);
#endif
    if (n >= 0 && addr && addr_len > 0) {
        const char *p = inet_ntoa(from.sin_addr);
        if (p) {
            strncpy(addr, p, addr_len - 1);
            addr[addr_len - 1] = '\0';
        }
    }
    if (n >= 0 && port) {
        *port = ntohs(from.sin_port);
    }
    return n;
}
