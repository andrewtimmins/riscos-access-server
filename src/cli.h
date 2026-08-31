/*
  ShareFS Server - Command Line

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

// One program with modes, rather than three programs.
//
// There used to be sharefs-server, sharefs-service and sharefs-admin, which on
// Windows meant a folder of executables with nothing to say which one to run.
// There is now one `sharefs`, and this file is its command line: it serves,
// manages the background service, reports where the configuration is, and
// hands over to the window when there is nothing else to do.

#ifndef SFS_CLI_H
#define SFS_CLI_H

// Runs the window and returns its exit code. The GUI build passes one of these
// in; a build without wxWidgets passes NULL and serving becomes the default.
typedef int (*sfs_cli_gui_fn)(int argc, char **argv);

// Parse the arguments and do as they say. `gui` may be NULL.
int sfs_cli_main(int argc, char **argv, sfs_cli_gui_fn gui);

// Load the configuration, bind the sockets and run the server loop until a
// signal or sfs_server_request_stop() arrives. `config_path` may be NULL, in
// which case the usual search runs and a starter configuration is written if
// there is nothing to find.
int sfs_cli_serve(const char *config_path);

#endif
