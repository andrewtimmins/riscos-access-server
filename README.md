# ShareFS Server

**Author:** Andy Timmins  
**License:** GPL-3.0-only

ShareFS Server is a C11 implementation of an Acorn ShareFS-compatible server for Linux, macOS and Windows. It allows modern computers to share files with RISC OS machines over a local network using the native ShareFS protocol.

## Features

- **Full ShareFS Protocol** - Complete implementation including file operations, directory browsing, and attribute handling
- **Freeway Broadcasts** - Automatic share discovery by RISC OS clients (port 32770)
- **Access+ Authentication** - Password-protected shares (port 32771)
- **RISC OS Filetype Preservation** - Via `,xxx` suffixes or automatic MIME mapping
- **Admin GUI** - wxWidgets-based **ShareFS Admin** interface for easy configuration and server control, which can host the server itself
- **Cross-Platform** - Native Linux (amd64, arm64), macOS (universal: Apple Silicon and Intel) and Windows (x64, arm64) builds

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

### macOS (Apple Silicon and Intel)

1. Download `sharefs-server_*-macos-universal.zip` from the release assets. One
   download covers both Apple Silicon and Intel; the binaries are universal.
2. Unpack it and drag `sharefs-admin.app` to Applications, or run
   `./sharefs-server` from the unpacked folder.
3. The first time it runs, macOS asks for **Local Network** access. Allow it,
   or RISC OS machines will not see your shares.
4. The app is signed ad-hoc rather than notarised, so the first launch needs
   right-click then **Open**, or `xattr -dr com.apple.quarantine sharefs-admin.app`.

Configuration lives at `/opt/homebrew/etc/sharefs.conf`, `/usr/local/etc/sharefs.conf`
or `/etc/sharefs.conf`, whichever exists; the log goes to
`~/Library/Logs/ShareFS/sharefs.log` unless `log_file` says otherwise.

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
git clone https://github.com/andrewtimmins/sharefs-server.git
cd sharefs-server

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

### Building for macOS

`build-macos.sh` builds a single-architecture slice, bundles the libraries the
admin GUI needs into the `.app` so it runs without Homebrew, and can fuse two
slices into a universal build.

```bash
brew install cmake wxwidgets
./build-macos.sh                     # slice for this machine's architecture
./build-macos.sh --arch arm64 --zip  # slice plus a zip
```

For a universal release both slices are needed. The server has no
dependencies, but the admin GUI links wxWidgets, which Homebrew ships
per-architecture only, so each slice must be built against a Homebrew matching
its own architecture:

```bash
./build-macos.sh --arch arm64        # against /opt/homebrew
./build-macos.sh --arch x86_64       # against /usr/local (Intel Homebrew)
./build-macos.sh --fuse --zip        # lipo both into releases/macos
```

Both Homebrews must be on the same formula versions. `lipo` fuses two files
into one, and a library from two different upstream releases is not one
library; the script stops with a clear error if the slices disagree.

CI does this on every push, cross-compiling the x86_64 slice on an Apple
Silicon runner against a Rosetta Homebrew, because free Intel runners are
effectively unavailable.

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
| `macos-arm64` | Apple Silicon slice, tests |
| `macos-x86_64` | Intel slice, cross-compiled under Rosetta |
| `macos-universal` | Fuses both slices, verifies, produces the universal zip |

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

The **ShareFS Admin** GUI allows easy configuration and control.

**How the two binaries relate.** `sharefs-server` is the headless server: plain
C11 with no GUI dependency, and it is what the systemd unit and the Windows
service run. `sharefs-admin` links the same server core, so when you press
**Start** in the GUI the server runs on a worker thread *inside the admin
process* rather than as a separate child. That means the status it shows is a
fact rather than a probe, and the log pane is fed straight from the server.

If a system service is already running the server, the GUI detects that and
drives the service instead, leaving its own in-process host idle.

`sharefs-server` accepts `--no-ui` for symmetry with the GUI; the server binary
is always headless, so the flag is a no-op and exists so scripts can pass it
freely. `--help` lists the options.

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
| `log_file`          | Where to write the log (omit for the platform default)  | see below       |
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

On **Linux**, the packaged service is hardened and only grants write access to
`/var/log/sharefs` and `/srv/sharefs`. To share from elsewhere, add the path:

```bash
sudo systemctl edit sharefs
# [Service]
# ReadWritePaths=/mnt/archive
```

Home directories are read-only by default; override `ProtectHome=false` the
same way if you share from `/home`.

On **macOS**, folders under `~/Documents`, `~/Desktop` and `~/Downloads` are
protected by the system privacy controls, and a background server cannot
prompt for access. Either share from somewhere else, or grant the binary Full
Disk Access in System Settings > Privacy & Security. macOS will also ask for
Local Network permission the first time the server runs; without it, RISC OS
machines will not see the shares.

### Symlinks inside a share

Paths are resolved and checked against the share root, so a symbolic link
pointing outside the share is refused rather than followed. Keep the content
you want served inside the share directory itself.

### Log file location / permission denied

Set `log_file` in the `[server]` section to put the log wherever you want. If it is not set, the default depends on the platform:

| Platform | Preferred | Fallback when not writable |
|----------|-----------|----------------------------|
| Linux    | `/var/log/sharefs/sharefs.log` | `/tmp/sharefs.log` |
| macOS    | `/var/log/sharefs/sharefs.log` | `~/Library/Logs/ShareFS/sharefs.log` |
| Windows  | `%ProgramData%\ShareFS\sharefs.log` | the folder holding the executable |

The server logs its version and the log file it opened at startup, so `sharefs.log` always records which build produced it.

### Admin GUI won't start

Install wxWidgets development packages before building (see **Building** above). On Debian/Ubuntu: `libwxgtk3.2-dev`.

---

## License

This project is licensed under the GNU General Public License v3.0. See LICENSE file for details.

Copyright © Andy Timmins, 2025.
