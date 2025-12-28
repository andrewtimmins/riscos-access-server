// RISC OS Access/ShareFS Server - Freeway Broadcasts
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_BROADCAST_H
#define RAS_BROADCAST_H

#include "config.h"
#include "net.h"

int ras_broadcast_shares(const ras_config *cfg, ras_net *net);
int ras_broadcast_printers(const ras_config *cfg, ras_net *net);

#endif
