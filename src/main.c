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
    if (argc > 1) {
        config_path = argv[1];
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

    ras_net net;
    if (ras_net_open(&net, NULL) != 0) {
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
