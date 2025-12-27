// RISC OS Access/ShareFS Server - Entry Point
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "config.h"
#include "log.h"
#include "platform.h"
#include "net.h"
#include "handle.h"
#include "server.h"
#include "printer.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *config_path = "access.conf";
    const char *bind_addr = NULL;
    
    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            bind_addr = argv[++i];
        } else if (argv[i][0] != '-') {
            config_path = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [-b bind_addr] [config_file]\n", argv[0]);
            fprintf(stderr, "  -b bind_addr  IP address to bind to (e.g., 192.168.1.100)\n");
            fprintf(stderr, "                Required for Windows WiFi adapters!\n");
            return EXIT_SUCCESS;
        }
    }

    if (ras_platform_init() != 0) {
        fprintf(stderr, "Platform init failed\n");
        return EXIT_FAILURE;
    }

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
    if (bind_addr) {
        ras_log(RAS_LOG_INFO, "Binding to specific address: %s", bind_addr);
    }

    ras_net net;
    if (ras_net_open(&net, bind_addr) != 0) {
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
    ras_platform_shutdown();
    return EXIT_SUCCESS;
}
