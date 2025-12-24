// RISC OS Access/ShareFS Server - File Operations (stub)
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_OPS_H
#define RAS_OPS_H

#include "handle.h"
#include "net.h"
#include "config.h"
#include "accessplus.h"

int ras_rpc_handle(const unsigned char *buf, size_t len, const char *addr, unsigned short port,
                   const ras_config *cfg, ras_net *net, ras_handle_table *handles, ras_auth_state *auth);

#endif
