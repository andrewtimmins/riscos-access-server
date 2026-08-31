/*
  ShareFS - Config I/O Header

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

#ifndef CONFIGIO_H
#define CONFIGIO_H

#include <cstdint>
#include <string>
#include <vector>

// Share attribute flags (matching server)
#define SFS_ATTR_PROTECTED 0x01
#define SFS_ATTR_READONLY 0x02
#define SFS_ATTR_HIDDEN 0x04
#define SFS_ATTR_SUBDIR 0x08
#define SFS_ATTR_CDROM 0x10

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
  std::string log_file;   // Empty = platform default
  int broadcast_interval = 3;
  bool access_plus = false;
  std::string bind_ip;
};

class SfsConfig {
public:
  SfsConfig() = default;

  bool Load(const std::string &path, std::string &error);
  bool Save(const std::string &path, std::string &error);

  // Accessors
  ServerConfig &Server() { return m_server; }
  std::vector<ShareConfig> &Shares() { return m_shares; }
  std::vector<PrinterConfig> &Printers() { return m_printers; }
  std::vector<MimeEntry> &MimeMap() { return m_mimemap; }

  // Helpers
  static std::string AttrsToString(uint32_t attrs);
  static uint32_t StringToAttrs(const std::string &str);
  void AddDefaultMimeMap();

private:
  ServerConfig m_server;
  std::vector<ShareConfig> m_shares;
  std::vector<PrinterConfig> m_printers;
  std::vector<MimeEntry> m_mimemap;
};

#endif // CONFIGIO_H
