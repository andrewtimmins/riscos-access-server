// ShareFS Server - Entry Point
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
    config_path = argv[1];
  } else {
#ifdef _WIN32
    static const char *search_paths[] = {
        "C:\\ShareFS\\sharefs.conf",
        "C:\\ProgramData\\ShareFS\\sharefs.conf",
        "sharefs.conf",
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

  sfs_log_init();

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

  sfs_log_set_level(sfs_log_level_from_string(cfg.server.log_level));
  sfs_log(SFS_LOG_INFO, "ShareFS Server starting with config %s", config_path);

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
