#!/bin/bash
# ShareFS Server - Build Script
# Builds for Linux and optionally Windows across amd64/arm64 (x64/arm64)
#
# Usage:
#   ./build.sh                         # Linux for host architecture
#   ./build.sh --arch arm64            # Linux arm64 (native on Pi, cross from x86)
#   ./build.sh --windows-only          # Windows x64 only (no Linux rebuild)
#   ./build.sh --windows               # Linux + Windows x64 (server only)
#   ./build.sh --windows --windows-arch arm64
#   ./build.sh --deb                   # Linux + .deb for selected arch
#   ./build.sh --all-full              # Linux + Windows x64 + deb + zip + NSIS
#   ./build.sh --windows-wxwidgets     # One-time wxWidgets for MinGW x64
#   ./build.sh --windows-wxwidgets --windows-arch arm64
#   ./build.sh --clean                 # Remove build directories and releases

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

get_version() {
    grep "project(" CMakeLists.txt | awk '{for(i=1;i<=NF;i++) if($i=="VERSION") print $(i+1)}'
}

normalize_linux_arch() {
    case "$1" in
        x86_64|amd64) echo amd64 ;;
        aarch64|arm64) echo arm64 ;;
        *) echo "" ;;
    esac
}

normalize_windows_arch() {
    case "$1" in
        x86_64|amd64|x64) echo x64 ;;
        aarch64|arm64) echo arm64 ;;
        *) echo "" ;;
    esac
}

host_linux_arch() {
    normalize_linux_arch "$(uname -m)"
}

VERSION=$(get_version)
if [ -z "$VERSION" ]; then VERSION="0.0.0"; fi

BUILD_LINUX=true
BUILD_WINDOWS=false
BUILD_WINDOWS_FULL=false
BUILD_DEB=false
BUILD_ZIP=false
BUILD_WXWIDGETS=false
CLEAN_ONLY=false
FORCE_CROSS_ARM64=false
LINUX_ARCH=""
WINDOWS_ARCH="x64"

# Parse arguments (first pass: flags)
prev=""
for arg in "$@"; do
    if [ -n "$prev" ]; then
        case "$prev" in
            --arch)
                LINUX_ARCH=$(normalize_linux_arch "$arg")
                if [ -z "$LINUX_ARCH" ]; then
                    echo "Error: unsupported Linux architecture '$arg' (use amd64 or arm64)"
                    exit 1
                fi
                ;;
            --windows-arch)
                WINDOWS_ARCH=$(normalize_windows_arch "$arg")
                if [ -z "$WINDOWS_ARCH" ]; then
                    echo "Error: unsupported Windows architecture '$arg' (use x64 or arm64)"
                    exit 1
                fi
                ;;
        esac
        prev=""
        continue
    fi

    case $arg in
        --windows|-w)
            BUILD_WINDOWS=true
            ;;
        --windows-full)
            BUILD_WINDOWS=true
            BUILD_WINDOWS_FULL=true
            ;;
        --deb|-d)
            BUILD_DEB=true
            ;;
        --zip|-z)
            BUILD_ZIP=true
            ;;
        --all|-a)
            BUILD_WINDOWS=true
            BUILD_DEB=true
            ;;
        --all-full)
            BUILD_WINDOWS=true
            BUILD_WINDOWS_FULL=true
            BUILD_DEB=true
            BUILD_ZIP=true
            ;;
        --windows-wxwidgets)
            BUILD_WXWIDGETS=true
            BUILD_LINUX=false
            ;;
        --cross-arm64)
            FORCE_CROSS_ARM64=true
            LINUX_ARCH=arm64
            ;;
        --linux-only)
            BUILD_WINDOWS=false
            ;;
        --windows-only)
            BUILD_LINUX=false
            BUILD_WINDOWS=true
            ;;
        --clean|-c)
            CLEAN_ONLY=true
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Architecture:"
            echo "  --arch ARCH           Linux target: amd64 or arm64 (default: host)"
            echo "  --cross-arm64         Cross-compile Linux arm64 from x86_64 (server only)"
            echo "  --windows-arch ARCH   Windows target: x64 or arm64 (default: x64)"
            echo ""
            echo "Build targets:"
            echo "  --linux-only          Build Linux only (default when no --windows*)"
            echo "  --windows-only        Build Windows only (skip Linux)"
            echo "  --windows, -w         Build Linux + Windows server (no admin GUI)"
            echo "  --windows-full        Build Linux + Windows server + admin GUI"
            echo "  --deb, -d             Create .deb package for selected Linux arch"
            echo "  --zip, -z             Create .zip archive (requires --windows*)"
            echo "  --all, -a             Linux + Windows x64 + deb"
            echo "  --all-full            Linux + Windows x64 + deb + zip + NSIS installer"
            echo "  --windows-wxwidgets   Build wxWidgets for MinGW (one-time per arch)"
            echo "  --clean, -c           Remove build directories and releases"
            echo ""
            echo "Release layout:"
            echo "  releases/linux/{amd64,arm64}/"
            echo "  releases/windows/{x64,arm64}/"
            echo ""
            echo "Supported ARM: aarch64/arm64 only (Raspberry Pi 4/5 with 64-bit OS)."
            echo "Windows arm64 cross-compile requires a MinGW aarch64 toolchain (not in apt)."
            exit 0
            ;;
        --arch|--windows-arch)
            prev="$arg"
            ;;
        -*)
            echo "Error: unknown option '$arg' (try --help)"
            exit 1
            ;;
        *)
            echo "Error: unexpected argument '$arg' (try --help)"
            exit 1
            ;;
    esac
done

if [ -n "$prev" ]; then
    echo "Error: option '$prev' requires a value"
    exit 1
fi

if [ "$FORCE_CROSS_ARM64" = true ]; then
    LINUX_ARCH=arm64
fi

if [ -z "$LINUX_ARCH" ]; then
    LINUX_ARCH=$(host_linux_arch)
fi
if [ -z "$LINUX_ARCH" ]; then
    echo "Error: unsupported host architecture $(uname -m)"
    exit 1
fi

HOST_LINUX_ARCH=$(host_linux_arch)
LINUX_CROSS=false
if [ "$LINUX_ARCH" != "$HOST_LINUX_ARCH" ]; then
    if [ "$LINUX_ARCH" = "arm64" ] && [ "$HOST_LINUX_ARCH" = "amd64" ]; then
        LINUX_CROSS=true
    else
        echo "Error: cannot build linux/$LINUX_ARCH on a $HOST_LINUX_ARCH host"
        echo "Native builds only — use the matching machine or CI runner."
        exit 1
    fi
fi

if [ "$FORCE_CROSS_ARM64" = true ] && [ "$HOST_LINUX_ARCH" != "amd64" ]; then
    echo "Error: --cross-arm64 is only for cross-compiling from x86_64"
    exit 1
fi

if [ "$BUILD_ZIP" = true ] && [ "$BUILD_WINDOWS" = false ]; then
    echo "Error: --zip requires --windows, --windows-full, --windows-only, or --all-full."
    exit 1
fi

if [ "$BUILD_DEB" = true ] && [ "$BUILD_LINUX" = false ]; then
    echo "Error: --deb requires a Linux build (omit --windows-only)."
    exit 1
fi

LINUX_RELEASE="releases/linux/$LINUX_ARCH"
WINDOWS_RELEASE="releases/windows/$WINDOWS_ARCH"
MINGW_TOOLCHAIN="cmake/toolchains/mingw-w64-x86_64.cmake"
MINGW_HOST="x86_64-w64-mingw32"
WX_MINGW_DIR="$HOME/wxWidgets-mingw-x64"
WX_MINGW_DIR_LEGACY="$HOME/wxWidgets-mingw/install"

if [ "$WINDOWS_ARCH" = "arm64" ]; then
    MINGW_TOOLCHAIN="cmake/toolchains/mingw-w64-aarch64.cmake"
    MINGW_HOST="aarch64-w64-mingw32"
    WX_MINGW_DIR="$HOME/wxWidgets-mingw-arm64"
fi

NPROC=$(nproc 2>/dev/null || echo 4)

if [ "$CLEAN_ONLY" = true ]; then
    echo "Cleaning build directories..."
    rm -rf build build-win build-win-arm64 releases
    echo "Done."
    exit 0
fi

if [ "$BUILD_LINUX" = true ]; then
    mkdir -p "$LINUX_RELEASE"
fi
if [ "$BUILD_WINDOWS" = true ]; then
    mkdir -p "$WINDOWS_RELEASE"
fi

echo "=================================================="
echo "ShareFS Server - Build"
echo "=================================================="
echo ""
if [ "$BUILD_LINUX" = true ]; then
    echo "Linux arch:   $LINUX_ARCH$([ "$LINUX_CROSS" = true ] && echo " (cross-compiled)" || echo " (native)")"
fi
if [ "$BUILD_WINDOWS" = true ]; then
    echo "Windows arch: $WINDOWS_ARCH"
fi
echo ""

resolve_wx_config() {
    local wxconfig="$WX_MINGW_DIR/install/bin/wx-config"
    if [ -f "$wxconfig" ]; then
        echo "$wxconfig"
        return
    fi
    if [ "$WINDOWS_ARCH" = "x64" ] && [ -f "$WX_MINGW_DIR_LEGACY/bin/wx-config" ]; then
        echo "$WX_MINGW_DIR_LEGACY/bin/wx-config"
        return
    fi
    echo ""
}

build_wxwidgets_mingw() {
    echo "Building wxWidgets for MinGW ($WINDOWS_ARCH)..."
    echo "This may take 10-15 minutes..."
    echo ""

    mkdir -p "$WX_MINGW_DIR"
    cd "$WX_MINGW_DIR"

    if [ ! -f "wxWidgets-3.2.4.tar.bz2" ]; then
        echo "Downloading wxWidgets 3.2.4..."
        wget -q --show-progress https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.4/wxWidgets-3.2.4.tar.bz2
    fi

    if [ ! -d "wxWidgets-3.2.4" ]; then
        echo "Extracting..."
        tar -xjf wxWidgets-3.2.4.tar.bz2
    fi

    rm -rf build-mingw
    mkdir -p build-mingw
    cd build-mingw

    echo "Configuring wxWidgets for $MINGW_HOST..."
    if ! ../wxWidgets-3.2.4/configure \
        --host="$MINGW_HOST" \
        --build="$(gcc -dumpmachine)" \
        --prefix="$WX_MINGW_DIR/install" \
        --disable-shared \
        --enable-unicode \
        --disable-mediactrl \
        --disable-webview > /dev/null 2>&1; then
        echo "Error: wxWidgets configure failed (see $WX_MINGW_DIR/build-mingw/config.log)"
        exit 1
    fi

    echo "Building wxWidgets..."
    if ! make -j"$NPROC" > /dev/null 2>&1; then
        echo "Error: wxWidgets build failed"
        exit 1
    fi
    make install > /dev/null 2>&1

    cd "$SCRIPT_DIR"
    echo ""
    echo "✓ wxWidgets for MinGW ($WINDOWS_ARCH) built successfully"
    echo "  Installed to: $WX_MINGW_DIR/install"
    echo ""
}

if [ "$BUILD_WXWIDGETS" = true ]; then
    if [ "$WINDOWS_ARCH" = "arm64" ] && ! command -v "$MINGW_HOST-gcc" &>/dev/null; then
        echo "Error: $MINGW_HOST-gcc not found."
        echo "Windows arm64 MinGW is not available via apt on Debian/Ubuntu."
        echo "See: https://github.com/Windows-on-ARM-Experiments/mingw-woarm64-build"
        exit 1
    fi
    if [ "$WINDOWS_ARCH" = "x64" ] && ! command -v x86_64-w64-mingw32-gcc &>/dev/null; then
        echo "Error: x86_64-w64-mingw32-gcc not found."
        echo "Install with: ./setup-build-env.sh --windows"
        exit 1
    fi
    build_wxwidgets_mingw
    exit 0
fi

stage_linux_release() {
    mkdir -p "$LINUX_RELEASE"
    cp build/src/sharefs-server "$LINUX_RELEASE/"
    if [ -f "build/admin/sharefs-admin" ]; then
        cp build/admin/sharefs-admin "$LINUX_RELEASE/"
    fi
    cp sharefs.conf.sample "$LINUX_RELEASE/sharefs.conf"
    if [ -f "scripts/configure-firewall-linux.sh" ]; then
        cp scripts/configure-firewall-linux.sh "$LINUX_RELEASE/"
        chmod +x "$LINUX_RELEASE/configure-firewall-linux.sh"
    fi
}

build_linux() {
    echo "Building for Linux ($LINUX_ARCH)..."
    rm -rf build

    local cmake_args=(-S . -B build)
    local build_admin=ON

    if [ "$LINUX_CROSS" = true ]; then
        if ! command -v aarch64-linux-gnu-gcc &>/dev/null; then
            echo "Error: aarch64-linux-gnu-gcc not found."
            echo "Install with: ./setup-build-env.sh --cross-arm64"
            exit 1
        fi
        cmake_args+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64-gnu.cmake)
        build_admin=OFF
        echo "Note: cross-compiling server only (admin GUI skipped)."
    fi

    cmake "${cmake_args[@]}" -DSFS_BUILD_ADMIN="$build_admin" > /dev/null
    cmake --build build -j"$NPROC"
    stage_linux_release

    echo "✓ Linux build complete ($LINUX_ARCH)"
    echo "  Server: $LINUX_RELEASE/sharefs-server"
    if [ -f "$LINUX_RELEASE/sharefs-admin" ]; then
        echo "  Admin:  $LINUX_RELEASE/sharefs-admin"
    fi

    if [ "$BUILD_DEB" = true ]; then
        echo ""
        echo "Creating .deb package ($LINUX_ARCH)..."
        (
            cd build
            cpack -G DEB > /dev/null 2>&1
        )
        shopt -s nullglob
        local debs=(build/*.deb)
        shopt -u nullglob
        if [ ${#debs[@]} -eq 0 ]; then
            echo "Error: cpack did not produce a .deb file"
            exit 1
        fi
        cp "${debs[@]}" "$LINUX_RELEASE/"
        echo "✓ Debian package created"
        echo "  Package: $LINUX_RELEASE/$(basename "${debs[0]}")"
    fi
}

build_windows() {
    local mingw_gcc="${MINGW_HOST}-gcc"
    if ! command -v "$mingw_gcc" &>/dev/null; then
        echo ""
        if [ "$WINDOWS_ARCH" = "arm64" ]; then
            echo "Error: $mingw_gcc not found."
            echo "Windows arm64 MinGW is not in standard Debian/Ubuntu packages."
            echo "Build a toolchain from:"
            echo "  https://github.com/Windows-on-ARM-Experiments/mingw-woarm64-build"
        else
            echo "Error: $mingw_gcc not found."
            echo "Install with: ./setup-build-env.sh --windows"
        fi
        exit 1
    fi

    echo ""
    echo "Building for Windows ($WINDOWS_ARCH)..."

    local build_dir="build-win"
    if [ "$WINDOWS_ARCH" = "arm64" ]; then
        build_dir="build-win-arm64"
    fi
    rm -rf "$build_dir"
    mkdir -p "$WINDOWS_RELEASE"

    local wxconfig
    wxconfig=$(resolve_wx_config)
    local cmake_args=(
        -S . -B "$build_dir"
        -DCMAKE_TOOLCHAIN_FILE="$MINGW_TOOLCHAIN"
    )

    if [ "$BUILD_WINDOWS_FULL" = true ] && [ -n "$wxconfig" ]; then
        cmake_args+=(-DwxWidgets_CONFIG_EXECUTABLE="$wxconfig")
    else
        if [ "$BUILD_WINDOWS_FULL" = true ]; then
            echo "Warning: wxWidgets for MinGW ($WINDOWS_ARCH) not found."
            echo "Run: ./build.sh --windows-wxwidgets --windows-arch $WINDOWS_ARCH"
            echo "Building server only (no admin GUI)..."
        fi
        cmake_args+=(-DSFS_BUILD_ADMIN=OFF)
    fi

    cmake "${cmake_args[@]}" > /dev/null
    cmake --build "$build_dir" -j"$NPROC"

    cp "$build_dir/src/sharefs-server.exe" "$WINDOWS_RELEASE/"
    cp "$build_dir/src/sharefs-service.exe" "$WINDOWS_RELEASE/"
    if [ -f "$build_dir/admin/sharefs-admin.exe" ]; then
        cp "$build_dir/admin/sharefs-admin.exe" "$WINDOWS_RELEASE/"
    fi
    cp sharefs.conf.sample-windows "$WINDOWS_RELEASE/sharefs.conf"
    if [ -f "scripts/configure-firewall-windows.bat" ]; then
        cp scripts/configure-firewall-windows.bat "$WINDOWS_RELEASE/"
    fi

    echo "✓ Windows build complete ($WINDOWS_ARCH)"
    echo "  Server:  $WINDOWS_RELEASE/sharefs-server.exe"
    echo "  Service: $WINDOWS_RELEASE/sharefs-service.exe"
    if [ -f "$WINDOWS_RELEASE/sharefs-admin.exe" ]; then
        echo "  Admin:   $WINDOWS_RELEASE/sharefs-admin.exe"
    fi
}

create_windows_zip() {
    if [ ! -f "$WINDOWS_RELEASE/sharefs-server.exe" ]; then
        echo ""
        echo "Error: No Windows binaries to zip in $WINDOWS_RELEASE"
        exit 1
    fi

    echo ""
    echo "Creating Windows zip archive ($WINDOWS_ARCH)..."

    if ! command -v zip &>/dev/null; then
        echo "Error: 'zip' command not found. Install it (e.g., sudo apt install zip)."
        exit 1
    fi

    local zip_name="sharefs-server_${VERSION}"
    if [ "$WINDOWS_ARCH" = "arm64" ]; then
        zip_name="${zip_name}-arm64"
    fi
    zip_name="${zip_name}.zip"
    rm -f "$WINDOWS_RELEASE/$zip_name"

    (
        cd "$WINDOWS_RELEASE"
        zip -r "$zip_name" . -x "$zip_name" > /dev/null
    )

    echo "✓ Windows zip archive created"
    echo "  Archive: $WINDOWS_RELEASE/$zip_name"
}

create_windows_installer() {
    if [ "$WINDOWS_ARCH" != "x64" ]; then
        echo ""
        echo "Note: NSIS installer is x64 only; arm64 releases use zip."
        return
    fi

    if [ ! -f "$WINDOWS_RELEASE/sharefs-server.exe" ]; then
        return
    fi

    echo ""
    echo "Creating Windows NSIS installer (x64)..."

    if ! command -v makensis &>/dev/null; then
        echo "Warning: 'makensis' command not found."
        echo "Install NSIS to create Windows installers (e.g., sudo apt install nsis)."
        return
    fi

    cp LICENSE "$WINDOWS_RELEASE/" 2>/dev/null || echo "Note: LICENSE file not found"
    cp README.md "$WINDOWS_RELEASE/" 2>/dev/null || echo "Note: README.md file not found"

    local installer="sharefs-server_${VERSION}-setup.exe"
    local nsis_release_dir
    nsis_release_dir=$(echo "$WINDOWS_RELEASE" | tr '/' '\\')
    makensis -NOCD \
        -DPRODUCT_VERSION="${VERSION}" \
        -DWINDOWS_RELEASE_DIR="${nsis_release_dir}" \
        installer.nsi > /dev/null

    if [ -f "$installer" ]; then
        mv "$installer" "$WINDOWS_RELEASE/"
        echo "✓ Windows installer created"
        echo "  Installer: $WINDOWS_RELEASE/$installer"
    else
        echo "Warning: NSIS installer creation failed"
    fi
}

if [ "$BUILD_LINUX" = true ]; then
    build_linux
fi

if [ "$BUILD_WINDOWS" = true ]; then
    build_windows
fi

if [ "$BUILD_ZIP" = true ]; then
    create_windows_zip
fi

if [ "$BUILD_WINDOWS" = true ] && [ "$BUILD_ZIP" = true ]; then
    create_windows_installer
fi

echo ""
echo "=================================================="
echo "Build complete!"
echo "=================================================="
echo ""
echo "Releases:"
if [ "$BUILD_LINUX" = true ]; then
    ls -la "$LINUX_RELEASE/"
fi
if [ "$BUILD_WINDOWS" = true ]; then
    echo ""
    ls -la "$WINDOWS_RELEASE/"
fi
