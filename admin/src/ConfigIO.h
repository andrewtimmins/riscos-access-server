// RISC OS Access Server - Admin GUI Config I/O Header

#ifndef CONFIGIO_H
#define CONFIGIO_H

#include <cstdint>
#include <string>
#include <vector>

// Share attribute flags (matching server)
#define RAS_ATTR_PROTECTED  0x01
#define RAS_ATTR_READONLY   0x02
#define RAS_ATTR_HIDDEN     0x04
#define RAS_ATTR_SUBDIR     0x08
#define RAS_ATTR_CDROM      0x10

struct ShareConfig {
    std::string name;
    std::string path;
    uint32_t attributes = 0;
    std::string password;
    std::string default_type;
};

struct PrinterConfig {
    std::string name;
    std::string path;
    std::string definition;
    std::string description;
    int poll_interval = 5;
    std::string command;
};

struct MimeEntry {
    std::string ext;
    std::string filetype;
};

struct ServerConfig {
    std::string log_level = "info";
    int broadcast_interval = 60;
    bool access_plus = false;
    std::string bind_ip;
};

class RasConfig {
public:
    RasConfig() = default;
    
    bool Load(const std::string& path, std::string& error);
    bool Save(const std::string& path, std::string& error);
    
    // Accessors
    ServerConfig& Server() { return m_server; }
    std::vector<ShareConfig>& Shares() { return m_shares; }
    std::vector<PrinterConfig>& Printers() { return m_printers; }
    std::vector<MimeEntry>& MimeMap() { return m_mimemap; }
    
    // Helpers
    static std::string AttrsToString(uint32_t attrs);
    static uint32_t StringToAttrs(const std::string& str);
    void AddDefaultMimeMap();

private:
    ServerConfig m_server;
    std::vector<ShareConfig> m_shares;
    std::vector<PrinterConfig> m_printers;
    std::vector<MimeEntry> m_mimemap;
};

#endif // CONFIGIO_H
