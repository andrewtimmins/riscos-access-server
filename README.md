# RISC OS Access/ShareFS Server

**Author:** Andrew Timmins  
**License:** GPL-3.0-only

A C11 implementation of an Acorn Access/ShareFS-compatible server for Linux and Windows. This allows modern computers to share files with RISC OS machines over a local network using the native ShareFS protocol.

## Features

- **Full ShareFS Protocol** - Complete implementation including file operations, directory browsing, and attribute handling
- **Freeway Broadcasts** - Automatic share discovery by RISC OS clients (port 32770)
- **Access+ Authentication** - Password-protected shares (port 32771)
- **RISC OS Filetype Preservation** - Via `,xxx` suffixes or automatic MIME mapping
- **Admin GUI** - wxWidgets-based graphical interface for easy configuration and server control
- **Cross-Platform** - Native Linux and Windows builds

---

## Quick Start

The easiest way to use the Access/ShareFS Server is through the **Admin GUI**. It provides:

- Visual configuration of shares, printers, and MIME mappings
- One-click server start/stop/restart
- Real-time server log viewing
- No need to manually edit configuration files

### Linux Users

```bash
# Build (see detailed instructions below)
cmake -S . -B build && cmake --build build -j$(nproc)

# Run the Admin GUI
./build/admin/access-admin
```

The Admin GUI will automatically load `access.conf` from the current directory if it exists.

---

## Installation

### Prerequisites by Distribution

<details>
<summary><b>Ubuntu / Debian / Linux Mint</b></summary>

```bash
# Build tools and dependencies
sudo apt update
sudo apt install build-essential cmake

# wxWidgets for the Admin GUI
sudo apt install libwxgtk3.0-gtk3-dev

# For Windows cross-compilation (optional)
sudo apt install mingw-w64
```

</details>

<details>
<summary><b>Fedora / RHEL / CentOS / Rocky Linux</b></summary>

```bash
# Build tools and dependencies
sudo dnf install gcc gcc-c++ cmake make

# wxWidgets for the Admin GUI
sudo dnf install wxGTK3-devel

# For Windows cross-compilation (optional)
sudo dnf install mingw64-gcc mingw64-gcc-c++
```

</details>

<details>
<summary><b>Arch Linux / Manjaro</b></summary>

```bash
# Build tools and dependencies
sudo pacman -S base-devel cmake

# wxWidgets for the Admin GUI
sudo pacman -S wxwidgets-gtk3

# For Windows cross-compilation (optional)
sudo pacman -S mingw-w64-gcc
```

</details>

<details>
<summary><b>openSUSE</b></summary>

```bash
# Build tools and dependencies
sudo zypper install gcc gcc-c++ cmake make

# wxWidgets for the Admin GUI
sudo zypper install wxWidgets-3_2-devel

# For Windows cross-compilation (optional)
sudo zypper install mingw64-cross-gcc mingw64-cross-gcc-c++
```

</details>

---

## Building

### Linux (Native Build)

```bash
# Clone the repository
git clone https://github.com/andrewtimmins/riscos-access-server.git
cd riscos-access-server

# Configure
cmake -S . -B build

# Build (use all CPU cores for faster compilation)
cmake --build build -j$(nproc)
```

This produces:
- `build/src/access` - The server executable
- `build/admin/access-admin` - The Admin GUI

### Building Without the Admin GUI

If you don't need the graphical interface (e.g., headless servers):

```bash
cmake -S . -B build -DRAS_BUILD_ADMIN=OFF
cmake --build build -j$(nproc)
```

---

## Windows Cross-Compilation

You can build Windows executables from your Linux machine.

### Step 1: Build Server Only (Quick)

The server has no dependencies and cross-compiles immediately:

```bash
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake -DRAS_BUILD_ADMIN=OFF
cmake --build build-win -j$(nproc)
```

This produces `build-win/src/access.exe`.

### Step 2: Build Server + Admin GUI (Requires wxWidgets)

To cross-compile the Admin GUI, you need to build wxWidgets for MinGW first:

```bash
# Download wxWidgets
mkdir -p ~/wxWidgets-mingw && cd ~/wxWidgets-mingw
wget https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.4/wxWidgets-3.2.4.tar.bz2
tar -xjf wxWidgets-3.2.4.tar.bz2

# Configure and build wxWidgets for MinGW (takes ~10-15 minutes)
mkdir build-mingw && cd build-mingw
../wxWidgets-3.2.4/configure \
    --host=x86_64-w64-mingw32 \
    --build=x86_64-linux-gnu \
    --prefix=$HOME/wxWidgets-mingw/install \
    --disable-shared \
    --enable-unicode \
    --disable-mediactrl \
    --disable-webview
make -j$(nproc)
make install

# Return to the project and build
cd /path/to/riscos-access-server
cmake -S . -B build-win \
    -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake \
    -DwxWidgets_CONFIG_EXECUTABLE=$HOME/wxWidgets-mingw/install/bin/wx-config

cmake --build build-win -j$(nproc)
```

This produces:
- `build-win/src/access.exe` - Windows server (~600KB)
- `build-win/admin/access-admin.exe` - Windows Admin GUI (~13MB, statically linked)

Both executables run on Windows without any additional DLLs.

---

## Running

### Using the Admin GUI (Recommended)

The Admin GUI is the recommended way to configure and run the server:

```bash
# Linux
./build/admin/access-admin

# Windows
access-admin.exe
```

**Features:**
- **Server Tab** - Configure log level, broadcast interval, and Access+ authentication
- **Shares Tab** - Add, edit, and remove file shares with visual directory browser
- **Printers Tab** - Configure network printer shares
- **MIME Map Tab** - Map file extensions to RISC OS filetypes
- **Control Tab** - Start/stop/restart server with live log viewer

Click **"Apply & Restart"** to save changes and restart the server automatically.

### Running the Server Directly

You can also run the server from the command line:

```bash
# Linux
./build/src/access access.conf

# Windows (wired network)
access.exe access.conf

# Windows (WiFi - must bind to your IP address)
access.exe -b 192.168.1.100 access.conf
```

**Note for Windows WiFi users:** Windows requires binding to a specific IP when using WiFi. Run `ipconfig` to find your adapter's IP address.

---

## Configuration

The server is configured via `access.conf`. The Admin GUI is the easiest way to edit this file, but you can also edit it manually:

```ini
# Access/ShareFS Server Configuration

[server]
log_level = info
broadcast_interval = 60
access_plus = true

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

| Setting | Description | Default |
|---------|-------------|---------|
| `log_level` | Logging verbosity: `debug`, `info`, `warn`, `error` | `info` |
| `broadcast_interval` | Seconds between Freeway broadcasts (0 to disable) | `60` |
| `access_plus` | Enable Access+ authentication support | `false` |

### Share Attributes

| Attribute | Description |
|-----------|-------------|
| `protected` | Requires Access+ authentication with password |
| `readonly` | Read-only access (no writes allowed) |
| `hidden` | Hidden from RISC OS *Free browser |
| `cdrom` | Treat as CD-ROM (implies readonly) |

### MIME Mappings

Map file extensions to RISC OS filetypes (3-character hex codes):

| Extension | Filetype | Description |
|-----------|----------|-------------|
| `txt` | `FFF` | Text |
| `html` | `FAF` | HTML |
| `pdf` | `ADF` | PDF |
| `jpg` | `C85` | JPEG Image |
| `png` | `B60` | PNG Image |
| `zip` | `A91` | Archive |

The Admin GUI includes common default mappings when creating a new configuration.

---

## Troubleshooting

### Server not visible to RISC OS clients

1. Ensure the server and RISC OS machine are on the same network/subnet
2. Check firewall allows UDP ports 32770, 32771, and 49171
3. On Windows WiFi, make sure you specified `-b <your-ip-address>`

### Permission denied errors

Ensure the server has read/write access to the share paths configured.

### Admin GUI won't start

Make sure wxWidgets is installed (see Prerequisites section for your distribution).

---

## License

This project is licensed under the GNU General Public License v3.0. See LICENSE file for details.

Copyright © Andrew Timmins, 2025.
