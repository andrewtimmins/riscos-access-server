#!/bin/bash
# ShareFS Server - Build Environment Setup
# This script installs all dependencies needed to build for Linux and Windows
#
# Usage:
#   ./setup-build-env.sh           # Linux only
#   ./setup-build-env.sh --windows # Linux + Windows cross-compilation

set -e

INSTALL_WINDOWS=false

for arg in "$@"; do
    case $arg in
        --windows|-w)
            INSTALL_WINDOWS=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--windows]"
            echo ""
            echo "Options:"
            echo "  --windows, -w  Also install Windows cross-compilation tools"
            echo "  --help, -h     Show this help message"
            exit 0
            ;;
    esac
done

echo "=================================================="
echo "ShareFS Server - Build Environment"
echo "=================================================="
echo ""

# Check for Ubuntu/Debian
if ! command -v apt &> /dev/null; then
    echo "Error: This script only supports Ubuntu/Debian systems."
    echo "This project requires a Debian-based system to build."
    exit 1
fi

echo "Detected Ubuntu/Debian system"
echo ""

# Update package lists
echo "Updating package lists..."
sudo apt update

# Install base build tools
echo ""
echo "Installing build tools..."
sudo apt install -y build-essential cmake git

# Install wxWidgets for admin GUI
echo "Installing wxWidgets..."
if ! sudo apt install -y libwxgtk3.2-dev; then
    echo "Falling back to GTK3..."
    sudo apt install -y libwxgtk3.0-gtk3-dev
fi

echo ""
echo "✓ Linux build environment ready"

# Windows cross-compilation setup
if [ "$INSTALL_WINDOWS" = true ]; then
    echo ""
    echo "Installing Windows cross-compilation tools..."
    sudo apt install -y mingw-w64 mingw-w64-tools
    
    echo ""
    echo "✓ Windows cross-compilation tools installed"
    echo ""
    echo "NOTE: To build the Windows Admin GUI, you need to build wxWidgets for MinGW."
    echo "Run: ./build.sh --windows-wxwidgets"
    echo "This only needs to be done once."
fi

echo ""
echo "=================================================="
echo "Setup complete!"
echo "=================================================="
echo ""
