# RISC OS Access/ShareFS Server - Copilot Instructions

## Project Overview

This project implements an Acorn Access/ShareFS-compatible file server in C, enabling modern Linux/Windows systems to serve files to RISC OS machines over a network.

## Design Decisions

- **Handle limit**: No artificial limit (unlike RISC OS's 256) - use dynamic allocation
- **Dependencies**: Minimal - standard C library + POSIX/Winsock sockets only, no external libraries
- **Logging**: Configurable levels (PROTOCOL/API/INFO), can be disabled at compile or runtime
- **Full protocol**: Implement ALL operations including RDEADHANDLES, RFREESPACE, RZERO, RVERSION

## Architecture

### Protocol Fundamentals

The ShareFS protocol uses **UDP** on three fixed ports:

| Port  | Purpose |
|-------|---------|
| 32770 | Freeway broadcasts (host/share/printer availability) |
| 32771 | Access+ authentication and secure share discovery |
| 49171 | File operations (RPC-style request/response) |

### Message Format (Port 49171)

Messages use a binary format with little-endian integers:
- **Byte 0**: Command character (e.g., `'A'`, `'B'`, `'R'`, `'E'`, `'S'`, `'D'`)
- **Bytes 1-3**: Reply ID (correlation token, echoed in responses)
- **Bytes 4+**: Command-specific payload

Responses use: `'R'` (success), `'E'` (error), `'S'` (catalogue data), `'D'` (file data)

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
```

### FileDesc Structure 

```c
typedef struct FileDesc {
    Information_Fields info;  // Load/exec addresses (contains filetype+date)
    int length;               // File length
    int attr;                 // RISC OS attributes (R/W/L/r/w bits)
    int type:8;               // 0=not found, 1=file, 2=directory
    int buffered:1;
    int interactive:1;
    int noosgbpb:1;
} FileDesc;
```

### RISC OS Attributes 

```c
#define Attr_R  0x01  // Owner readable
#define Attr_W  0x02  // Owner writable  
#define Attr_L  0x08  // Locked
#define Attr_r  0x10  // Public readable
#define Attr_w  0x20  // Public writable
```

## Key Implementation Details

### Freeway Broadcast Format (Port 32770)

Header (3 words):
```
Word 0: (major_type << 16) | minor_type
Word 1: Flags/version info
Word 2: (length2 << 16) | length1
```

Types:
- **0x0001**: Discs (minor: 0x02=add, 0x03=remove, 0x04=periodic)
- **0x0002**: Printers (minor: 0x02=add, 0x03=remove, 0x04=periodic)
- **0x0005**: Hosts/clients

### Printer Sharing Protocol

Printers are announced on port 32770 using major type `0x0002`. The broadcast format is identical to disc shares:

```
Word 0: 0x0002XXYY  (XX=00, YY=02/03/04 for add/remove/periodic)
Word 1: 0x00010000  (flags/version)
Word 2: (description_len << 16) | name_len
Data:   name + description (null-terminated strings)
```

**Directory Structure:** When a printer is shared, the server creates:
```
<printer_directory>/
├── <printername>.fc6    # Printer definition file (filetype 0xFC6/PrntDefn)
├── RemQueue/            # Queue for jobs being processed
└── RemSpool/            # Incoming spool directory (clients write here)
```

**Print Job Flow:**
1. Client writes print job file to `RemSpool/` directory
2. Server detects new file (via polling or inotify/ReadDirectoryChanges)
3. Server moves file to `RemQueue/` for processing
4. Server executes configured print command with file as argument
5. File is deleted after successful printing

**Note:** Printer sharing was likely handled by a separate RISC OS module, not ShareFS itself. The original Acorn ShareFS source only defines `DOMAIN_DISCS` (1) and `DOMAIN_CLIENTS` (5), not printers. Our implementation adds printer support based on observed protocol behavior.

### Date/Time Format

RISC OS uses 5-byte centiseconds since 1900-01-01. The load/exec addresses encode filetype and timestamp:
```c
// Load address format: 0xFFFTTTdd where TTT=filetype, dd=high byte of date
// Exec address: low 4 bytes of date
load_addr = 0xFFF00000 | (filetype << 8) | ((centiseconds >> 32) & 0xFF)
exec_addr = centiseconds & 0xFFFFFFFF
```

### Password Encoding (Access+)

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

### Handle Management

- Handle 0 is special: represents the root directory containing all exports
- Handles are dynamically allocated (no 256 limit like RISC OS)
- Each handle contains a randomized token to detect stale references
- Server broadcasts dead handles via RDEADHANDLES to notify clients

### Directory Catalogue Format

OSGBPB format 10/11 - each entry:
```
Offset 0:  FileDesc (20 bytes)
Offset 20: Null-terminated filename
Padding:   Aligned to 4-byte boundary
```

### Chunk Size Constants

```c
#define CHUNKSIZE   8192    // Default file transfer chunk
#define WINDOWSIZE  2       // Concurrent outstanding requests (Access+)
#define ROPENDIRSIZE 2048   // Directory data returned with ROPENDIR
```

## Cross-Platform Considerations

### Socket Differences

- **Windows**: Use same socket for broadcast and receive; bind to host address
- **Linux**: Separate sockets; bind broadcast socket to broadcast address

### Build System

Use CMake with MinGW support:
```cmake
if(WIN32)
    target_link_libraries(access ws2_32)
else()
    target_link_libraries(access pthread)
endif()
```

## Known Issues to Fix

1. Directory catalogue returns incorrect date/access in trailer
2. Setting filetype fails from RISC OS 5 Filer
3. Changing from "Protected" access mode fails

## File Naming Conventions

- Source files: `src/` directory with logical subdirectories
- Headers: Corresponding `.h` files alongside `.c` files
- Platform-specific: `src/platform/{linux,windows}/`

## Testing

Test with:
- RPCEmu (RISC OS 3.x, 5.x emulator)
- Real RISC OS hardware if available
- Wireshark with UDP port filters on 32770, 32771, 49171

## Configuration File Format

The server uses an INI-style configuration file (`access.conf` or specified via `-c` flag):

```ini
[server]
# Logging: none, error, info, debug, protocol
log_level = info

# Broadcast interval in seconds (default: 30)
broadcast_interval = 30

# Enable Access+ authentication (port 32771)
access_plus = true

[share:Documents]
# Local path to share (required)
path = /home/user/documents

# Share attributes (optional, default: none)
# Options: protected, readonly, hidden, cdrom
attributes = readonly

# Password for protected shares (required if protected)
# password = secret

# Default filetype for extensionless files (hex, default: FFF)
default_filetype = FFF

[share:Public]
path = /srv/public
attributes = 

[share:Software]
path = /home/user/riscos-apps
attributes = readonly, hidden

[printer:LaserJet]
# Local directory for print spool (required)
path = /var/spool/riscos/laserjet

# Printer definition file to copy (required, ,fc6/PrntDefn format)
definition = /usr/share/riscos/printers/PostScript,fc6

# Human-readable description shown to clients
description = PostScript Level 2

# Poll interval for new print jobs in seconds (default: 5)
poll_interval = 5

# Command to execute for each print job
# %f = full path to spool file
command = lpr -P laserjet %f

[printer:PDFPrinter]
path = /var/spool/riscos/pdf
definition = /usr/share/riscos/printers/PostScript,fc6
description = PDF Generator
poll_interval = 3
command = ps2pdf %f /home/user/PDFs/$(basename %f .ps).pdf

[mimemap]
# Override default RISC OS filetype mappings
# Format: extension = hex_filetype
pdf = ADF
txt = FFF
bas = FFB
c = FFD
h = FFD
```

### Configuration Notes

- Section names are case-insensitive
- Share/printer names must be valid RISC OS filenames (10 chars max, no spaces)
- Multiple shares and printers can be defined
- The `[mimemap]` section extends the built-in extension-to-filetype mapping
- Paths use native OS format (forward slashes on Linux, either on Windows)
