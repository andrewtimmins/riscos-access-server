// ShareFS Server - Freeway Broadcasts
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_BROADCAST_H
#define SFS_BROADCAST_H

#include "config.h"
#include "net.h"

int sfs_broadcast_shares(const sfs_config *cfg, sfs_net *net);
int sfs_broadcast_printers(const sfs_config *cfg, sfs_net *net);

#endif
