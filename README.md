# RISC OS Access/ShareFS Server

Author: Andrew Timmins  
License: GPL-3.0-only

A C11 implementation of an Acorn Access/ShareFS-compatible server for Linux and Windows (MinGW). It serves files to RISC OS clients over UDP (ports 32770/32771/49171), with minimal dependencies (standard C, POSIX sockets or Winsock2, pthreads on Linux).

## Features
- Full ShareFS protocol: RFIND, ROPENIN, ROPENUP, ROPENDIR, RCREATE, RCREATEDIR, RDELETE, RACCESS, RFREESPACE, RRENAME, RCLOSE, RREAD, RWRITE, RREADDIR, RENSURE, RSETLENGTH, RSETINFO, RGETSEQPTR, RSETSEQPTR, RVERSION
- Freeway broadcasts for share discovery (port 32770)
- Access+ authentication for protected shares (port 32771)
- RISC OS filetype preservation via `,xxx` suffixes
- Automatic parent directory creation
- Dynamic handle allocation (no artificial limits)
- Configurable logging levels and extension-to-filetype mapping

## Build

### Linux (Native)
```sh
# Configure
cmake -S . -B build

# Build
cmake --build build

# Or with parallel jobs for faster builds
cmake --build build -j$(nproc)
```

### Windows (Cross-Compilation from Linux)

**Prerequisites:**
Install MinGW-w64 cross-compiler:
```sh
# Ubuntu/Debian
sudo apt-get install mingw-w64

# Fedora/RHEL
sudo dnf install mingw64-gcc
```

**Build:**
```sh
# Clean previous build
rm -rf build

# Configure with MinGW toolchain
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake

# Build
cmake --build build
```

The Windows executable will be at `build/src/access.exe`.

## Run

### Linux
```sh
./build/src/access access.conf
```

### Windows

**Important for WiFi users:** Windows WiFi adapters require binding to a specific IP address. First, find your WiFi adapter's IP:

```cmd
ipconfig
```

Look for your WiFi adapter's IPv4 address (e.g., `192.168.1.100`), then run:

```cmd
access.exe -b 192.168.1.100 access.conf
```

For wired Ethernet on Windows, you can usually omit the `-b` option:

```cmd
access.exe access.conf
```

## Configuration (access.conf)
```ini
[server]
log_level = info
broadcast_interval = 30
access_plus = true

[share:Public]
path = /home/user/public

[share:Documents]
path = /home/user/documents
attributes = protected
password = secret
default_filetype = FFF

[mimemap]
pdf = ADF
txt = FFF
bas = FFB
```

### Share Attributes
- `protected` - Requires Access+ authentication
- `readonly` - Read-only access
- `hidden` - Hidden from browser
- `cdrom` - CD-ROM share
