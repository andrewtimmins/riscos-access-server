// ShareFS Server - Core Server Loop
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_SERVER_H
#define SFS_SERVER_H

#include "config.h"
#include "handle.h"
#include "net.h"

int sfs_server_run(sfs_config *cfg, sfs_net *net, sfs_handle_table *handles);

#endif
