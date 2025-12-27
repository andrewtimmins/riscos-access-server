// RISC OS Access/ShareFS Server - Printer Support
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_PRINTER_H
#define RAS_PRINTER_H

#include "config.h"

int ras_printers_setup(const ras_config *cfg);
void ras_printers_poll(const ras_config *cfg);
void ras_printers_shutdown(void);

#endif
