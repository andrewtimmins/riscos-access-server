# ShareFS Server - Copilot Instructions

## Project Overview

ShareFS Server is an Acorn ShareFS-compatible file server in C, enabling modern Linux/Windows systems to serve files to RISC OS machines over a network. It includes **ShareFS Admin**, a wxWidgets-based GUI for configuration and server control. Internal APIs use the `sfs_` prefix.

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
│       ├── ControlPanel.cpp/h  # Start/stop/logs
│       └── UiHelpers.h         # Shared layout/styling helpers
├── CMakeLists.txt          # Root build configuration
├── cmake/toolchains/       # Cross-compile toolchain files
│   ├── linux-aarch64-gnu.cmake
│   ├── mingw-w64-x86_64.cmake
│   └── mingw-w64-aarch64.cmake
├── build.sh                # Main build script
├── setup-build-env.sh      # Dependency installer (Debian/Ubuntu)
├── installer.nsi           # Windows x64 NSIS installer
├── debian/                 # systemd unit, postinst/postrm for .deb
├── scripts/                # Firewall helper scripts
├── docs/protocol.md        # Detailed protocol notes
├── sharefs.conf.sample     # Linux sample configuration
├── sharefs.conf.sample-windows
└── README.md
```

## Design Decisions

- **Server Language**: C11 with minimal dependencies (POSIX/Winsock only)
- **Admin GUI**: wxWidgets C++ for cross-platform native look and static linking
- **Handle limit**: Dynamic allocation (no artificial 256 limit)
- **Configuration**: INI-style `sharefs.conf` file
- **Cross-compilation**: Windows x64 via MinGW-w64 (apt); Linux arm64 native or cross; Windows arm64 needs a custom MinGW toolchain
- **System Integration**: Debian package uses `systemd` and auto-configures `ufw`/`firewalld` in postinst.

## Building

### Automated Building (Recommended)

- **setup-build-env.sh**: Installs dependencies (Debian/Ubuntu), optional MinGW and arm64 cross-compiler
- **build.sh**: Main build script

#### Build Options (`./build.sh [option]`)

| Option | Effect |
|--------|--------|
| *(none)* | Linux only, host architecture |
| `--deb` | Linux + `.deb` for selected arch |
| `--arch arm64` | Linux arm64 (native on Pi) |
| `--cross-arm64` | Cross-compile Linux arm64 from x86_64 (server only) |
| `--windows-only` | Windows only (skip Linux rebuild) |
| `--windows` | Linux + Windows x64 server |
| `--windows-full` | Linux + Windows x64 + admin GUI |
| `--windows-only --zip` | Windows x64 zip + NSIS installer |
| `--all-full` | Linux + Windows x64 + deb + zip + NSIS |
| `--windows-wxwidgets` | One-time wxWidgets build for MinGW |
| `--windows-arch arm64` | Target Windows on ARM (with `--windows-only`) |
| `--clean` | Remove build dirs and `releases/` |

#### Output Structure

- `releases/linux/amd64/` and `releases/linux/arm64/` — Linux binaries and `.deb`
- `releases/windows/x64/` and `releases/windows/arm64/` — Windows executables and `.zip`
- NSIS installer (x64 only): `releases/windows/x64/sharefs-server_*-setup.exe`

Only the architecture being built gets a release folder (empty siblings are not created).

#### Architectures

- **Linux:** `amd64` (default on x86_64), `arm64` (native on Pi / ARM CI, or cross via `--cross-arm64`)
- **Windows:** `x64` (MinGW in apt), `arm64` (custom MinGW toolchain required)
- **ARM scope:** `aarch64`/`arm64` only — no 32-bit `armhf`

#### CI

`.github/workflows/build.yml` builds and tests Linux amd64 and arm64 (native + cross smoke test).

### Manual CMake

See `README.md` or `build.sh` for toolchain file paths under `cmake/toolchains/`.

## Admin GUI Architecture

The Admin GUI uses wxWidgets with a tabbed notebook interface:

- **MainFrame**: Main window, handles file menu, Apply/Revert buttons
- **ConfigIO**: Parses and writes `sharefs.conf` (mirrors server's config.c logic)
- **ServerPanel**: Log level, broadcast interval, Access+ toggle
- **SharesPanel**: CRUD for shares with attribute checkboxes
- **PrintersPanel**: CRUD for printers with spool settings
- **MimePanel**: Extension-to-filetype mappings (Enforces 3-digit Hex, Uppercase)
- **ControlPanel**: Start/stop/restart buttons, live log viewer
 
### Firewall Configuration
- **Linux**: `scripts/configure-firewall-linux.sh` (or auto-run by .deb postinst)
- **Windows**: `scripts/configure-firewall-windows.bat` (Admin required)

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
#define SFS_ATTR_PROTECTED  0x01  // Requires authentication
#define SFS_ATTR_READONLY   0x02  // Read-only share
#define SFS_ATTR_HIDDEN     0x04  // Hidden from browser
#define SFS_ATTR_SUBDIR     0x08  // Access+ subdirectory share
#define SFS_ATTR_CDROM      0x10  // CD-ROM share
```

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

**RRENAME (0x09):**
- A packet arms the rename and announces the new-name length; client then sends the name via `d` or `D` packet.
- Server tolerates an extra leading zero word in the payload and requests missing data with `w` if `data_len == 0`.
- When the source exists on disk with a `,xxx` filetype suffix (e.g., `BugReport,fff`), the server remembers that suffix and appends it to the destination if the client omits it, so `BugReport` → `BugReportass` becomes `BugReportass,fff` on disk.
- Uses `safe_rename_cross` (Windows `MoveFileEx` with replace-existing; POSIX `rename`).

**RGETSEQPTR (0x11):**
Returns the current sequential file pointer using `lseek(fd, 0, SEEK_CUR)`. Important for execution of Obey/Run files.

**Text Files:**
The server treats all files as binary. Text file translation (LF vs CR) is **NOT** performed. Obey files (`&FEB`) should use LF line endings (though RISC OS accepts any control character except tab as a line terminator).

See `docs/protocol.md` for full protocol documentation.

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
broadcast_interval = 3
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
    target_link_libraries(sharefs-server ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(sharefs-server Threads::Threads)
endif()
```

Admin GUI uses wxWidgets with static linking for Windows (single .exe).

## Testing

- RPCEmu (RISC OS 3.x, 5.x emulator)
- Real RISC OS hardware
- Wireshark with UDP port filters on 32770, 32771, 49171
