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

#include "config.h"
#include "handle.h"
#include "log.h"
#include "net.h"
#include "platform.h"
#include "printer.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *argv0) {
  printf("Usage: %s [options] [config-file]\n\n", argv0);
  printf("Runs the ShareFS server. With no config file given, the standard\n");
  printf("locations are searched.\n\n");
  printf("Options:\n");
  printf("  --no-ui       Run headless. Accepted for symmetry with\n");
  printf("                sharefs-admin, which can host the server itself;\n");
  printf("                this binary is always headless, so it is a no-op.\n");
  printf("  -h, --help    Show this message.\n");
}

int main(int argc, char **argv) {
  const char *config_path = NULL;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return EXIT_SUCCESS;
    }
    if (strcmp(argv[i], "--no-ui") == 0) {
      continue; // Already headless; accepted so scripts can pass it freely.
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
    config_path = argv[i];
  }

  if (config_path == NULL) {
#ifdef _WIN32
    static const char *search_paths[] = {
        "C:\\ShareFS\\sharefs.conf",
        "C:\\ProgramData\\ShareFS\\sharefs.conf",
        "sharefs.conf",
        NULL};
#elif defined(__APPLE__)
    static const char *search_paths[] = {"/opt/homebrew/etc/sharefs.conf",
                                         "/usr/local/etc/sharefs.conf",
                                         "/etc/sharefs.conf", "sharefs.conf",
                                         NULL};
#else
    static const char *search_paths[] = {"/etc/sharefs.conf", "sharefs.conf",
                                         NULL};
#endif
    for (const char **p = search_paths; *p != NULL; ++p) {
      FILE *f = fopen(*p, "r");
      if (f) {
        fclose(f);
        config_path = *p;
        break;
      }
    }

    if (!config_path) {
      fprintf(stderr, "No configuration file found.\n");
      fprintf(stderr, "Create /etc/sharefs.conf or ./sharefs.conf, or specify "
                      "path as argument.\n");
      return EXIT_FAILURE;
    }
  }

  if (sfs_platform_init() != 0) {
    fprintf(stderr, "Platform init failed\n");
    return EXIT_FAILURE;
  }

  // Read the configuration before opening the log: the log location is one of
  // the things it can set.
  sfs_config cfg;
  if (sfs_config_load(config_path, &cfg) != 0) {
    fprintf(stderr, "Failed to load config: %s\n", config_path);
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
  sfs_log(SFS_LOG_INFO, "ShareFS Server %s starting", SHAREFS_VERSION);
  sfs_log(SFS_LOG_INFO, "Configuration: %s", config_path);
  if (sfs_log_get_path())
    sfs_log(SFS_LOG_INFO, "Log file: %s", sfs_log_get_path());

  sfs_net net;
  if (cfg.server.bind_ip) {
    sfs_log(SFS_LOG_INFO, "Binding to specific address: %s",
            cfg.server.bind_ip);
  }
  if (sfs_net_open(&net, cfg.server.bind_ip) != 0) {
    fprintf(stderr, "Failed to open network sockets\n");
    sfs_config_unload(&cfg);
    sfs_platform_shutdown();
    return EXIT_FAILURE;
  }

  sfs_handle_table handles;
  sfs_handles_init(&handles);

  if (sfs_server_run(&cfg, &net, &handles) != 0) {
    fprintf(stderr, "Server failed\n");
  }

  sfs_handles_free(&handles);
  sfs_net_close(&net);

  sfs_printers_shutdown();
  sfs_config_unload(&cfg);
  sfs_log_shutdown();
  sfs_platform_shutdown();
  return EXIT_SUCCESS;
}
