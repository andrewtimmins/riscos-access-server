// RISC OS Access/ShareFS Server - Configuration
// Author: Andrew Timmins
// License: GPL-3.0-only

#ifndef RAS_CONFIG_H
#define RAS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

// Share attribute flags
#define RAS_ATTR_PROTECTED  0x01
#define RAS_ATTR_READONLY   0x02
#define RAS_ATTR_HIDDEN     0x04
#define RAS_ATTR_SUBDIR     0x08
#define RAS_ATTR_CDROM      0x10

typedef struct {
    char *name;           // Share name from section
    char *path;           // Local path to share
    uint32_t attributes;  // Parsed attribute flags
    char *password;       // Optional password for protected shares
    char *default_type;   // Default filetype for extensionless files
} ras_share_config;

typedef struct {
    char *name;           // Printer name from section
    char *path;           // Spool directory path
    char *definition;     // Printer definition (.fc6)
    char *description;    // Human-readable description
    int poll_interval;    // Seconds between checks
    char *command;        // Print command with %f placeholder
} ras_printer_config;

typedef struct {
    char *ext;            // Extension (lowercase)
    char *filetype;       // Hex filetype string
} ras_mime_entry;

typedef struct {
    char *log_level;
    char *bind_ip;           // IP address to bind sockets to (NULL = all interfaces)
    int broadcast_interval;
    int access_plus;
} ras_server_config;

typedef struct {
    ras_server_config server;
    ras_share_config *shares;
    size_t share_count;
    ras_printer_config *printers;
    size_t printer_count;
    ras_mime_entry *mimemap;
    size_t mimemap_count;
} ras_config;

int ras_config_load(const char *path, ras_config *out);
void ras_config_unload(ras_config *cfg);
int ras_config_validate(const ras_config *cfg);

#endif
