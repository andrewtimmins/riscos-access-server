// RISC OS Access Server - Admin GUI Config I/O Implementation

#include "ConfigIO.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// Trim whitespace from string
static std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Convert string to lowercase
static std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool RasConfig::Load(const std::string& path, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "Cannot open file: " + path;
        return false;
    }
    
    // Clear existing data
    m_server = ServerConfig();
    m_shares.clear();
    m_printers.clear();
    m_mimemap.clear();
    
    std::string line;
    std::string currentSection;
    std::string currentName;
    ShareConfig* currentShare = nullptr;
    PrinterConfig* currentPrinter = nullptr;
    bool inMimeMap = false;
    
    while (std::getline(file, line)) {
        line = Trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        
        // Section header
        if (line[0] == '[' && line.back() == ']') {
            std::string section = line.substr(1, line.size() - 2);
            
            // Parse section type and name
            size_t colonPos = section.find(':');
            if (colonPos != std::string::npos) {
                currentSection = ToLower(section.substr(0, colonPos));
                currentName = section.substr(colonPos + 1);
            } else {
                currentSection = ToLower(section);
                currentName.clear();
            }
            
            // Create appropriate config object
            currentShare = nullptr;
            currentPrinter = nullptr;
            inMimeMap = false;
            
            if (currentSection == "share" && !currentName.empty()) {
                m_shares.push_back(ShareConfig());
                currentShare = &m_shares.back();
                currentShare->name = currentName;
            } else if (currentSection == "printer" && !currentName.empty()) {
                m_printers.push_back(PrinterConfig());
                currentPrinter = &m_printers.back();
                currentPrinter->name = currentName;
            } else if (currentSection == "mimemap") {
                inMimeMap = true;
            }
            continue;
        }
        
        // Key = Value
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos)
            continue;
        
        std::string key = Trim(ToLower(line.substr(0, eqPos)));
        std::string value = Trim(line.substr(eqPos + 1));
        
        // Parse based on current section
        if (currentSection == "server") {
            if (key == "log_level") {
                m_server.log_level = value;
            } else if (key == "broadcast_interval") {
                m_server.broadcast_interval = std::stoi(value);
            } else if (key == "access_plus") {
                m_server.access_plus = (ToLower(value) == "true" || value == "1");
            }
        } else if (currentShare) {
            if (key == "path") {
                currentShare->path = value;
            } else if (key == "attributes") {
                currentShare->attributes = StringToAttrs(value);
            } else if (key == "password") {
                currentShare->password = value;
            } else if (key == "default_filetype") {
                currentShare->default_type = value;
            }
        } else if (currentPrinter) {
            if (key == "path") {
                currentPrinter->path = value;
            } else if (key == "definition") {
                currentPrinter->definition = value;
            } else if (key == "description") {
                currentPrinter->description = value;
            } else if (key == "poll_interval") {
                currentPrinter->poll_interval = std::stoi(value);
            } else if (key == "command") {
                currentPrinter->command = value;
            }
        } else if (inMimeMap) {
            // key is extension, value is filetype
            MimeEntry entry;
            entry.ext = key;
            entry.filetype = value;
            m_mimemap.push_back(entry);
        }
    }
    
    // Add default MIME mappings if none defined
    if (m_mimemap.empty()) {
        AddDefaultMimeMap();
    }
    
    return true;
}

bool RasConfig::Save(const std::string& path, std::string& error) {
    std::ofstream file(path);
    if (!file.is_open()) {
        error = "Cannot write to file: " + path;
        return false;
    }
    
    file << "# Access/ShareFS Server Configuration\n\n";
    
    // Server section
    file << "[server]\n";
    file << "log_level = " << m_server.log_level << "\n";
    file << "broadcast_interval = " << m_server.broadcast_interval << "\n";
    file << "access_plus = " << (m_server.access_plus ? "true" : "false") << "\n";
    file << "\n";
    
    // Shares
    for (const auto& share : m_shares) {
        file << "[share:" << share.name << "]\n";
        file << "path = " << share.path << "\n";
        if (share.attributes) {
            file << "attributes = " << AttrsToString(share.attributes) << "\n";
        }
        if (!share.password.empty()) {
            file << "password = " << share.password << "\n";
        }
        if (!share.default_type.empty()) {
            file << "default_filetype = " << share.default_type << "\n";
        }
        file << "\n";
    }
    
    // Printers
    for (const auto& printer : m_printers) {
        file << "[printer:" << printer.name << "]\n";
        if (!printer.path.empty())
            file << "path = " << printer.path << "\n";
        if (!printer.definition.empty())
            file << "definition = " << printer.definition << "\n";
        if (!printer.description.empty())
            file << "description = " << printer.description << "\n";
        file << "poll_interval = " << printer.poll_interval << "\n";
        if (!printer.command.empty())
            file << "command = " << printer.command << "\n";
        file << "\n";
    }
    
    // MIME map
    if (!m_mimemap.empty()) {
        file << "[mimemap]\n";
        for (const auto& entry : m_mimemap) {
            file << entry.ext << " = " << entry.filetype << "\n";
        }
    }
    
    return true;
}

std::string RasConfig::AttrsToString(uint32_t attrs) {
    std::vector<std::string> parts;
    if (attrs & RAS_ATTR_PROTECTED) parts.push_back("protected");
    if (attrs & RAS_ATTR_READONLY) parts.push_back("readonly");
    if (attrs & RAS_ATTR_HIDDEN) parts.push_back("hidden");
    if (attrs & RAS_ATTR_CDROM) parts.push_back("cdrom");
    
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += ",";
        result += parts[i];
    }
    return result;
}

uint32_t RasConfig::StringToAttrs(const std::string& str) {
    uint32_t attrs = 0;
    std::string lower = ToLower(str);
    
    if (lower.find("protected") != std::string::npos) attrs |= RAS_ATTR_PROTECTED;
    if (lower.find("readonly") != std::string::npos) attrs |= RAS_ATTR_READONLY;
    if (lower.find("hidden") != std::string::npos) attrs |= RAS_ATTR_HIDDEN;
    if (lower.find("cdrom") != std::string::npos) attrs |= RAS_ATTR_CDROM;
    
    return attrs;
}

void RasConfig::AddDefaultMimeMap() {
    // Common file extensions to RISC OS filetypes
    m_mimemap.push_back({"txt", "FFF"});   // Text
    m_mimemap.push_back({"text", "FFF"});  // Text
    m_mimemap.push_back({"html", "FAF"});  // HTML
    m_mimemap.push_back({"htm", "FAF"});   // HTML
    m_mimemap.push_back({"css", "F79"});   // CSS
    m_mimemap.push_back({"js", "F81"});    // JavaScript
    m_mimemap.push_back({"json", "F81"});  // JSON (as JavaScript)
    m_mimemap.push_back({"xml", "F80"});   // XML
    m_mimemap.push_back({"jpg", "C85"});   // JPEG
    m_mimemap.push_back({"jpeg", "C85"});  // JPEG
    m_mimemap.push_back({"png", "B60"});   // PNG
    m_mimemap.push_back({"gif", "695"});   // GIF
    m_mimemap.push_back({"bmp", "69C"});   // BMP
    m_mimemap.push_back({"tif", "FF0"});   // TIFF
    m_mimemap.push_back({"tiff", "FF0"});  // TIFF
    m_mimemap.push_back({"pdf", "ADF"});   // PDF
    m_mimemap.push_back({"zip", "A91"});   // Archive
    m_mimemap.push_back({"mp3", "1AD"});   // MPEG Audio
    m_mimemap.push_back({"wav", "FB1"});   // AIFF/Wave
    m_mimemap.push_back({"avi", "FB2"});   // AVI
    m_mimemap.push_back({"mp4", "BF8"});   // MPEG-4
    m_mimemap.push_back({"mov", "BF8"});   // QuickTime
    m_mimemap.push_back({"c", "102"});     // C source
    m_mimemap.push_back({"h", "102"});     // C header
    m_mimemap.push_back({"cpp", "102"});   // C++ source
    m_mimemap.push_back({"py", "A73"});    // Python
    m_mimemap.push_back({"sh", "FEB"});    // Shell script
    m_mimemap.push_back({"csv", "DFE"});   // CSV
}
