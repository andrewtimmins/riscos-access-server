/*
  ShareFS Server - Start In The Background

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

// "Keep sharing when the window is closed", expressed once for all three
// platforms.
//
// The GUI used to show the user which mechanism was in play: a systemd unit, a
// Windows service, or the server thread inside the app. That is an
// implementation detail of the operating system, and nobody installing a file
// server has an opinion about it. What they have an opinion about is whether
// sharing keeps working after they close the window and reboot. So the
// question asked here is that one, and each platform answers it with whatever
// it happens to use:
//
//   Windows  the ShareFS service, through the service control manager
//   macOS    a launchd agent in ~/Library/LaunchAgents
//   Linux    the sharefs systemd unit
//
// Nothing here needs administrator rights on macOS. On Linux the Debian
// package installs a polkit rule so members of the sharefs-admin group can
// enable and disable the unit; on Windows the service calls need elevation,
// which is why enabling it reports a readable failure rather than asserting.

#ifndef SFS_AUTOSTART_H
#define SFS_AUTOSTART_H

#include <stddef.h>

typedef enum {
  // This platform has no mechanism we can drive, or the tool that drives it
  // is not installed (a Linux system without systemd, say).
  SFS_AUTOSTART_UNSUPPORTED = 0,
  SFS_AUTOSTART_DISABLED,
  SFS_AUTOSTART_ENABLED
} sfs_autostart_state;

// Human-readable name of the mechanism, for a status line or a tooltip.
// Never NULL.
const char *sfs_autostart_mechanism(void);

// Whether background sharing is set up to happen on its own.
sfs_autostart_state sfs_autostart_query(void);

// Whether a background copy is running right now. This is a different question
// from sfs_autostart_query: a service can be installed but stopped, and it can
// be running after being started by hand without being enabled at boot.
int sfs_autostart_is_running(void);

// Turn background sharing on or off. Returns 0 on success; on failure returns
// -1 and fills `err`, when given, with something worth showing the user.
int sfs_autostart_set(int enabled, char *err, size_t err_sz);

// Start or stop the background copy without changing whether it is enabled at
// boot. Used by the window's start and stop controls when the server is being
// hosted in the background rather than in the app.
int sfs_autostart_start_now(char *err, size_t err_sz);
int sfs_autostart_stop_now(char *err, size_t err_sz);

// Make sure an existing arrangement still points at this copy of ShareFS, and
// repair it if not.
//
// This is for macOS, where the app can live anywhere: the launchd agent
// records the path of whichever copy set it up, so running it from Downloads
// and then dragging it to Applications leaves a login item pointing at a file
// that is no longer there, and background sharing stops without saying so.
// Called at startup. Does nothing where the path is fixed by the packaging, as
// it is for a systemd unit or a Windows service, and nothing when background
// sharing is off.
//
// Returns 1 if it repaired something, 0 if there was nothing to do, -1 on
// failure with `err` filled in.
int sfs_autostart_repair(char *err, size_t err_sz);

#endif
