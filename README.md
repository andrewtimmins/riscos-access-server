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
```sh
cmake -S . -B build
cmake --build build
```

On Windows/MinGW ensure `ws2_32` is available; on Linux pthreads are linked.

## Run
```sh
./build/access access.conf
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
