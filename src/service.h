/*
  ShareFS Server - Windows Service Integration

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

// The Windows service, as a set of functions rather than its own executable.
//
// This used to be sharefs-service.exe, a third binary beside the server and
// the GUI, which left a Windows user looking at a folder of executables with
// nothing to say which one to run. The service now lives inside the single
// sharefs binary and is reached through `sharefs service ...`.

#ifndef SFS_SERVICE_H
#define SFS_SERVICE_H

#ifdef _WIN32

// Registered name, and the name the GUI queries. One definition, so the two
// cannot drift apart.
#define SFS_SERVICE_NAME "ShareFSServer"
#define SFS_SERVICE_DISPLAY_NAME "ShareFS"

// Hand this process to the service control manager. Returns 0 if we really
// were started by the SCM and have now finished, or -1 if we were not started
// by it, in which case the caller should carry on as an ordinary program.
int sfs_service_dispatch(void);

// Service management. Each prints its own diagnostics and returns 0 on
// success, non-zero on failure, so they can be returned straight from main.
int sfs_service_install(void);
int sfs_service_uninstall(void);
int sfs_service_start(void);
int sfs_service_stop(void);

// Whether the service exists, and whether it is currently running. Both answer
// 0 when the question cannot be asked at all.
int sfs_service_is_installed(void);
int sfs_service_is_running(void);

#endif // _WIN32

#endif
