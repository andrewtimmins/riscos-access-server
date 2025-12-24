// RISC OS Access/ShareFS Server - Network Layer
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_NET_H
#define RAS_NET_H

#include "platform.h"

#include <stddef.h>

#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#define RAS_PORT_BROADCAST 32770
#define RAS_PORT_AUTH      32771
#define RAS_PORT_RPC       49171

typedef struct {
    ras_socket broadcast;
    ras_socket freeway;
    ras_socket auth;
    ras_socket rpc;
} ras_net;

int ras_net_open(ras_net *net, const char *bind_addr);
void ras_net_close(ras_net *net);
ssize_t ras_net_sendto(ras_socket s, const void *buf, size_t len, const char *addr, unsigned short port);
ssize_t ras_net_recvfrom(ras_socket s, void *buf, size_t len, char *addr, size_t addr_len, unsigned short *port);

#endif
