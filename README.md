# ShareFS Server

**Author:** Andrew Timmins  
**License:** GPL-3.0-only

ShareFS Server is a C11 implementation of an Acorn ShareFS-compatible server for Linux and Windows. It allows modern computers to share files with RISC OS machines over a local network using the native ShareFS protocol.

> The git repository folder is still named `riscos-access-server` on GitHub; the Debian package is `sharefs-server`.

## Features

- **Full ShareFS Protocol** - Complete implementation including file operations, directory browsing, and attribute handling
- **Freeway Broadcasts** - Automatic share discovery by RISC OS clients (port 32770)
- **Access+ Authentication** - Password-protected shares (port 32771)
- **RISC OS Filetype Preservation** - Via `,xxx` suffixes or automatic MIME mapping
- **Admin GUI** - wxWidgets-based **ShareFS Admin** interface for easy configuration and server control
- **Cross-Platform** - Native Linux (amd64, arm64) and Windows (x64, arm64) builds

---

## Quick Start (Installation)

### Linux (Debian/Ubuntu)

1. Download the latest `.deb` release for your architecture (`*_amd64.deb` or `*_arm64.deb`).
2. Install it:
   ```bash
   sudo apt install ./sharefs-server_*.deb
   ```
3. **That's it!** The server starts automatically as a system service.
   - **Configure:** Run `sharefs-admin` (or edit `/etc/sharefs.conf`)
   - **Status:** `sudo systemctl status sharefs`

### Windows

1. Download the Windows zip archive (`sharefs-server_*.zip` for x64, or `sharefs-server_*-arm64.zip` for Windows on ARM) from the GitHub release assets.
2. Extract it to a folder.
3. Run `sharefs-server.exe` (Server) or `sharefs-admin.exe` (GUI).
   - **Note:** You likely need to allow the application through Windows Firewall (Ports 32770, 32771, 49171 UDP).

---

## Building

### Automated Build

We provide scripts that automate the entire build process:

```bash
# Clone the repository
git clone https://github.com/andrewtimmins/riscos-access-server.git
cd riscos-access-server

# Install build dependencies (Debian/Ubuntu)
./setup-build-env.sh

# Build for Linux (host architecture → releases/linux/amd64 or arm64/)
./build.sh
```

**Output:** Binaries are placed under `releases/linux/<arch>/` (e.g. `amd64` on a PC, `arm64` on a Pi).

Only the architecture you build is created — empty sibling folders are not made.

### Release layout

```
releases/
├── linux/
│   ├── amd64/          # x86_64 PCs
│   └── arm64/          # Raspberry Pi 4/5 (64-bit OS), ARM servers
└── windows/
    ├── x64/            # 64-bit Windows (+ NSIS installer when built with --zip)
    └── arm64/          # Windows on ARM (zip only)
```

**Supported ARM:** `aarch64` / `arm64` only (no 32-bit `armhf`).

### Build Options

```bash
./build.sh                         # Linux only, for host architecture
./build.sh --deb                   # Linux + .deb for host arch
./build.sh --arch arm64 --deb      # Linux arm64 (native on Pi)
./build.sh --cross-arm64           # Cross-compile Linux arm64 from x86_64 (server only)

# Windows (see notes below)
./build.sh --windows-only          # Windows x64 only — skips Linux rebuild
./build.sh --windows               # Linux + Windows x64 (server only)
./build.sh --windows-full          # Linux + Windows x64 + admin GUI
./build.sh --windows-only --zip    # Windows x64 zip (+ NSIS installer)
./build.sh --windows --zip         # Linux + Windows x64 + zip + NSIS installer
./build.sh --windows-only --windows-arch arm64   # Windows on ARM (zip only)
./build.sh --all-full              # Linux + Windows x64 + deb + zip + installer
./build.sh --clean                 # Remove build directories and releases
./build.sh --help                  # Full option list
```

**`--windows` vs `--windows-only`:** `--windows` and `--windows-full` always rebuild **Linux and Windows** (typical release build). Use **`--windows-only`** when you only want to refresh the Windows output under `releases/windows/<arch>/`.

**Setup helpers** (run once):

```bash
./setup-build-env.sh              # Native Linux build tools + wxWidgets
./setup-build-env.sh --windows    # + MinGW for Windows x64 cross-compile
./setup-build-env.sh --cross-arm64 # + aarch64-linux-gnu (cross server from x86 PC)
```

### Building for Raspberry Pi (arm64)

On a Pi 4/5 running **64-bit Raspberry Pi OS** (or Debian arm64):

```bash
./setup-build-env.sh
./build.sh --deb
```

Output: `releases/linux/arm64/` including `sharefs-server_*.deb` (`arm64` package).

To cross-compile the **server only** from an x86_64 PC:

```bash
./setup-build-env.sh --cross-arm64
./build.sh --cross-arm64
```

### Building all Linux architectures

There is no single `./build.sh` flag for both Linux arches. Use one of these approaches:

| Goal | How |
|------|-----|
| **Full amd64 + arm64 `.deb` packages** | Build on an x86_64 PC (`./build.sh --deb`) and on a Pi or ARM CI runner (`./build.sh --deb` — auto-detects arm64), then combine the `releases/linux/*/` folders |
| **Both from CI** | Download the `sharefs-linux-amd64` and `sharefs-linux-arm64` artifacts from GitHub Actions |
| **amd64 full + arm64 server from one x86 PC** | `./build.sh --deb && ./build.sh --cross-arm64` (arm64 cross build is server-only, no admin or `.deb`) |

```bash
# x86_64 PC — full amd64 release
./setup-build-env.sh
./build.sh --deb

# Raspberry Pi 4/5 (64-bit OS) — full arm64 release
./setup-build-env.sh
./build.sh --deb

# Optional: arm64 server binary only, from x86 PC
./setup-build-env.sh --cross-arm64
./build.sh --cross-arm64
```

### Building for Windows

Cross-compile for Windows x64 from Linux:

```bash
./setup-build-env.sh --windows
./build.sh --windows-only                # Server only (~600KB)
./build.sh --windows-wxwidgets           # One-time wxWidgets build (x64)
./build.sh --windows-only --windows-full # Server + Admin GUI (~13MB)
./build.sh --windows-only --zip          # + zip and NSIS installer
```

For a full release including Linux:

```bash
./build.sh --windows --zip    # Linux + Windows x64 + zip + NSIS
./build.sh --all-full         # Linux + Windows x64 + deb + zip + NSIS
```

**Output:** `releases/windows/x64/` (installer: `sharefs-server_*-setup.exe`, zip: `sharefs-server_*.zip`)

**Windows on ARM** (`releases/windows/arm64/`):

```bash
./build.sh --windows-wxwidgets --windows-arch arm64   # one-time, needs MinGW aarch64 toolchain
./build.sh --windows-only --windows-arch arm64 --zip
```

MinGW for aarch64 is **not in apt** — you need a separate toolchain ([mingw-woarm64-build](https://github.com/Windows-on-ARM-Experiments/mingw-woarm64-build)) or a native build on Windows ARM hardware. ARM64 releases are zip-only (no NSIS installer).

### Continuous integration

GitHub Actions (`.github/workflows/build.yml`) runs on every push and pull request:

| Job | Output |
|-----|--------|
| `linux-amd64` | `releases/linux/amd64/` + `.deb`, tests |
| `linux-arm64` | `releases/linux/arm64/` + `.deb`, tests (native ARM runner) |
| `linux-arm64-cross` | Smoke test: cross-compiled arm64 server |
| `windows-x64` | `releases/windows/x64/` zip (+ NSIS on tags) |

**Publishing a release:** push a version tag (e.g. `v0.1.1`). The workflow builds full Linux amd64/arm64 packages, a complete Windows x64 zip with admin GUI (wxWidgets cached after the first run), NSIS installer, and attaches everything to a GitHub Release automatically.

```bash
git tag v0.1.1
git push origin v0.1.1
```

Windows on ARM is not built in CI (no MinGW aarch64 on hosted runners).

### Manual Build (Advanced)

If you prefer manual control:

```bash
# Linux
cmake -S . -B build
cmake --build build -j$(nproc)

# Create deb package
cd build && cpack -G DEB

# Windows cross-compile (server only, x64)
cmake -S . -B build-win \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
  -DSFS_BUILD_ADMIN=OFF
cmake --build build-win -j$(nproc)

# Linux arm64 cross-compile (server only, from amd64 host)
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64-gnu.cmake \
  -DSFS_BUILD_ADMIN=OFF
cmake --build build -j$(nproc)
```

---

## Running

**Note for WiFi users:** WiFi usually requires binding to a specific IP. Run `ipconfig` (Windows) or `ip addr` (Linux) to find your adapter's IP address and add it to the `bind_ip` setting in `sharefs.conf` (or use the Admin GUI).

### Linux (Debian/Ubuntu Package)

The server runs automatically as a system service.

```bash
# Check status
sudo systemctl status sharefs

# Start/Stop/Restart
sudo systemctl start sharefs
sudo systemctl stop sharefs
sudo systemctl restart sharefs

# Run Admin GUI (Installed to system path)
sharefs-admin
```
Configuration is at `/etc/sharefs.conf`.

Non-root management (Debian/Ubuntu package):
- Add your user to the `sharefs-admin` group: `sudo usermod -aG sharefs-admin $USER` then re-login.
- `/etc/sharefs.conf` is owned by `root:sharefs-admin` with mode `664`, so group members can edit it without sudo.
- A polkit rule allows the `sharefs-admin` group to start/stop/restart `sharefs.service` without a password (e.g., `systemctl restart sharefs`).

### Windows (Installer)

1. Run the Windows installer (`sharefs-server_*.exe`).
2. Everything is installed to `C:\ShareFS`:
   - Binaries: `sharefs-server.exe`, `sharefs-admin.exe`, `sharefs-service.exe`
   - Config: `sharefs.conf`
   - Shares: `C:\ShareFS\Shares\Public` (writeable for local users out of the box)
3. Firewall rules for UDP 32770, 32771, 49171 are added during install.
4. Start/stop/restart the server via the Admin GUI (controls the Windows service) or run `sharefs-service.exe start|stop`.

### Windows (Zip Archive)

Extract the zip archive anywhere (no installer, no firewall rules configured automatically).

- **Run Server:** Double-click `sharefs-server.exe` (or run from CMD: `sharefs-server.exe sharefs.conf`).
- **Run Admin GUI:** Double-click `sharefs-admin.exe` (if included).

> **Firewall Warning:** Ensure Windows Firewall allows UDP ports 32770, 32771, and 49171.

#### Windows Service Installation

For automatic startup and running in the background, you can install the server as a Windows service:

1. Open PowerShell or Command Prompt **as Administrator**
2. Navigate to the installation directory
3. Run: `sharefs-service.exe install`
4. The service is configured to start automatically on boot
5. To start immediately: `sharefs-service.exe start`
   - Or use the Services console (`services.msc`)

**Configuration file location**: `C:\ShareFS\sharefs.conf`  
(Falls back to `./sharefs.conf` in the executable directory if the above does not exist)

**Managing the service:**
- **Start**: `sharefs-service.exe start` or use Services console
- **Stop**: `sharefs-service.exe stop`
- **Check status**: `sc query ShareFSServer`
- **Uninstall**: `sharefs-service.exe stop` (if running), then `sharefs-service.exe uninstall`

**Admin GUI integration**: The Admin GUI automatically detects if the Windows service is installed and provides start/stop/restart controls for it.

### Using the Admin GUI

The **ShareFS Admin** GUI allows easy configuration and control:

**Features:**
- **Server Tab** - Configure log level, broadcast interval, and Access+ authentication
- **Shares Tab** - Add, edit, and remove file shares
- **Printers Tab** - Configure network printer shares
- **MIME Map Tab** - Map file extensions to RISC OS filetypes
- **Control Tab** - Start/stop/restart server with live log viewer

### Manual / Development Build

If running directly from the build output (e.g., for testing):

```bash
# Linux (amd64 PC)
./releases/linux/amd64/sharefs-server releases/linux/amd64/sharefs.conf
./releases/linux/amd64/sharefs-admin

# Linux (Raspberry Pi / arm64)
./releases/linux/arm64/sharefs-server releases/linux/arm64/sharefs.conf
./releases/linux/arm64/sharefs-admin

# Windows (x64)
releases/windows/x64/sharefs-server.exe releases/windows/x64/sharefs.conf
```

---

## Configuration

The server is configured via `sharefs.conf`. ShareFS Admin is the easiest way to edit this file, but you can also edit it manually:

```ini
# ShareFS Server Configuration

[server]
log_level = info
broadcast_interval = 3

access_plus = true
bind_ip = 192.168.1.100  # Optional: Bind to specific interface

[share:Public]
path = /home/user/public

[share:Documents]
path = /home/user/documents
attributes = protected
password = secret123
default_filetype = FFF

[share:CD-ROM]
path = /media/cdrom
attributes = cdrom,readonly

[mimemap]
txt = FFF
pdf = ADF
html = FAF
jpg = C85
png = B60
```

### Server Settings

| Setting             | Description                                             | Default         |
|---------------------|---------------------------------------------------------|-----------------|
| `log_level`         | `none`, `error`, `info`, `debug`, or `protocol`       | `info`          |
| `broadcast_interval`| Seconds between Freeway broadcasts (0 to disable)       | `3`             |
| `access_plus`       | Enable Access+ authentication support                   | `true`          |
| `bind_ip`           | IP address to bind to (omit for all interfaces)       | all interfaces  |

### Share Attributes

| Attribute           | Description                                                               |
|---------------------|---------------------------------------------------------------------------|
| `protected`         | Requires Access+ authentication with password                             |
| `readonly`          | Read-only access (no writes allowed)                                      |
| `hidden`            | Hidden from RISC OS *Free browser                                         |
| `cdrom`             | Treat as CD-ROM (implies readonly)                                        |

### MIME Mappings

Map file extensions to RISC OS filetypes (3-character hex codes):

| Extension | Filetype | Description                                                              |
|-----------|----------|--------------------------------------------------------------------------|
| `txt`     | `FFF`    | Text                                                                     |
| `html`    | `FAF`    | HTML                                                                     |
| `pdf`     | `ADF`    | PDF                                                                      |
| `jpg`     | `C85`    | JPEG Image                                                               |
| `png`     | `B60`    | PNG Image                                                                |
| `zip`     | `A91`    | Archive                                                                  |

The Admin GUI includes common default mappings when creating a new configuration.

For protocol-level detail, see [docs/protocol.md](docs/protocol.md).

---

## Troubleshooting

### Server not visible to RISC OS clients

1. Ensure the server and RISC OS machine are on the same network/subnet
2. Check firewall allows UDP ports 32770, 32771, and 49171
3. On Windows WiFi, make sure you configured `bind_ip` in `sharefs.conf` with your IP address

### Permission denied errors

Ensure the server has read/write access to the share paths configured.

### Log file location / permission denied

By default on Linux the server writes to `/var/log/sharefs/sharefs.log`. If that path is not writable (e.g., running unprivileged or without the service-created directory), the server falls back to `/tmp/sharefs.log` and prints a warning. On Windows, logging uses `C:\ShareFS\sharefs.log` and falls back to `./sharefs.log` when the primary path is unavailable.

### Admin GUI won't start

Install wxWidgets development packages before building (see **Building** above). On Debian/Ubuntu: `libwxgtk3.2-dev`.

---

## License

This project is licensed under the GNU General Public License v3.0. See LICENSE file for details.

Copyright © Andrew Timmins, 2025.
