// RISC OS Access/ShareFS Server - Entry Point
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "config.h"
#include "handle.h"
#include "log.h"
#include "net.h"
#include "platform.h"
#include "printer.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  const char *config_path = NULL;

  if (argc > 1) {
    // Use command-line argument if provided
    config_path = argv[1];
  } else {
    // Search standard paths in order
#ifdef _WIN32
  // Windows: check default install dir then current directory
  static const char *search_paths[] = {"C:\\AccessServer\\access.conf", "access.conf", NULL};
#else
    // Linux: check system path first, then current directory
    static const char *search_paths[] = {"/etc/access.conf", "access.conf",
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
      fprintf(stderr, "Create /etc/access.conf or ./access.conf, or specify "
                      "path as argument.\n");
      return EXIT_FAILURE;
    }
  }

  if (ras_platform_init() != 0) {
    fprintf(stderr, "Platform init failed\n");
    return EXIT_FAILURE;
  }

  // Initialize logging (opens log file)
  ras_log_init();

  ras_config cfg;
  if (ras_config_load(config_path, &cfg) != 0) {
    fprintf(stderr, "Failed to load config: %s\n", config_path);
    ras_platform_shutdown();
    return EXIT_FAILURE;
  }

  if (ras_config_validate(&cfg) != 0) {
    fprintf(stderr, "Invalid configuration\n");
    ras_config_unload(&cfg);
    ras_platform_shutdown();
    return EXIT_FAILURE;
  }

  ras_log_set_level(ras_log_level_from_string(cfg.server.log_level));
  ras_log(RAS_LOG_INFO, "ras-server starting with config %s", config_path);

  ras_net net;
  if (cfg.server.bind_ip) {
    ras_log(RAS_LOG_INFO, "Binding to specific address: %s",
            cfg.server.bind_ip);
  }
  if (ras_net_open(&net, cfg.server.bind_ip) != 0) {
    fprintf(stderr, "Failed to open network sockets\n");
    ras_config_unload(&cfg);
    ras_platform_shutdown();
    return EXIT_FAILURE;
  }

  ras_handle_table handles;
  ras_handles_init(&handles);

  if (ras_server_run(&cfg, &net, &handles) != 0) {
    fprintf(stderr, "Server failed\n");
  }

  ras_handles_free(&handles);
  ras_net_close(&net);

  ras_printers_shutdown();
  ras_config_unload(&cfg);
  ras_log_shutdown();
  ras_platform_shutdown();
  return EXIT_SUCCESS;
}
