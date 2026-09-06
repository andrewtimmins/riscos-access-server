# ShareFS Server - Copilot Instructions

## Project Overview

ShareFS Server is an Acorn ShareFS-compatible file server in C, enabling modern Linux, macOS and Windows systems to serve files to RISC OS machines over a network. Internal APIs use the `sfs_` prefix.

**One binary, several modes.** There is a single executable, `sharefs` (`ShareFS.app` on macOS, `sharefs.exe` on Windows). Run it with no arguments and it opens its wxWidgets window; give it a command and it does that instead (`serve`, `status`, `config`, `autostart`, `service`). Up to 0.1.7 this was three executables, `sharefs-server`, `sharefs-service` and `sharefs-admin`; on Linux and macOS those names are installed as symlinks and `program_name()` in `src/cli.c` honours what the link was called.

The shape of it:

- `src/` builds a static library `sfs` holding every piece of server logic.
- The entry point is either `src/main.c` (no window, `-DSFS_BUILD_ADMIN=OFF`) or `admin/src/main.cpp` (with the window). **Both call `sfs_cli_main()` in `src/cli.c`**, so the two flavours cannot answer `--help`, or find the configuration, differently.
- The window runs the server core on a worker thread *inside the same process* (`admin/src/EmbeddedServer.cpp`) rather than launching a child. Reported status is therefore a fact rather than a probe, and log lines come straight from the server.

When adding a mode or an option, put it in `src/cli.c` so both flavours get it. When adding anything that decides *where a file lives*, put it in `src/paths.c` for the same reason: the GUI and the server once carried separate search lists that disagreed, so the GUI saved to a file the service never read.

## Project Structure

```
sharefs-server/
├── src/                    # C server core, built as the static library `sfs`
│   ├── main.c              # Entry point for the build without a window
│   ├── cli.c/h             # The command line, shared by both flavours
│   ├── server.c/h          # Main server loop (single select() over the sockets)
│   ├── config.c/h          # Configuration file parser
│   ├── paths.c/h           # Where the configuration lives, for the whole product
│   ├── net.c/h             # Network abstraction
│   ├── broadcast.c/h       # Freeway broadcasts
│   ├── ops.c/h             # ShareFS protocol operations (the bulk of the code)
│   ├── handle.c/h          # File handle management, dir listing cache
│   ├── names.c/h           # RISC OS <-> host filename encoding
│   ├── printer.c/h         # Printer support
│   ├── riscos.c/h          # RISC OS filetype/date utilities
│   ├── accessplus.c/h      # Access+ authentication
│   ├── autostart.c/h       # "Keep sharing when closed", per platform
│   ├── service.c/h         # Windows service (compiled on Windows only)
│   ├── platform.c/h        # Platform abstraction
│   └── log.c/h             # Logging
├── admin/                  # The window (wxWidgets, C++). Builds `sharefs` itself.
│   ├── CMakeLists.txt      # GUI build configuration
│   ├── Info.plist.in       # macOS bundle metadata
│   └── src/
│       ├── main.cpp        # Entry point; hands argv to sfs_cli_main()
│       ├── MainFrame.cpp/h # Main window with tabs
│       ├── ConfigIO.cpp/h  # Config file I/O
│       ├── EmbeddedServer.cpp/h # Server core on a worker thread, in-process
│       ├── FirstRunDialog.cpp/h # Nothing configured yet: ask for a folder
│       ├── AboutDialog.cpp/h
│       ├── ServerPanel.cpp/h
│       ├── SharesPanel.cpp/h
│       ├── PrintersPanel.cpp/h
│       ├── MimePanel.cpp/h
│       ├── ControlPanel.cpp/h  # Status, autostart tick box, live log
│       ├── Icons.h             # Embedded SVG icons
│       └── UiHelpers.h         # Shared layout/styling helpers
├── tests/                  # Unit tests (C) and protocol tests (Python)
├── CMakeLists.txt          # Root build configuration
├── cmake/toolchains/       # Cross-compile toolchain files
│   ├── linux-aarch64-gnu.cmake
│   ├── mingw-w64-x86_64.cmake
│   └── mingw-w64-aarch64.cmake
├── build.sh                # Main build script (Linux and Windows)
├── build-macos.sh          # macOS slices, bundling and the universal fuse
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
- **Window**: wxWidgets C++ for cross-platform native look and static linking. Optional: `-DSFS_BUILD_ADMIN=OFF` produces the same `sharefs` binary and the same command line without linking wx, and running it with no arguments then serves instead of opening a window
- **Threading**: the server loop is single-threaded. Anything that could block it (a print command) must be detached; see `sfs_spawn_detached()`
- **Handle limit**: Dynamic allocation (no artificial 256 limit). Handles are bound to the address and port that opened them
- **Configuration**: INI-style `sharefs.conf` file. The search order lives in `src/paths.c` and nowhere else
- **Versioning**: single-sourced from `project()` in the root `CMakeLists.txt` into `SHAREFS_VERSION`, so the binary, the packages and the About dialog cannot disagree
- **Cross-compilation**: Windows x64 via MinGW-w64 (apt); Linux arm64 native or cross; Windows arm64 needs a custom MinGW toolchain
- **System Integration**: Debian package uses `systemd` and auto-configures `ufw`/`firewalld` in postinst. macOS uses a per-user launchd agent, needing no `sudo`

### Comment style

Several files carry a `★`-marked block explaining a protocol truth that was got wrong once and is not obvious from the code: the read chunk size and window in `ops.c`, the error-block format in `ops.c`, the attribute descriptor in `broadcast.c`, the `off_t` width trap in `riscos.h`. Preserve them when editing nearby, and add one when a fix turns on something a later reader would otherwise "simplify" back into a bug.

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

`.github/workflows/build.yml` runs on every push and pull request:

| Job | Output |
|-----|--------|
| `version-check` | On a tag, the project version must match it |
| `linux` (amd64, arm64) | Release folder and `.deb`, tests. arm64 is a native runner |
| `linux-arm64-cross` | Smoke test of the documented cross build |
| `windows` (x64, arm64) | Release zip, NSIS installer on tags |
| `macos` (arm64, x86_64) | One slice each; x86_64 is cross-compiled under Rosetta |
| `macos-universal` | Fuses both slices, verifies, produces the universal zip and .dmg |

An ordinary push builds Windows *without* the window, because wxWidgets for MinGW has to be built from source first and that is cached. Run the workflow by hand with **full** ticked to rehearse what a release ships. Pushing a version tag (`v0.1.9`) publishes a GitHub Release.

**Windows on ARM ships the server without the window.** wxWidgets cannot be cross-compiled for aarch64 with llvm-mingw: it uses libc++, which no longer provides the `char_traits` primary template, and `wxUString` is declared as `std::basic_string<wxChar32>`. Version 3.3 declares it the same way, so there is no version to move to. Do not "fix" this by trying another wx version.

### Manual CMake

See `README.md` or `build.sh` for toolchain file paths under `cmake/toolchains/`.

## Admin GUI Architecture

The Admin GUI uses wxWidgets with a tabbed notebook interface:

- **MainFrame**: Main window, file menu, toolbar, Apply/Revert
- **ConfigIO**: Parses and writes `sharefs.conf` (mirrors server's config.c logic)
- **EmbeddedServer**: Runs the server core on a worker thread in this process. Binds the sockets on the *caller's* thread so "port already in use" is reported synchronously rather than arriving later as a log line
- **FirstRunDialog**: Nothing configured yet. Asks for one folder, writes the file, starts sharing
- **ServerPanel**: Log level, broadcast interval, Access+ toggle, which config file is in use
- **SharesPanel**: CRUD for shares with attribute checkboxes
- **PrintersPanel**: CRUD for printers with spool settings
- **MimePanel**: Extension-to-filetype mappings (Enforces 3-digit Hex, Uppercase)
- **ControlPanel**: Status, the "keep sharing when this window is closed" tick box, live log viewer

Only one copy can hold the UDP ports, so the tick box hands over between the in-window server and the background one. Anything showing a dialog at startup must be queued with `CallAfter`, not run from `OnInit`: a modal dialog in `OnInit` stops NSApplication finishing its launch sequence, and the macOS Dock icon bounces for ever.

`sfs_log_set_sink()` delivers log lines to the GUI. The sink is called on the *server* thread, so it must marshal to the GUI thread before touching a widget.
 
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
    ROPENDIR     = 3,   // Open directory (catalogue follows as an S+B page)
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
    RFREESPACE64 = 22,  // 0x16: free space as lo/hi u32 pairs
};
```

### Command letters

`ops.c` dispatches on the first byte, and the layouts differ:

| Letter | Layout | Used for |
|--------|--------|----------|
| `A` | `A \| rid[3] \| code \| handle \| payload` | Most requests. Path at offset 12 for codes 0x00-0x09 and 0x16 |
| `B` | `B \| rid[3] \| code \| handle \| extra \| path` | ROPENDIR and the B-form read/readdir. **The extra word means a B path starts at offset 16, not 12** |
| `a` | `a \| rid[3] \| code \| handle \| args` | Handle-based operations |
| `F` | `F \| rid[3] \| code \| handle` | RVERSION, RDEADHANDLES |

A path-based command is authenticated by share name; a handle-based one is authorised by the handle, which is bound to the address and port that opened it. **Adding a path-carrying code to the `B` branch means adding the `check_share_auth()` call too**: ROPENDIR once listed a protected share to anyone who asked, because the check lived only in the `A` branch.

### Errors are RISC OS error blocks, not errnos

An `E` reply is `E | rid[3] | errnum(u32 le) | message(NUL-terminated)`. Both halves matter:

- The **message is not optional**. Without it the client hands the OS whatever follows the number in its own receive buffer, and every error appears as rubbish.
- The **number must be one RISC OS knows**, not a POSIX errno. The client compares against its own "not found" number to decide whether an object exists, so answering with an errno makes a copy into a share fail rather than create the object.

`riscos_error_for()` in `src/ops.c` maps host errnos onto `SFS_RO_ERR_*`. Add a mapping there rather than sending an errno.

### Path safety is two layers, and both are needed

1. `sfs_path_is_safe()` rejects absolute paths and `..`, textually.
2. `path_within_share()` canonicalises through `realpath` (Windows: `GetFinalPathNameByHandleA`) and confirms the result is genuinely under the share root, walking up to the nearest existing ancestor so that creates still work.

The second is what stops a symlink planted inside a share from escaping it. The first alone does not.

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
A sliding window, not a ping-pong.

1. Client sends `A` command (RREAD). Server creates `pending_read_t`.
2. Server sends every chunk of the window the client does not already hold, as `D` packets. Header is 8 bytes: `D | rid[3] | offset(u32)`, the offset being relative to the start of the transfer.
3. Client sends `r`: `r | rid[3] | done(u32) | bits(u32)`. `done` is how much it has contiguously received, `bits` a mask of the chunks it holds beyond that.
4. Repeat 2-3 until the acknowledged point reaches the amount asked for, then send `R` with bytes_sent and the new seqptr.

**The chunk size and window are fixed by the client, not by us.**

- `READ_CHUNK_SIZE` is **8192**. The client's ack bitmask indexes chunks by offset divided by chunk size, so a different size makes its accounting wrong.
- `READ_WINDOW_CHUNKS` is **2**. The protocol permits up to 32 and some client builds use 16, but the ordinary RISC OS module tracks 2 and discards anything past its window. Overshooting also bursts the guest's network stack, and the symptom is that small files transfer fine while larger ones fail.

Do not raise either to "make reads faster". Sending one chunk per ack instead of the whole window is the thing that made reads slow, and that is already fixed.

**B-Command RREAD:**
If `pos == 0xFFFFFFFF`, perform a **sequential read** from the current file pointer (`lseek(fd, 0, SEEK_CUR)`).

**RWRITE (0x0c):**
Must enforce **strict sequentiality** of incoming `d` packets. If a gap is detected (packet loss), drop the packet and send `w` to request retransmission of the missing offset. Do NOT `lseek` past holes, as this creates zero-filled corruption. `WRITE_CHUNK_SIZE` is 8192. The `w` packet is 16 bytes: `w | rid[3] | rel_pos(u32) | zero(u32) | rel_end(u32)`, the zero word between the two positions being part of the format. A zero-length `d` triggers retry with backoff, aborting to EIO after 1000.

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

CMake with MinGW support. The platform libraries hang off the `sfs` library as `PUBLIC`, because whichever flavour of the binary links it needs them too, and there is no longer one executable per platform to spell them out on:

```cmake
if(WIN32)
    target_link_libraries(sfs PUBLIC ws2_32 advapi32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(sfs PUBLIC Threads::Threads)
endif()
```

The window uses wxWidgets with static linking for Windows (single .exe).

## Testing

`ctest` from a build directory, or `./build.sh` which runs them. Four tests:

| Test | What it covers |
|------|----------------|
| `test_names` | RISC OS <-> host filename encoding round trips |
| `test_filedesc` | `sfs_length_for_wire()`, including the 32-bit `off_t` trap |
| `protocol_security` | Starts the real server and drives it over UDP: a protected share cannot be listed without authenticating, and a handle opened by one client cannot be used by another |
| `protocol_listing` | A RISC OS name starting with `/` is stored with a leading `.`, so the catalogue must list dotfiles |

The two Python tests bind the real UDP ports and are marked `RUN_SERIAL`. They will fail if a ShareFS instance, or RPCEmu, is already holding 32770/32771/49171.

Anything reachable only over the wire belongs in a protocol test rather than a unit test. Access control is the example: neither property above can be reached from a unit test, and both were once broken.

Beyond the suite:

- RPCEmu (RISC OS 3.x, 5.x emulator). Note that it binds all three ports itself, so it cannot run alongside the tests
- Real RISC OS hardware
- Wireshark with UDP port filters on 32770, 32771, 49171
