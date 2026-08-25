/*
  ShareFS Server - Printer Support

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

#ifndef SFS_PRINTER_H
#define SFS_PRINTER_H

#include "config.h"

int sfs_printers_setup(const sfs_config *cfg);
void sfs_printers_poll(const sfs_config *cfg);
void sfs_printers_shutdown(void);

#endif
