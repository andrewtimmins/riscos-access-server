// RISC OS Access/ShareFS Server - Network Layer
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

static ras_socket open_udp(unsigned short port, const char *bind_addr) {
    ras_socket s = (ras_socket)socket(AF_INET, SOCK_DGRAM, 0);
    if (s == RAS_INVALID_SOCKET) {
        return RAS_INVALID_SOCKET;
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
        return RAS_INVALID_SOCKET;
    }

    return s;
}

int ras_net_open(ras_net *net, const char *bind_addr) {
    if (!net) return -1;
    memset(net, 0, sizeof(*net));

    net->broadcast = open_udp(RAS_PORT_BROADCAST, bind_addr);
    net->freeway   = open_udp(RAS_PORT_BROADCAST, bind_addr);  // Listen on same port
    net->auth      = open_udp(RAS_PORT_AUTH, bind_addr);
    net->rpc       = open_udp(RAS_PORT_RPC, bind_addr);

    if (net->broadcast == RAS_INVALID_SOCKET || net->auth == RAS_INVALID_SOCKET || net->rpc == RAS_INVALID_SOCKET) {
        ras_net_close(net);
        return -1;
    }

    // Freeway socket failure is not fatal - it just shares broadcast socket
    if (net->freeway == RAS_INVALID_SOCKET) {
        net->freeway = net->broadcast;
    }

    int yes = 1;
#ifdef _WIN32
    setsockopt(net->broadcast, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes));
    ras_log(RAS_LOG_INFO, "Sockets opened - broadcast:%llu auth:%llu rpc:%llu",
            (unsigned long long)net->broadcast, (unsigned long long)net->auth, (unsigned long long)net->rpc);
#else
    setsockopt(net->broadcast, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    ras_log(RAS_LOG_INFO, "Sockets opened - broadcast:%d auth:%d rpc:%d",
            net->broadcast, net->auth, net->rpc);
#endif
    return 0;
}

void ras_net_close(ras_net *net) {
    if (!net) return;
#ifdef _WIN32
    if (net->broadcast != RAS_INVALID_SOCKET) closesocket(net->broadcast);
    if (net->freeway != RAS_INVALID_SOCKET && net->freeway != net->broadcast) closesocket(net->freeway);
    if (net->auth != RAS_INVALID_SOCKET) closesocket(net->auth);
    if (net->rpc != RAS_INVALID_SOCKET) closesocket(net->rpc);
#else
    if (net->broadcast != RAS_INVALID_SOCKET) close(net->broadcast);
    if (net->freeway != RAS_INVALID_SOCKET && net->freeway != net->broadcast) close(net->freeway);
    if (net->auth != RAS_INVALID_SOCKET) close(net->auth);
    if (net->rpc != RAS_INVALID_SOCKET) close(net->rpc);
#endif
    net->broadcast = net->freeway = net->auth = net->rpc = RAS_INVALID_SOCKET;
}

ssize_t ras_net_sendto(ras_socket s, const void *buf, size_t len, const char *addr, unsigned short port) {
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = addr ? inet_addr(addr) : htonl(INADDR_BROADCAST);
    return sendto(s, (const char *)buf, (int)len, 0, (struct sockaddr *)&to, sizeof(to));
}

ssize_t ras_net_recvfrom(ras_socket s, void *buf, size_t len, char *addr, size_t addr_len, unsigned short *port) {
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
