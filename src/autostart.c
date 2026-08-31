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

#include "autostart.h"

#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "service.h"
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#define SFS_AGENT_LABEL "uk.co.andytimmins.sharefs"
#endif

// ---------------------------------------------------------------------------
// Windows: the service control manager
// ---------------------------------------------------------------------------
#ifdef _WIN32

const char *sfs_autostart_mechanism(void) { return "the ShareFS service"; }

sfs_autostart_state sfs_autostart_query(void) {
  return sfs_service_is_installed() ? SFS_AUTOSTART_ENABLED
                                    : SFS_AUTOSTART_DISABLED;
}

int sfs_autostart_is_running(void) { return sfs_service_is_running(); }

int sfs_autostart_set(int enabled, char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';

  if (enabled) {
    if (!sfs_service_is_installed() && sfs_service_install() != 0) {
      if (err && err_sz)
        snprintf(err, err_sz,
                 "Could not install the ShareFS service. Installing a service "
                 "needs administrator rights, so run ShareFS as an "
                 "administrator and try again.");
      return -1;
    }
    if (!sfs_service_is_running() && sfs_service_start() != 0) {
      if (err && err_sz)
        snprintf(err, err_sz, "The service is installed but would not start.");
      return -1;
    }
    return 0;
  }

  if (sfs_service_is_running() && sfs_service_stop() != 0) {
    if (err && err_sz)
      snprintf(err, err_sz,
               "Could not stop the ShareFS service. Stopping a service needs "
               "administrator rights.");
    return -1;
  }
  if (sfs_service_is_installed() && sfs_service_uninstall() != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not remove the ShareFS service.");
    return -1;
  }
  return 0;
}

int sfs_autostart_start_now(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  if (!sfs_service_is_installed()) {
    if (err && err_sz)
      snprintf(err, err_sz, "The ShareFS service is not installed.");
    return -1;
  }
  if (sfs_service_start() != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not start the ShareFS service.");
    return -1;
  }
  return 0;
}

int sfs_autostart_stop_now(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  if (sfs_service_stop() != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not stop the ShareFS service.");
    return -1;
  }
  return 0;
}

// The service's command line is written by whoever installed it, and the
// installer puts the binary in one place, so there is nothing to drift.
int sfs_autostart_repair(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  return 0;
}

// ---------------------------------------------------------------------------
// macOS: a launchd agent under the user's own home directory
// ---------------------------------------------------------------------------
#elif defined(__APPLE__)

const char *sfs_autostart_mechanism(void) { return "a login item"; }

// Where the agent definition lives. Per-user, so none of this needs sudo.
static int agent_plist_path(char *out, size_t out_sz) {
  const char *home = getenv("HOME");
  if (!home || !home[0])
    return -1;
  int n = snprintf(out, out_sz, "%s/Library/LaunchAgents/%s.plist", home,
                   SFS_AGENT_LABEL);
  return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
}

// The binary to run, which is this one: inside an app bundle that is
// Contents/MacOS/sharefs, and it serves headlessly when given "serve".
static int executable_path(char *out, size_t out_sz) {
  uint32_t size = (uint32_t)out_sz;
  if (_NSGetExecutablePath(out, &size) != 0)
    return -1;
  return 0;
}

static int run_launchctl(const char *verb, const char *argument) {
  char cmd[SFS_PATH_MAX * 2];
  // gui/<uid> is the per-user domain; the agent belongs to the logged-in user
  // and must not be loaded into the system domain.
  snprintf(cmd, sizeof(cmd), "/bin/launchctl %s gui/%u %s >/dev/null 2>&1",
           verb, (unsigned)getuid(), argument);
  return system(cmd);
}

sfs_autostart_state sfs_autostart_query(void) {
  char plist[SFS_PATH_MAX];
  if (agent_plist_path(plist, sizeof(plist)) != 0)
    return SFS_AUTOSTART_UNSUPPORTED;
  struct stat st;
  return (stat(plist, &st) == 0) ? SFS_AUTOSTART_ENABLED
                                 : SFS_AUTOSTART_DISABLED;
}

int sfs_autostart_is_running(void) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "/bin/launchctl print gui/%u/%s >/dev/null 2>&1", (unsigned)getuid(),
           SFS_AGENT_LABEL);
  return system(cmd) == 0;
}

int sfs_autostart_set(int enabled, char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';

  char plist[SFS_PATH_MAX];
  if (agent_plist_path(plist, sizeof(plist)) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not find your home directory.");
    return -1;
  }

  if (!enabled) {
    char quoted[SFS_PATH_MAX + 8];
    snprintf(quoted, sizeof(quoted), "'%s'", plist);
    run_launchctl("bootout", quoted);
    if (remove(plist) != 0) {
      struct stat st;
      if (stat(plist, &st) == 0) {
        if (err && err_sz)
          snprintf(err, err_sz, "Could not remove %s.", plist);
        return -1;
      }
    }
    return 0;
  }

  char exe[SFS_PATH_MAX];
  if (executable_path(exe, sizeof(exe)) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not work out where ShareFS is installed.");
    return -1;
  }

  char dir[SFS_PATH_MAX];
  const char *home = getenv("HOME");
  snprintf(dir, sizeof(dir), "%s/Library/LaunchAgents", home ? home : "");
  if (sfs_paths_mkdir_p(dir) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not create %s.", dir);
    return -1;
  }

  FILE *f = fopen(plist, "w");
  if (!f) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not write %s.", plist);
    return -1;
  }

  // KeepAlive restarts it if it exits; RunAtLoad starts it now and at login.
  // No StandardOut/ErrorPath: the server writes its own log, and pointing
  // launchd at the same file would interleave two writers.
  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
             "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
  fprintf(f, "<plist version=\"1.0\">\n");
  fprintf(f, "<dict>\n");
  fprintf(f, "\t<key>Label</key>\n\t<string>%s</string>\n", SFS_AGENT_LABEL);
  fprintf(f, "\t<key>ProgramArguments</key>\n\t<array>\n");
  fprintf(f, "\t\t<string>%s</string>\n", exe);
  fprintf(f, "\t\t<string>serve</string>\n");
  fprintf(f, "\t</array>\n");
  fprintf(f, "\t<key>RunAtLoad</key>\n\t<true/>\n");
  fprintf(f, "\t<key>KeepAlive</key>\n\t<true/>\n");
  fprintf(f, "\t<key>ProcessType</key>\n\t<string>Background</string>\n");
  fprintf(f, "</dict>\n</plist>\n");

  int ok = (fflush(f) == 0);
  if (fclose(f) != 0)
    ok = 0;
  if (!ok) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not finish writing %s.", plist);
    return -1;
  }

  char quoted[SFS_PATH_MAX + 8];
  snprintf(quoted, sizeof(quoted), "'%s'", plist);
  // Replace any previous registration, so pointing at a new install of the app
  // does not leave the old path loaded.
  run_launchctl("bootout", quoted);
  if (run_launchctl("bootstrap", quoted) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz,
               "Wrote %s, but launchd would not load it. It will start at your "
               "next login.",
               plist);
    return -1;
  }
  return 0;
}

int sfs_autostart_start_now(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "/bin/launchctl kickstart gui/%u/%s >/dev/null 2>&1",
           (unsigned)getuid(), SFS_AGENT_LABEL);
  if (system(cmd) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not start the background copy.");
    return -1;
  }
  return 0;
}

int sfs_autostart_stop_now(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "/bin/launchctl kill SIGTERM gui/%u/%s >/dev/null 2>&1",
           (unsigned)getuid(), SFS_AGENT_LABEL);
  if (system(cmd) != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not stop the background copy.");
    return -1;
  }
  return 0;
}

// Whether the agent already names this executable. Read as text rather than
// parsed as a property list: the file is one this program wrote, and the only
// question is whether the path in it is still the right one.
static int agent_names_this_executable(const char *plist, const char *exe) {
  FILE *f = fopen(plist, "r");
  if (!f)
    return 0;

  char line[SFS_PATH_MAX + 64];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    if (strstr(line, exe)) {
      found = 1;
      break;
    }
  }
  fclose(f);
  return found;
}

int sfs_autostart_repair(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';

  char plist[SFS_PATH_MAX];
  if (agent_plist_path(plist, sizeof(plist)) != 0)
    return 0;

  struct stat st;
  if (stat(plist, &st) != 0)
    return 0; // Background sharing is off; nothing to keep in step.

  char exe[SFS_PATH_MAX];
  if (executable_path(exe, sizeof(exe)) != 0)
    return 0;

  if (agent_names_this_executable(plist, exe))
    return 0;

  // The login item points at a copy of ShareFS that is somewhere else, and
  // possibly nowhere at all. Rewriting it costs a moment and is what the user
  // would otherwise have to work out to do by hand, having first noticed that
  // sharing had quietly stopped.
  if (sfs_autostart_set(1, err, err_sz) != 0)
    return -1;
  return 1;
}

// ---------------------------------------------------------------------------
// Linux and the rest: the systemd unit the package installs
// ---------------------------------------------------------------------------
#else

const char *sfs_autostart_mechanism(void) { return "the sharefs system service"; }

// Whether systemctl is there to be driven at all. A container or a system
// running another init has no unit to enable, and saying so is better than
// failing with a shell error.
static int have_systemctl(void) {
  return system("command -v systemctl >/dev/null 2>&1") == 0;
}

// Run a systemctl subcommand, asking for authentication if the plain call is
// refused.
//
// The Debian package's polkit rule covers org.freedesktop.systemd1.manage-units,
// which is start, stop and restart. Enabling and disabling a unit is a
// different action, manage-unit-files, and systemd does not tell polkit which
// unit that call is about, so a rule cannot be written for this unit alone and
// granting it for every unit is not something a file server should ask for. So
// the plain call is tried first, and pkexec is used only if it is refused and
// there is a desktop session to show the prompt in.
static int run_systemctl(const char *args) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "systemctl %s >/dev/null 2>&1", args);
  if (system(cmd) == 0)
    return 0;

  const char *display = getenv("DISPLAY");
  const char *wayland = getenv("WAYLAND_DISPLAY");
  if ((!display || !display[0]) && (!wayland || !wayland[0]))
    return -1;

  snprintf(cmd, sizeof(cmd), "pkexec systemctl %s >/dev/null 2>&1", args);
  return system(cmd) == 0 ? 0 : -1;
}

sfs_autostart_state sfs_autostart_query(void) {
  if (!have_systemctl())
    return SFS_AUTOSTART_UNSUPPORTED;
  // is-enabled answers "static", "masked" and others besides enabled and
  // disabled, but only a zero exit means it starts on its own. Asked directly
  // rather than through run_systemctl: reading the state needs no privilege,
  // and a status check must never raise a password prompt.
  return system("systemctl is-enabled --quiet sharefs >/dev/null 2>&1") == 0
             ? SFS_AUTOSTART_ENABLED
             : SFS_AUTOSTART_DISABLED;
}

int sfs_autostart_is_running(void) {
  if (!have_systemctl())
    return 0;
  return system("systemctl is-active --quiet sharefs >/dev/null 2>&1") == 0;
}

int sfs_autostart_set(int enabled, char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  if (!have_systemctl()) {
    if (err && err_sz)
      snprintf(err, err_sz,
               "This system does not use systemd, so ShareFS cannot manage "
               "background sharing for you. Start 'sharefs serve' from "
               "whatever your system uses instead.");
    return -1;
  }

  // Two calls rather than "enable --now", so that being allowed to start the
  // service but not to enable it at boot is a partial success reported as
  // such, instead of nothing happening at all.
  const int changed_boot =
      run_systemctl(enabled ? "enable sharefs" : "disable sharefs");
  const int changed_now =
      run_systemctl(enabled ? "start sharefs" : "stop sharefs");

  if (changed_boot != 0 && changed_now != 0) {
    if (err && err_sz)
      snprintf(err, err_sz,
               "Could not %s the sharefs service. The Debian package allows "
               "this for members of the sharefs-admin group; if you have just "
               "been added to it, log out and back in.",
               enabled ? "enable" : "disable");
    return -1;
  }

  if (changed_boot != 0) {
    if (err && err_sz)
      snprintf(err, err_sz,
               "Sharing is %s now, but the sharefs service could not be %s at "
               "boot. Run 'sudo systemctl %s sharefs' to make it stick.",
               enabled ? "on" : "off", enabled ? "enabled" : "disabled",
               enabled ? "enable" : "disable");
    return -1;
  }
  return 0;
}

int sfs_autostart_start_now(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  if (!have_systemctl() || run_systemctl("start sharefs") != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not start the sharefs service.");
    return -1;
  }
  return 0;
}

int sfs_autostart_stop_now(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  if (!have_systemctl() || run_systemctl("stop sharefs") != 0) {
    if (err && err_sz)
      snprintf(err, err_sz, "Could not stop the sharefs service.");
    return -1;
  }
  return 0;
}

// The unit file belongs to the package and names /usr/bin/sharefs, which is
// where the package puts it. Nothing drifts.
int sfs_autostart_repair(char *err, size_t err_sz) {
  if (err && err_sz)
    err[0] = '\0';
  return 0;
}

#endif
