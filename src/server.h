/*
  ShareFS Server - Core Server Loop

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

#ifndef SFS_SERVER_H
#define SFS_SERVER_H

#include "config.h"
#include "handle.h"
#include "net.h"

// Run the server loop. Blocks until sfs_server_request_stop() is called, so a
// caller that needs to keep doing other work (the admin GUI hosting the server
// in-process) should run this on its own thread.
int sfs_server_run(sfs_config *cfg, sfs_net *net, sfs_handle_table *handles);

// Ask the loop to exit. Safe to call from another thread or a signal handler:
// it only sets a flag, which the loop notices within one select() timeout
// (currently one second). Call before joining the server thread.
void sfs_server_request_stop(void);

// Clear a previous stop request. Call before starting the loop again, or the
// next run exits immediately.
void sfs_server_clear_stop(void);

#endif
