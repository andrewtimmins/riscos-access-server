// ShareFS Server - Network Layer
// Author: Andrew Timmins
// License: GPL-3.0-only

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
    return sendto(s, (const char *)buf, (int)len, 0, (struct sockaddr *)&to, sizeof(to));
}

ssize_t sfs_net_recvfrom(sfs_socket s, void *buf, size_t len, char *addr, size_t addr_len, unsigned short *port) {
    struct sockaddr_in from;
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
    ssize_t n = recvfrom(s, (char *)buf, (int)len, 0, (struct sockaddr *)&from, &from_len);
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
