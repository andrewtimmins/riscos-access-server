# ShareFS

**Author:** Andy Timmins  
**License:** GPL-3.0-only

ShareFS is a C11 implementation of an Acorn ShareFS-compatible server for Linux, macOS and Windows. It allows modern computers to share files with RISC OS machines over a local network using the native ShareFS protocol.

## Features

- **Full ShareFS Protocol** - Complete implementation including file operations, directory browsing, and attribute handling
- **Freeway Broadcasts** - Automatic share discovery by RISC OS clients (port 32770)
- **Access+ Authentication** - Password-protected shares (port 32771)
- **RISC OS Filetype Preservation** - Via `,xxx` suffixes or automatic MIME mapping
- **One program** - `sharefs` is the window, the server and the background service, chosen by what you ask it to do
- **Cross-Platform** - Native Linux (amd64, arm64), macOS (universal: Apple Silicon and Intel) and Windows (x64, arm64) builds

## One program, several modes

There is a single executable, `sharefs` (`ShareFS.app` on macOS, `sharefs.exe`
on Windows). Run it with no arguments and it opens its window. Give it a
command and it does that instead:

| Command | What it does |
|---------|--------------|
| `sharefs` | Open the window |
| `sharefs serve` | Share in the foreground until stopped |
| `sharefs status` | Where the configuration is, what is running |
| `sharefs config path` | Print the configuration file in use |
| `sharefs config search` | Print every location searched, marking the one in use |
| `sharefs config create` | Write a starter configuration |
| `sharefs autostart on\|off` | Keep sharing when nothing is open |
| `sharefs service install\|start\|stop\|uninstall` | The Windows service |

Versions up to 0.1.7 shipped three executables: `sharefs-server`,
`sharefs-service` and `sharefs-admin`. On Linux and macOS the old names are
installed as links to `sharefs` and keep working; `sharefs-server` still serves
headlessly. On Windows the installer removes them.

---

## Quick Start (Installation)

### Linux (Debian/Ubuntu)

1. Download the latest `.deb` release for your architecture (`*_amd64.deb` or `*_arm64.deb`).
2. Install it:
   ```bash
   sudo apt install ./sharefs-server_*.deb
   ```
3. **That's it!** Sharing starts automatically as a system service, and
   **ShareFS** appears in your applications menu.
   - The installer adds you to the `sharefs-admin` group so the window can save
     settings and start and stop the service. **Log out and back in once**, or
     that group membership is not yet in effect.
   - **Status:** `sharefs status`, or `sudo systemctl status sharefs`

### macOS (Apple Silicon and Intel)

1. Download `sharefs-server_*-macos-universal.dmg` from the release assets. One
   download covers both Apple Silicon and Intel.
2. Open it and drag **ShareFS** to Applications. That is the whole product: the
   window, the server and the command line are one app.
3. The first time it runs it asks which folder to share, then starts sharing.
4. macOS asks for **Local Network** access. Allow it, or RISC OS machines will
   not see your shares.
5. The app is signed ad-hoc rather than notarised, so the first launch needs
   right-click then **Open**, or
   `xattr -dr com.apple.quarantine /Applications/ShareFS.app`.

Settings are kept in `~/Library/Application Support/ShareFS/sharefs.conf`, so
nothing needs `sudo`; **File → Reveal Configuration File** shows it. The log
goes to `~/Library/Logs/ShareFS/sharefs.log` unless `log_file` says otherwise.

### Windows

1. Download the installer (`sharefs-server_*-setup.exe`), or the zip archive
   (`sharefs-server_*.zip` for x64, `sharefs-server_*-arm64.zip` for Windows on
   ARM) if you would rather not install anything.
2. The installer adds firewall rules, installs the service and puts **ShareFS**
   in the Start Menu. From the zip, run `sharefs.exe`.
   - **From the zip:** you need to allow ShareFS through Windows Firewall
     yourself (UDP 32770, 32771 and 49171), or run
     `configure-firewall-windows.bat` as an administrator.

`sharefs.exe` is both the window and the command line. Double-click it for the
window; from a command prompt, `sharefs status` and the rest work as listed
above.

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

`build-macos.sh` builds a single-architecture slice, bundles the libraries
`ShareFS.app` needs so it runs without Homebrew, and can fuse two slices into a
universal build. What ships is `ShareFS.app` and nothing else: the command line
is inside it at `Contents/MacOS/ShareFS`.

```bash
brew install cmake wxwidgets
./build-macos.sh                     # slice for this machine's architecture
./build-macos.sh --arch arm64 --dmg  # slice plus a disk image
```

For a universal release both slices are needed. wxWidgets is Homebrew-only and
per-architecture, so each slice must be built against a Homebrew matching its
own architecture:

```bash
./build-macos.sh --arch arm64          # against /opt/homebrew
./build-macos.sh --arch x86_64         # against /usr/local (Intel Homebrew)
./build-macos.sh --fuse --zip --dmg    # lipo both into releases/macos
```

`--dmg` produces the disk image users get, with the app and a shortcut to
Applications. `--zip` produces the same tree as an archive, for scripts.

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
./build.sh --windows-only                # Without the window (~600KB)
./build.sh --windows-wxwidgets           # One-time wxWidgets build (x64)
./build.sh --windows-only --windows-full # With the window (~13MB)
./build.sh --windows-only --zip          # + zip and NSIS installer
```

For a full release including Linux:

```bash
./build.sh --windows --zip    # Linux + Windows x64 + zip + NSIS
./build.sh --all-full         # Linux + Windows x64 + deb + zip + NSIS
```

**Output:** `releases/windows/x64/`, holding `sharefs.exe` (installer:
`sharefs-server_*-setup.exe`, zip: `sharefs-server_*.zip`). Release archives
keep the `sharefs-server_` prefix, because that is the name of this project and
of the Debian package; the binary inside is `sharefs`.

**Windows on ARM** (`releases/windows/arm64/`):

```bash
./build.sh --windows-wxwidgets --windows-arch arm64   # one-time, needs MinGW aarch64 toolchain
./build.sh --windows-only --windows-arch arm64 --zip
```

MinGW for aarch64 is **not in apt** — you need a separate toolchain ([mingw-woarm64-build](https://github.com/Windows-on-ARM-Experiments/mingw-woarm64-build)) or a native build on Windows ARM hardware. ARM64 releases are zip-only (no NSIS installer).

**Windows on ARM ships the server without the window.** wxWidgets cannot be
cross-compiled for aarch64 with llvm-mingw: it uses libc++, which no longer
provides the `char_traits` primary template, and `wxUString` is declared as
`std::basic_string<wxChar32>`, so wxWidgets' own base library fails to compile.
Version 3.3 declares it the same way, so there is no version to move to. The
ARM64 `sharefs.exe` therefore has no window; it shares, and the command line
(`sharefs serve`, `status`, `config`, `service`) is identical to every other
platform. Windows on ARM also runs x64 binaries under emulation, so the x64 zip
or installer is how to get the window on those machines today.

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
| `macos-universal` | Fuses both slices, verifies, produces the universal zip and .dmg |

**Rehearsing a release:** an ordinary push builds the Windows binary without
the window, because wxWidgets for MinGW has to be built from source first. Run
the workflow by hand with **full** ticked (Actions → Build → Run workflow) to
build exactly what a release ships, without publishing anything. That also
leaves the wxWidgets cache warm, so the tag build afterwards is quick. Worth
doing before any release that touched the Windows build.

**Publishing a release:** push a version tag (e.g. `v0.1.8`). The workflow
builds full Linux amd64/arm64 packages, a complete Windows zip with the window
compiled in, the NSIS installer, the macOS universal .dmg and .zip, and
attaches everything to a GitHub Release automatically.

```bash
git tag v0.1.8
git push origin v0.1.8
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

**Note for WiFi users:** WiFi usually requires binding to a specific IP. Run `ipconfig` (Windows) or `ip addr` (Linux) to find your adapter's IP address and add it to the `bind_ip` setting in `sharefs.conf` (or the **Server** tab in the window).

### Keeping it running

Whether sharing survives the window closing is one tick box on the **Sharing**
tab: **Keep sharing when this window is closed**. Ticking it sets up whatever
this platform uses for that, and starts it:

| Platform | What the tick box sets up |
|----------|---------------------------|
| Linux | The `sharefs` systemd unit |
| macOS | A launchd agent in `~/Library/LaunchAgents` |
| Windows | The ShareFS service (needs administrator rights) |

The same switch from a terminal is `sharefs autostart on` and
`sharefs autostart off`, and `sharefs status` says what is set up and what is
running.

Only one copy can hold the UDP ports, so ShareFS hands over between the
in-window server and the background one for you when you use that tick box.

### Linux (Debian/Ubuntu Package)

Sharing runs as a system service from the moment the package is installed.

```bash
# What is configured, and what is running
sharefs status

# Or through systemd directly
sudo systemctl status sharefs
sudo systemctl start sharefs
sudo systemctl stop sharefs
sudo systemctl restart sharefs

# Open the window
sharefs
```

Configuration is at `/etc/sharefs.conf`.

Non-root management (Debian/Ubuntu package):
- The installer adds the user who ran it to the `sharefs-admin` group. **Log out
  and back in** for that to take effect. To add somebody else:
  `sudo usermod -aG sharefs-admin someone`.
- `/etc/sharefs.conf` is owned by `root:sharefs-admin` with mode `664`, so group
  members can edit it without sudo.
- A polkit rule allows the `sharefs-admin` group to start, stop and restart
  `sharefs.service` without a password.

### macOS

Open **ShareFS** from Applications. On first run it asks for a folder and starts
sharing; after that it reopens with what you had.

Everything is per-user and needs no `sudo`: settings in
`~/Library/Application Support/ShareFS/sharefs.conf`, the log in
`~/Library/Logs/ShareFS/sharefs.log`, and the login item in
`~/Library/LaunchAgents`. The command line is inside the bundle if you want it
from a terminal:

```bash
/Applications/ShareFS.app/Contents/MacOS/ShareFS status
```

### Windows (Installer)

1. Run `sharefs-server_*-setup.exe`.
2. `sharefs.exe` is installed to `C:\ShareFS`, with the default share at
   `C:\ShareFS\Shares\Public` (writeable for local users out of the box).
3. Configuration goes to `%ProgramData%\ShareFS\sharefs.conf`, which is where
   both the window and the service read it. An existing
   `C:\ShareFS\sharefs.conf` from an earlier version is moved there on upgrade.
4. Firewall rules for UDP 32770, 32771 and 49171 are added during install.
5. The service is installed and started, so sharing survives a reboot. Turn it
   off with the tick box on the **Sharing** tab, or
   `sharefs autostart off` from an administrator prompt.

### Windows (Zip Archive)

Extract it anywhere. No installer, and no firewall rules configured for you.

- **Window:** double-click `sharefs.exe`.
- **Foreground server:** `sharefs.exe serve` from a command prompt.
- **Service:** `sharefs.exe service install` then `sharefs.exe service start`,
  from an **administrator** prompt. `sc query ShareFSServer` reports on it, and
  `sharefs.exe service stop` then `... uninstall` removes it.

> **Firewall Warning:** Ensure Windows Firewall allows UDP ports 32770, 32771, and 49171.

### Using the window

Nine tenths of setting up a file server is choosing a folder, so a fresh
install asks that and nothing else. Everything after that lives on tabs:

- **Server** - Log level, broadcast interval, Access+ authentication, and which
  configuration file is in use
- **Shares** - Add, edit and remove shared folders
- **Printers** - Configure network printer shares
- **MIME Map** - Map file extensions to RISC OS filetypes
- **Sharing** - One status line, the tick box described above, and a live log

**How one binary can be a window and a server.** The window links the same
server core the command line runs, so pressing **Start** runs the server on a
worker thread *inside this process* rather than launching a child. The status
it shows is a fact rather than a probe, and the log pane is fed straight from
the server. Turning on **Keep sharing when this window is closed** moves that
same core into a service or launchd agent, which is the only difference between
the two.

For a machine with no graphical libraries at all, build with
`-DSFS_BUILD_ADMIN=OFF`. That produces the same `sharefs` binary and the same
command line without linking wxWidgets; running it with no arguments serves
instead of opening a window.

### Manual / Development Build

If running directly from the build output (e.g., for testing):

```bash
# Linux (amd64 PC) - serve with a specific configuration, or open the window
./releases/linux/amd64/sharefs serve --config releases/linux/amd64/sharefs.conf
./releases/linux/amd64/sharefs

# Linux (Raspberry Pi / arm64)
./releases/linux/arm64/sharefs serve --config releases/linux/arm64/sharefs.conf

# Windows (x64)
releases/windows/x64/sharefs.exe serve --config releases/windows/x64/sharefs.conf
```

---

## Configuration

Everything is one file, `sharefs.conf`. The ShareFS window is the easiest way to
edit it, but it is plain text and you can edit it by hand.

**Where it lives.** ShareFS searches these in order and uses the first one it
finds, and the window, the command line and the background service all use the
same list, so the file you edit is the file that gets served:

| Platform | Searched, in order |
|----------|--------------------|
| Linux | `/etc/sharefs.conf`, `~/.config/sharefs/sharefs.conf`, `./sharefs.conf` |
| macOS | `/opt/homebrew/etc`, `/usr/local/etc` or `/etc/sharefs.conf`, then `~/Library/Application Support/ShareFS/sharefs.conf`, then `./sharefs.conf` |
| Windows | `%ProgramData%\ShareFS`, `C:\ShareFS`, `%APPDATA%\ShareFS`, then `.\sharefs.conf` |

The system-wide location comes first because that is what a service reads.
`sharefs config search` prints the list with the one in use marked, and
`sharefs config path` prints just that one. In the window it is on the
**Server** tab, and **File → Reveal Configuration File** opens it.

There is no need to create it yourself: a first run writes one, with a single
share, wherever this platform keeps it.

```ini
# ShareFS Configuration

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

ShareFS has a built-in table of common mappings, so a `[mimemap]` section is only needed to add to it or override it.

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

### The window will not start

Install the wxWidgets development packages before building (see **Building**
above). On Debian/Ubuntu: `libwxgtk3.2-dev`. A build without them still
produces a working `sharefs`; it serves rather than opening a window.

### The window saves settings that seem to be ignored

Check that the file it is editing is the file the server reads:

```bash
sharefs config search
```

The marked line is the one in use, and the first one found always wins. Before
0.1.8 the window and the server searched in different orders, so with a file in
two places one could be edited while the other was served.

On Linux, if saving fails outright, you are probably not yet in the
`sharefs-admin` group in this session: the package adds you at install time, but
group membership only applies to logins made afterwards.

---

## License

This project is licensed under the GNU General Public License v3.0. See LICENSE file for details.

Copyright © Andy Timmins, 2025.
