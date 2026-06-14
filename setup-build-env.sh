#!/bin/bash
# ShareFS Server - Build Environment Setup
# This script installs all dependencies needed to build for Linux and Windows
#
# Usage:
#   ./setup-build-env.sh              # Linux (native arch)
#   ./setup-build-env.sh --windows    # Linux + Windows x64 cross-compilation
#   ./setup-build-env.sh --cross-arm64 # Linux amd64 host + arm64 cross-compiler

set -e

INSTALL_WINDOWS=false
INSTALL_CROSS_ARM64=false

for arg in "$@"; do
    case $arg in
        --windows|-w)
            INSTALL_WINDOWS=true
            ;;
        --cross-arm64)
            INSTALL_CROSS_ARM64=true
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --windows, -w     Install Windows x64 cross-compilation (MinGW)"
            echo "  --cross-arm64     Install Linux arm64 cross-compiler (from amd64 host)"
            echo "  --help, -h        Show this help message"
            echo ""
            echo "Native arm64 Linux builds (e.g. Raspberry Pi): run this script with no"
            echo "extra flags on the Pi itself — no cross-compiler needed."
            echo ""
            echo "Windows arm64 MinGW is not available via apt; see build.sh --help."
            exit 0
            ;;
    esac
done

echo "=================================================="
echo "ShareFS Server - Build Environment"
echo "=================================================="
echo ""

if ! command -v apt &> /dev/null; then
    echo "Error: This script only supports Ubuntu/Debian systems."
    exit 1
fi

echo "Detected Ubuntu/Debian system ($(uname -m))"
echo ""

echo "Updating package lists..."
sudo apt update

echo ""
echo "Installing build tools..."
sudo apt install -y build-essential cmake git

echo "Installing wxWidgets..."
if ! sudo apt install -y libwxgtk3.2-dev; then
    echo "Falling back to GTK3..."
    sudo apt install -y libwxgtk3.0-gtk3-dev
fi

if [ "$INSTALL_CROSS_ARM64" = true ]; then
    echo ""
    echo "Installing Linux arm64 cross-compilation tools..."
    sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
fi

echo ""
echo "✓ Linux build environment ready"

if [ "$INSTALL_WINDOWS" = true ]; then
    echo ""
    echo "Installing Windows x64 cross-compilation tools..."
    sudo apt install -y \
        gcc-mingw-w64-x86-64 \
        g++-mingw-w64-x86-64 \
        mingw-w64-tools \
        zip nsis

    echo ""
    echo "✓ Windows x64 cross-compilation tools installed"
    echo ""
    echo "NOTE: To build the Windows Admin GUI, build wxWidgets for MinGW once:"
    echo "  ./build.sh --windows-wxwidgets"
    echo ""
    echo "Windows arm64 (WoA) is not provided by apt. To cross-compile for"
    echo "Windows on ARM, build a MinGW aarch64 toolchain separately:"
    echo "  https://github.com/Windows-on-ARM-Experiments/mingw-woarm64-build"
fi

echo ""
echo "=================================================="
echo "Setup complete!"
echo "=================================================="
echo ""
