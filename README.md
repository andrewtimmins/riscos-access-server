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
- **Cross-Platform** - Native Linux and Windows builds

---

## Quick Start (Installation)

### Linux (Debian/Ubuntu)

1. Download the latest `.deb` release.
2. Install it:
   ```bash
   sudo apt install ./sharefs-server_*.deb
   ```
3. **That's it!** The server starts automatically as a system service.
   - **Configure:** Run `sharefs-admin` (or edit `/etc/sharefs.conf`)
   - **Status:** `sudo systemctl status sharefs`

### Windows

1. Download the Windows zip archive (`sharefs-server_*.zip`).
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

# Install build dependencies (auto-detects your distro)
./setup-build-env.sh

# Build for Linux
./build.sh
```

**Output:** Binaries are placed in `releases/linux/`

### Build Options

```bash
./build.sh                    # Linux only
./build.sh --deb              # Linux + create .deb package
./build.sh --windows          # Linux + Windows (server only)
./build.sh --windows-full     # Linux + Windows (server + admin GUI)
./build.sh --zip              # Create .zip archive (Windows only)
./build.sh --all              # Build Linux + Win Server + .deb
./build.sh --all-full         # Build everything (incl. Win Admin GUI + zip)
./build.sh --clean            # Remove all build directories
```

### Building for Windows

To cross-compile for Windows from Linux:

```bash
# Install Windows cross-compilation tools
./setup-build-env.sh --windows

# Build Windows executables
./build.sh --windows          # Server only (~600KB)

# Or with admin GUI (requires wxWidgets, ~10-15 min one-time setup)
./build.sh --windows-wxwidgets  # One-time wxWidgets build
./build.sh --windows-full       # Server + Admin GUI (~13MB)
```

**Output:** Binaries are placed in `releases/windows/`

### Manual Build (Advanced)

If you prefer manual control:

```bash
# Linux
cmake -S . -B build
cmake --build build -j$(nproc)

# Create deb package
cd build && cpack -G DEB

# Windows cross-compile (server only)
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake -DSFS_BUILD_ADMIN=OFF
cmake --build build-win -j$(nproc)
```

---

## Running

**Note for WiFi users:** WiFi usually requires binding to a specific IP. Run `ipconfig` (Windows) or `ip addr` (Linux) to find your adapter's IP address and add it to the `bind_ip` setting in `sharefs.conf` (or usage the Admin GUI).

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
# Linux
./releases/linux/sharefs-server releases/linux/sharefs.conf
./releases/linux/sharefs-admin

# Windows
releases/windows/sharefs-server.exe releases/windows/sharefs.conf
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
| `log_level`         | Logging verbosity: `debug`, `info`, `warn`, `error`     | `info`          |
| `broadcast_interval`| Seconds between Freeway broadcasts (0 to disable)       | `3`             |
| `access_plus`       | Enable Access+ authentication support                   | `false`         |
| `bind_ip`           | IP address to bind to (required for Windows WiFi)       | `0.0.0.0` (all) |

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

Make sure wxWidgets is installed (see Prerequisites section for your distribution).

---

## License

This project is licensed under the GNU General Public License v3.0. See LICENSE file for details.

Copyright © Andrew Timmins, 2025.
