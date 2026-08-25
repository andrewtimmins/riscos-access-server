/*
  ShareFS Server - File Operations

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
