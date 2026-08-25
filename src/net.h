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

#ifndef SFS_NET_H
#define SFS_NET_H

#include "platform.h"

#include <stddef.h>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

#define SFS_PORT_BROADCAST 32770
#define SFS_PORT_AUTH      32771
#define SFS_PORT_RPC       49171

typedef struct {
    sfs_socket broadcast;
    sfs_socket freeway;
    sfs_socket auth;
    sfs_socket rpc;
} sfs_net;

int sfs_net_open(sfs_net *net, const char *bind_addr);
void sfs_net_close(sfs_net *net);
ssize_t sfs_net_sendto(sfs_socket s, const void *buf, size_t len, const char *addr, unsigned short port);
ssize_t sfs_net_recvfrom(sfs_socket s, void *buf, size_t len, char *addr, size_t addr_len, unsigned short *port);

#endif
