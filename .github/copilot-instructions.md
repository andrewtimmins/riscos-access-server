# RISC OS Access/ShareFS Server - Copilot Instructions

## Project Overview

This project implements an Acorn Access/ShareFS-compatible file server in C, enabling modern Linux/Windows systems to serve files to RISC OS machines over a network. It includes a wxWidgets-based Admin GUI for easy configuration and server control.

## Project Structure

```
riscos-access-server/
├── src/                    # C server source code
│   ├── main.c              # Entry point
│   ├── server.c/h          # Main server loop
│   ├── config.c/h          # Configuration file parser
│   ├── net.c/h             # Network abstraction
│   ├── broadcast.c/h       # Freeway broadcasts
│   ├── ops.c/h             # ShareFS protocol operations
│   ├── handle.c/h          # File handle management
│   ├── printer.c/h         # Printer support
│   ├── riscos.c/h          # RISC OS filetype/date utilities
│   ├── accessplus.c/h      # Access+ authentication
│   ├── platform.c/h        # Platform abstraction
│   └── log.c/h             # Logging
├── admin/                  # wxWidgets Admin GUI (C++)
│   ├── CMakeLists.txt      # GUI build configuration
│   └── src/
│       ├── main.cpp        # wxApp entry point
│       ├── MainFrame.cpp/h # Main window with tabs
│       ├── ConfigIO.cpp/h  # Config file I/O
│       ├── ServerPanel.cpp/h
│       ├── SharesPanel.cpp/h
│       ├── PrintersPanel.cpp/h
│       ├── MimePanel.cpp/h
│       └── ControlPanel.cpp/h  # Start/stop/logs
├── CMakeLists.txt          # Root build configuration
├── mingw-w64-x86_64.cmake  # MinGW cross-compile toolchain
├── access.conf             # Sample configuration
└── README.md
```

## Design Decisions

- **Server Language**: C11 with minimal dependencies (POSIX/Winsock only)
- **Admin GUI**: wxWidgets C++ for cross-platform native look and static linking
- **Handle limit**: Dynamic allocation (no artificial 256 limit)
- **Configuration**: INI-style `access.conf` file
- **Cross-compilation**: Full Windows support via MinGW-w64

## Building

### Linux (Native)

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
# Produces: build/src/access, build/admin/access-admin
```

### Windows (Cross-Compile)

```bash
# Server only
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake -DRAS_BUILD_ADMIN=OFF
cmake --build build-win

# Server + GUI (requires wxWidgets built for MinGW, see README.md)
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake \
    -DwxWidgets_CONFIG_EXECUTABLE=$HOME/wxWidgets-mingw/install/bin/wx-config
cmake --build build-win
```

## Admin GUI Architecture

The Admin GUI uses wxWidgets with a tabbed notebook interface:

- **MainFrame**: Main window, handles file menu, Apply/Revert buttons
- **ConfigIO**: Parses and writes `access.conf` (mirrors server's config.c logic)
- **ServerPanel**: Log level, broadcast interval, Access+ toggle
- **SharesPanel**: CRUD for shares with attribute checkboxes
- **PrintersPanel**: CRUD for printers with spool settings
- **MimePanel**: Extension-to-filetype mappings
- **ControlPanel**: Start/stop/restart buttons, live log viewer

### Key GUI Patterns

- All panels receive a `MainFrame*` pointer for accessing config and setting modified state
- `RefreshFromConfig()` method on each panel to reload from config object
- Call `m_frame->SetModified(true)` when any field changes
- "Apply & Restart" saves config and calls `ControlPanel::RestartServer()`

## Protocol Fundamentals

The ShareFS protocol uses **UDP** on three fixed ports:

| Port  | Purpose |
|-------|---------|
| 32770 | Freeway broadcasts (host/share/printer availability) |
| 32771 | Access+ authentication and secure share discovery |
| 49171 | File operations (RPC-style request/response) |

### Message Format (Port 49171)

- **Byte 0**: Command character (e.g., `'A'`, `'B'`, `'R'`, `'E'`, `'S'`, `'D'`)
- **Bytes 1-3**: Reply ID (correlation token, echoed in responses)
- **Bytes 4+**: Command-specific payload

### Operation Codes 

```c
enum op {
    RFIND        = 0,   // Find file info
    ROPENIN      = 1,   // Open for reading
    ROPENUP      = 2,   // Open for read/write
    ROPENDIR     = 3,   // Open directory (returns 2048 bytes of catalogue)
    RCREATE      = 4,   // Create file
    RCREATEDIR   = 5,   // Create directory
    RDELETE      = 6,   // Delete file/directory
    RACCESS      = 7,   // Set access attributes
    RFREESPACE   = 8,   // Get free space
    RRENAME      = 9,   // Rename (data contains new path)
    RCLOSE       = 10,  // Close handle
    RREAD        = 11,  // Read file data
    RWRITE       = 12,  // Write file data
    RREADDIR     = 13,  // Read directory contents
    RENSURE      = 14,  // Ensure file size allocated
    RSETLENGTH   = 15,  // Set file length
    RSETINFO     = 16,  // Set load/exec addresses (filetype/date)
    RGETSEQPTR   = 17,  // Get sequential pointer
    RSETSEQPTR   = 18,  // Set sequential pointer
    RDEADHANDLES = 19,  // Server broadcast: invalidated handles
    RZERO        = 20,  // Write zeros to file
    RVERSION     = 21,  // Get server protocol version (returns 2)
};
```

### Share Attributes 

```c
#define ATTRIBUTE_PROTECTED 0x01  // Requires authentication
#define ATTRIBUTE_READONLY  0x02  // Read-only share
#define ATTRIBUTE_HIDDEN    0x04  // Hidden from browser
#define ATTRIBUTE_SUBDIR    0x08  // Access+ subdirectory share
#define ATTRIBUTE_CDROM     0x10  // CD-ROM share

### Advanced Protocol Logic

**A-Command RREAD (0x0b):**
Uses a 4-step "Ping-Pong" state machine to prevent UDP packet loss:
1. Client sends `A` command (RREAD). Srv creates `pending_read_t`.
2. Srv sends `D` packet (chunk of data).
3. Client sends `r` acknowledgement packet.
4. Srv sends next `D` packet or completion status.

**B-Command RREAD:**
If `pos == 0xFFFFFFFF`, perform a **sequential read** from the current file pointer (`lseek(fd, 0, SEEK_CUR)`).

**RWRITE (0x0c):**
Must enforce **strict sequentiality** of incoming `d` packets. If a gap is detected (packet loss), drop the packet and send `w` (ACK) to request retransmission of the missing offset. Do NOT `lseek` past holes, as this creates zero-filled corruption.

**RGETSEQPTR (0x11):**
Returns the current sequential file pointer using `lseek(fd, 0, SEEK_CUR)`. Important for execution of Obey/Run files.

**Text Files:**
The server treats all files as binary. Text file translation (LF vs CR) is **NOT** performed. Obey files (`&FEB`) must have CR line endings to execute on RISC OS 3.70+.
```

## RISC OS Date/Time Format

5-byte centiseconds since 1900-01-01. Load/exec addresses encode filetype and timestamp:

```c
// Load address format: 0xFFFTTTdd where TTT=filetype, dd=high byte of date
load_addr = 0xFFF00000 | (filetype << 8) | ((centiseconds >> 32) & 0xFF)
exec_addr = centiseconds & 0xFFFFFFFF
```

## Password Encoding (Access+)

```c
static int encode_psw_char(char c) {
    c = toupper(c);
    if (isdigit(c)) return (c - '0') + 1;
    if (isalpha(c)) return (c - 'A') + 11;
    return 0;
}

static int password_to_pin(char *buf) {
    int pin = 0;
    for (; *buf; buf++) {
        pin *= 37;
        pin += encode_psw_char(*buf);
    }
    return pin;
}
```

## Configuration File Format

```ini
[server]
log_level = info
broadcast_interval = 60
access_plus = true
bind_ip = 192.168.1.100  # Required for Windows WiFi

[share:Documents]
path = /home/user/documents
attributes = protected
password = secret
default_filetype = FFF

[printer:LaserJet]
path = /var/spool/riscos/laserjet
definition = /usr/share/riscos/printers/PostScript,fc6
description = PostScript Level 2
poll_interval = 5
command = lpr -P laserjet %f

[mimemap]
pdf = ADF
txt = FFF
```

## Cross-Platform Notes

### Socket Differences

- **Windows**: Use same socket for broadcast and receive; bind to host address. **MUST** bind to specific IP (`bind_ip`) for WiFi to work (multicast behavior).
- **Linux**: Separate sockets; bind broadcast socket to broadcast address.

### Build System

CMake with MinGW support:
```cmake
if(WIN32)
    target_link_libraries(access ws2_32)
else()
    target_link_libraries(access pthread)
endif()
```

Admin GUI uses wxWidgets with static linking for Windows (single .exe).

## Testing

- RPCEmu (RISC OS 3.x, 5.x emulator)
- Real RISC OS hardware
- Wireshark with UDP port filters on 32770, 32771, 49171
