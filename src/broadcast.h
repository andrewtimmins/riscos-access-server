/*
  ShareFS Server - Freeway Broadcasts

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

#ifndef SFS_BROADCAST_H
#define SFS_BROADCAST_H

#include "config.h"
#include "net.h"

int sfs_broadcast_shares(const sfs_config *cfg, sfs_net *net);
int sfs_broadcast_printers(const sfs_config *cfg, sfs_net *net);

#endif
