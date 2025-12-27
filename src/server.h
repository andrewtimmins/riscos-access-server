// RISC OS Access/ShareFS Server - Core Server Loop
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_SERVER_H
#define RAS_SERVER_H

#include "config.h"
#include "handle.h"
#include "net.h"

int ras_server_run(ras_config *cfg, ras_net *net, ras_handle_table *handles);

#endif
