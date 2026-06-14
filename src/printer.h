// ShareFS Server - Printer Support
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef SFS_PRINTER_H
#define SFS_PRINTER_H

#include "config.h"

int sfs_printers_setup(const sfs_config *cfg);
void sfs_printers_poll(const sfs_config *cfg);
void sfs_printers_shutdown(void);

#endif
