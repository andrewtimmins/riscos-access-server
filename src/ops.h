// ShareFS Server - File Operations
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_OPS_H
#define SFS_OPS_H

#include "handle.h"
#include "net.h"
#include "config.h"
#include "accessplus.h"

int sfs_rpc_handle(const unsigned char *buf, size_t len, const char *addr, unsigned short port,
                   const sfs_config *cfg, sfs_net *net, sfs_handle_table *handles, sfs_auth_state *auth);

int sfs_rpc_handle_r(const unsigned char *buf, size_t len, const char *addr, unsigned short port,
                     sfs_net *net, sfs_handle_table *handles);

#endif
