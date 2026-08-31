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

#include "cli.h"

#include "autostart.h"
#include "config.h"
#include "handle.h"
#include "log.h"
#include "net.h"
#include "paths.h"
#include "platform.h"
#include "printer.h"
#include "server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "service.h"
#define sfs_strcasecmp _stricmp
#else
#include <strings.h>
#define sfs_strcasecmp strcasecmp
#endif

static void print_usage(void) {
  printf("ShareFS %s - file sharing for RISC OS machines\n\n", SHAREFS_VERSION);
  printf("Usage: sharefs [command] [options]\n\n");
  printf("With no command, ShareFS opens its window. Without a window in this\n");
  printf("build, it shares straight away, as `sharefs serve` does.\n\n");
  printf("Commands:\n");
  printf("  serve                 Share now, in the foreground, until stopped.\n");
  printf("  status                Say what is configured and what is running.\n");
  printf("  config path           Print the configuration file in use.\n");
  printf("  config create         Write a starter configuration.\n");
  printf("  autostart on|off      Keep sharing when nothing is open.\n");
  printf("  autostart status      Say whether it is set to.\n");
#ifdef _WIN32
  printf("  service install       Install the Windows service.\n");
  printf("  service uninstall     Remove it.\n");
  printf("  service start|stop    Start or stop it.\n");
#endif
  printf("\nOptions:\n");
  printf("  -c, --config FILE     Use this configuration file.\n");
  printf("      --no-ui           Share without opening the window.\n");
  printf("  -V, --version         Print the version and exit.\n");
  printf("  -h, --help            Show this message.\n");
  printf("\nA configuration file may also be given as a bare argument, which\n");
  printf("is how sharefs-server used to be called.\n");
}

// -------------------------------------------------------------------------
// Serving
// -------------------------------------------------------------------------

// SIGINT and SIGTERM ask the loop to finish rather than killing the process
// where it stands, so the log is flushed and the sockets are closed. Without
// this, `systemctl stop` and Ctrl-C both went through the default action.
static void handle_signal(int sig) {
  (void)sig;
  sfs_server_request_stop();
}

static void install_signal_handlers(void) {
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
}

// Resolve the configuration to use, creating a starter one if there is none.
// Returns 0 on success, -1 if there is nothing usable and nothing could be
// written either.
static int resolve_config(const char *requested, char *out, size_t out_sz,
                          int quiet) {
  if (requested && requested[0]) {
    FILE *f = fopen(requested, "r");
    if (!f) {
      fprintf(stderr, "Cannot read configuration file: %s\n", requested);
      return -1;
    }
    fclose(f);
    snprintf(out, out_sz, "%s", requested);
    return 0;
  }

  if (sfs_paths_find_config(out, out_sz) == 0)
    return 0;

  // Nothing to find. A first run used to stop here with "No configuration file
  // found", which leaves a new user with an error and no way forward, so write
  // a working one instead and say where it went.
  char created[SFS_PATH_MAX];
  if (sfs_paths_default_config(created, sizeof(created)) != 0) {
    fprintf(stderr, "No configuration file, and nowhere obvious to put one.\n");
    return -1;
  }

  char err[512];
  if (sfs_paths_write_default_config(created, NULL, NULL, err, sizeof(err)) != 0) {
    fprintf(stderr, "No configuration file found, and one could not be "
                    "created: %s\n", err);
    return -1;
  }

  if (!quiet) {
    char share[SFS_PATH_MAX];
    printf("First run: created %s\n", created);
    if (sfs_paths_default_share(share, sizeof(share)) == 0)
      printf("Sharing %s as \"Public\". Edit the file, or run ShareFS, to "
             "change that.\n", share);
  }

  snprintf(out, out_sz, "%s", created);
  return 0;
}

int sfs_cli_serve(const char *config_path) {
  char path[SFS_PATH_MAX];
  if (resolve_config(config_path, path, sizeof(path), 0) != 0)
    return EXIT_FAILURE;

  if (sfs_platform_init() != 0) {
    fprintf(stderr, "Platform init failed\n");
    return EXIT_FAILURE;
  }

  // Read the configuration before opening the log: the log location is one of
  // the things it can set.
  sfs_config cfg;
  if (sfs_config_load(path, &cfg) != 0) {
    fprintf(stderr, "Failed to load config: %s\n", path);
    sfs_platform_shutdown();
    return EXIT_FAILURE;
  }

  if (sfs_config_validate(&cfg) != 0) {
    fprintf(stderr, "Invalid configuration\n");
    sfs_config_unload(&cfg);
    sfs_platform_shutdown();
    return EXIT_FAILURE;
  }

  sfs_log_set_path(cfg.server.log_file);
  sfs_log_init();
  sfs_log_set_level(sfs_log_level_from_string(cfg.server.log_level));

  // Report the build version on startup, so a release's assets can be checked
  // against what is actually running (issue #17).
  sfs_log(SFS_LOG_INFO, "ShareFS %s starting", SHAREFS_VERSION);
  sfs_log(SFS_LOG_INFO, "Configuration: %s", path);
  if (sfs_log_get_path())
    sfs_log(SFS_LOG_INFO, "Log file: %s", sfs_log_get_path());

  sfs_net net;
  if (cfg.server.bind_ip) {
    sfs_log(SFS_LOG_INFO, "Binding to specific address: %s",
            cfg.server.bind_ip);
  }
  if (sfs_net_open(&net, cfg.server.bind_ip) != 0) {
    // Naming the ports matters: the thing holding them is not always another
    // ShareFS. RPCEmu binds all three when it is emulating a RISC OS machine
    // on the same computer.
    fprintf(stderr,
            "Could not bind UDP ports %d, %d and %d.\n"
            "Another program is using them: ShareFS already running in the "
            "background, or an emulator such as RPCEmu.\n",
            SFS_PORT_BROADCAST, SFS_PORT_AUTH, SFS_PORT_RPC);
    sfs_log_shutdown();
    sfs_config_unload(&cfg);
    sfs_platform_shutdown();
    return EXIT_FAILURE;
  }

  sfs_handle_table handles;
  sfs_handles_init(&handles);

  install_signal_handlers();
  sfs_server_clear_stop();

  int rc = EXIT_SUCCESS;
  if (sfs_server_run(&cfg, &net, &handles) != 0) {
    fprintf(stderr, "Server failed\n");
    rc = EXIT_FAILURE;
  }

  sfs_handles_free(&handles);
  sfs_net_close(&net);

  sfs_printers_shutdown();
  sfs_config_unload(&cfg);
  sfs_log_shutdown();
  sfs_platform_shutdown();
  return rc;
}

// -------------------------------------------------------------------------
// Reporting
// -------------------------------------------------------------------------

static int cmd_status(const char *requested) {
  char path[SFS_PATH_MAX];
  int have_config = 0;

  if (requested && requested[0]) {
    snprintf(path, sizeof(path), "%s", requested);
    FILE *f = fopen(path, "r");
    if (f) {
      fclose(f);
      have_config = 1;
    }
  } else {
    have_config = (sfs_paths_find_config(path, sizeof(path)) == 0);
  }

  printf("ShareFS %s\n", SHAREFS_VERSION);

  if (have_config) {
    printf("Configuration:  %s\n", path);
  } else {
    char would[SFS_PATH_MAX];
    if (sfs_paths_default_config(would, sizeof(would)) == 0)
      printf("Configuration:  none yet (a first run would create %s)\n", would);
    else
      printf("Configuration:  none yet\n");
  }

  if (have_config) {
    sfs_config cfg;
    if (sfs_config_load(path, &cfg) == 0) {
      printf("Shares:         %zu\n", cfg.share_count);
      for (size_t i = 0; i < cfg.share_count; ++i) {
        printf("  %-16s %s\n", cfg.shares[i].name ? cfg.shares[i].name : "?",
               cfg.shares[i].path ? cfg.shares[i].path : "?");
      }
      if (cfg.printer_count)
        printf("Printers:       %zu\n", cfg.printer_count);
      sfs_config_unload(&cfg);
    } else {
      printf("Shares:         could not read %s\n", path);
    }
  }

  sfs_autostart_state state = sfs_autostart_query();
  const char *desc = "not available on this system";
  if (state == SFS_AUTOSTART_ENABLED)
    desc = "on";
  else if (state == SFS_AUTOSTART_DISABLED)
    desc = "off";
  printf("Background:     %s (%s)\n", desc, sfs_autostart_mechanism());
  printf("Sharing now:    %s\n",
         sfs_autostart_is_running() ? "yes, in the background" : "not in the background");
  return EXIT_SUCCESS;
}

static int cmd_config(int argc, char **argv, const char *requested) {
  const char *sub = (argc > 0) ? argv[0] : "path";

  if (sfs_strcasecmp(sub, "path") == 0) {
    char path[SFS_PATH_MAX];
    if (requested && requested[0]) {
      printf("%s\n", requested);
      return EXIT_SUCCESS;
    }
    if (sfs_paths_find_config(path, sizeof(path)) == 0) {
      printf("%s\n", path);
      return EXIT_SUCCESS;
    }
    if (sfs_paths_default_config(path, sizeof(path)) == 0) {
      fprintf(stderr, "No configuration file yet. `sharefs config create` "
                      "would write %s\n", path);
      return EXIT_FAILURE;
    }
    fprintf(stderr, "No configuration file, and nowhere obvious to put one.\n");
    return EXIT_FAILURE;
  }

  if (sfs_strcasecmp(sub, "create") == 0) {
    char path[SFS_PATH_MAX];
    if (requested && requested[0])
      snprintf(path, sizeof(path), "%s", requested);
    else if (sfs_paths_default_config(path, sizeof(path)) != 0) {
      fprintf(stderr, "Nowhere obvious to put a configuration file.\n");
      return EXIT_FAILURE;
    }

    char err[512];
    if (sfs_paths_write_default_config(path, NULL, NULL, err, sizeof(err)) != 0) {
      fprintf(stderr, "%s\n", err);
      return EXIT_FAILURE;
    }
    printf("Created %s\n", path);
    return EXIT_SUCCESS;
  }

  if (sfs_strcasecmp(sub, "search") == 0) {
    char candidates[SFS_MAX_CONFIG_CANDIDATES][SFS_PATH_MAX];
    size_t count =
        sfs_paths_config_candidates(candidates, SFS_MAX_CONFIG_CANDIDATES);
    for (size_t i = 0; i < count; ++i) {
      FILE *f = fopen(candidates[i], "r");
      printf("%c %s\n", f ? '*' : ' ', candidates[i]);
      if (f)
        fclose(f);
    }
    printf("\n* marks the file in use; the first one found wins.\n");
    return EXIT_SUCCESS;
  }

  fprintf(stderr, "Unknown config command: %s\n", sub);
  fprintf(stderr, "Try: path, create, search\n");
  return EXIT_FAILURE;
}

static int cmd_autostart(int argc, char **argv) {
  const char *sub = (argc > 0) ? argv[0] : "status";
  char err[512];

  if (sfs_strcasecmp(sub, "status") == 0) {
    sfs_autostart_state state = sfs_autostart_query();
    if (state == SFS_AUTOSTART_UNSUPPORTED) {
      printf("Background sharing cannot be managed on this system.\n");
      return EXIT_FAILURE;
    }
    printf("Background sharing is %s, using %s.\n",
           state == SFS_AUTOSTART_ENABLED ? "on" : "off",
           sfs_autostart_mechanism());
    printf("Running now: %s\n", sfs_autostart_is_running() ? "yes" : "no");
    return EXIT_SUCCESS;
  }

  int enable = -1;
  if (sfs_strcasecmp(sub, "on") == 0 || sfs_strcasecmp(sub, "enable") == 0)
    enable = 1;
  else if (sfs_strcasecmp(sub, "off") == 0 || sfs_strcasecmp(sub, "disable") == 0)
    enable = 0;

  if (enable < 0) {
    fprintf(stderr, "Unknown autostart command: %s\n", sub);
    fprintf(stderr, "Try: on, off, status\n");
    return EXIT_FAILURE;
  }

  if (sfs_autostart_set(enable, err, sizeof(err)) != 0) {
    fprintf(stderr, "%s\n", err);
    return EXIT_FAILURE;
  }
  printf("Background sharing is now %s.\n", enable ? "on" : "off");
  return EXIT_SUCCESS;
}

#ifdef _WIN32
static int cmd_service(int argc, char **argv) {
  const char *sub = (argc > 0) ? argv[0] : "";

  if (sfs_strcasecmp(sub, "run") == 0)
    return sfs_service_dispatch() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  if (sfs_strcasecmp(sub, "install") == 0)
    return sfs_service_install();
  if (sfs_strcasecmp(sub, "uninstall") == 0 || sfs_strcasecmp(sub, "remove") == 0)
    return sfs_service_uninstall();
  if (sfs_strcasecmp(sub, "start") == 0)
    return sfs_service_start();
  if (sfs_strcasecmp(sub, "stop") == 0)
    return sfs_service_stop();
  if (sfs_strcasecmp(sub, "status") == 0) {
    printf("Installed: %s\n", sfs_service_is_installed() ? "yes" : "no");
    printf("Running:   %s\n", sfs_service_is_running() ? "yes" : "no");
    return EXIT_SUCCESS;
  }

  fprintf(stderr, "Unknown service command: %s\n", sub);
  fprintf(stderr, "Try: install, uninstall, start, stop, status\n");
  return EXIT_FAILURE;
}
#endif

// -------------------------------------------------------------------------
// Argument parsing
// -------------------------------------------------------------------------

// The basename of argv[0], without a .exe suffix, so the old names can keep
// their old behaviour after being installed as links to this binary.
static void program_name(const char *argv0, char *out, size_t out_sz) {
  const char *base = argv0 ? argv0 : "";
  for (const char *p = base; *p; ++p) {
    if (*p == '/' || *p == '\\')
      base = p + 1;
  }
  snprintf(out, out_sz, "%s", base);
  size_t len = strlen(out);
  if (len > 4 && sfs_strcasecmp(out + len - 4, ".exe") == 0)
    out[len - 4] = '\0';
}

// Hand over to the window.
//
// wxWidgets parses the command line itself and knows nothing about the
// subcommands here, so it gets one it understands: the program name and, at
// most, the configuration file as a single positional argument. Passing the
// real argv through made `sharefs --config x` fail with "Unknown long option
// 'config'" from wx's parser after this one had already accepted it.
static int launch_gui(sfs_cli_gui_fn gui, char **argv, const char *config) {
  char *gui_argv[3];
  int gui_argc = 0;

  gui_argv[gui_argc++] = argv[0];
  if (config && config[0]) {
    // wx does not write to argv, and this points either into argv itself or at
    // a string literal, so the cast is safe.
    gui_argv[gui_argc++] = (char *)config;
  }
  gui_argv[gui_argc] = NULL;

  return gui(gui_argc, gui_argv);
}

int sfs_cli_main(int argc, char **argv, sfs_cli_gui_fn gui) {
  const char *config = NULL;
  const char *command = NULL;
  int no_ui = 0;
  char *rest[8];
  int rest_count = 0;

  char invoked_as[64];
  program_name(argc > 0 ? argv[0] : NULL, invoked_as, sizeof(invoked_as));

  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];

    if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
      print_usage();
      return EXIT_SUCCESS;
    }
    if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
      printf("ShareFS %s\n", SHAREFS_VERSION);
      return EXIT_SUCCESS;
    }
    if (strcmp(a, "--no-ui") == 0) {
      no_ui = 1;
      continue;
    }
    if (strcmp(a, "-c") == 0 || strcmp(a, "--config") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s needs a file path\n", a);
        return EXIT_FAILURE;
      }
      config = argv[++i];
      continue;
    }
    if (strncmp(a, "--config=", 9) == 0) {
      config = a + 9;
      continue;
    }
    if (a[0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", a);
      print_usage();
      return EXIT_FAILURE;
    }

    // First bare word is the command, unless it looks like the configuration
    // file that sharefs-server used to be given.
    if (!command) {
      if (strcmp(a, "serve") == 0 || strcmp(a, "status") == 0 ||
          strcmp(a, "config") == 0 || strcmp(a, "autostart") == 0 ||
          strcmp(a, "service") == 0 || strcmp(a, "gui") == 0) {
        command = a;
      } else {
        config = a;
      }
      continue;
    }

    if (rest_count < (int)(sizeof(rest) / sizeof(rest[0])))
      rest[rest_count++] = argv[i];
  }

  if (command) {
    // `sharefs serve my.conf` reads the way `sharefs my.conf` used to, so a
    // bare word after a command that takes no subcommand of its own is the
    // configuration file rather than a mistake.
    const int takes_config = (strcmp(command, "serve") == 0 ||
                              strcmp(command, "status") == 0 ||
                              strcmp(command, "gui") == 0);
    if (takes_config && !config && rest_count > 0)
      config = rest[0];

    if (strcmp(command, "serve") == 0)
      return sfs_cli_serve(config);
    if (strcmp(command, "status") == 0)
      return cmd_status(config);
    if (strcmp(command, "config") == 0)
      return cmd_config(rest_count, rest, config);
    if (strcmp(command, "autostart") == 0)
      return cmd_autostart(rest_count, rest);
#ifdef _WIN32
    if (strcmp(command, "service") == 0)
      return cmd_service(rest_count, rest);
#else
    if (strcmp(command, "service") == 0) {
      fprintf(stderr, "`service` is a Windows command. On this system, use "
                      "`sharefs autostart on`.\n");
      return EXIT_FAILURE;
    }
#endif
    if (strcmp(command, "gui") == 0) {
      if (!gui) {
        fprintf(stderr, "This build has no window. Use `sharefs serve`.\n");
        return EXIT_FAILURE;
      }
      return launch_gui(gui, argv, config);
    }
  }

  // No command. Open the window if there is one and nobody said not to.
  //
  // An installation may still have sharefs-server and sharefs-admin as links
  // to this binary, and somebody's script will be calling them, so honour what
  // the link was called.
  int wants_server = no_ui || strcmp(invoked_as, "sharefs-server") == 0 ||
                     strcmp(invoked_as, "sharefs-service") == 0;

  if (gui && !wants_server)
    return launch_gui(gui, argv, config);

  return sfs_cli_serve(config);
}
