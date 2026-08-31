/*
  ShareFS Server - Entry Point

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

// The entry point for a build without the window. Everything it does lives in
// src/cli.c, which the build with the window shares, so the two cannot answer
// `--help` or find the configuration differently. See src/cli.h.

#include "cli.h"

#include <stddef.h>

int main(int argc, char **argv) { return sfs_cli_main(argc, argv, NULL); }
