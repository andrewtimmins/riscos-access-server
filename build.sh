#!/bin/bash
# ShareFS Server - Build Script
# Builds for Linux and optionally Windows, creates deb packages
#
# Usage:
#   ./build.sh                    # Linux only
#   ./build.sh --windows          # Linux + Windows (server only)
#   ./build.sh --windows-full     # Linux + Windows (server + admin GUI)
#   ./build.sh --deb              # Linux + create .deb package
#   ./build.sh --all              # Linux + Windows + .deb
#   ./build.sh --windows-wxwidgets # Build wxWidgets for MinGW (one-time setup)
#   ./build.sh --clean            # Clean all build directories

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_LINUX=true
BUILD_WINDOWS=false
BUILD_WINDOWS_FULL=false
BUILD_DEB=false
BUILD_ZIP=false
BUILD_WXWIDGETS=false
CLEAN_ONLY=false

# Parse arguments
for arg in "$@"; do
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
            ;;
        --clean|-c)
            CLEAN_ONLY=true
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --windows, -w         Build Windows server (no admin GUI)"
            echo "  --windows-full        Build Windows server + admin GUI"
            echo "  --deb, -d             Create .deb package"
            echo "  --zip, -z             Create .zip archive (Windows only)"
            echo "  --all, -a             Build everything (Linux + Windows Server + deb)"
            echo "  --all-full            Build everything (Linux + Windows Server & Admin + deb + zip + installer)"
            echo "  --windows-wxwidgets   Build wxWidgets for MinGW (one-time setup)"
            echo "  --clean, -c           Remove all build directories"
            echo "  --help, -h            Show this help message"
            echo ""
            echo "Note: Windows installer requires NSIS (sudo apt install nsis)"
            exit 0
            ;;
    esac
done

# Clean
if [ "$CLEAN_ONLY" = true ]; then
    echo "Cleaning build directories..."
    rm -rf build build-win releases
    echo "Done."
    exit 0
fi

# Create releases directories (clean start)
rm -rf releases
mkdir -p releases/linux releases/windows

# Number of CPU cores for parallel build
NPROC=$(nproc 2>/dev/null || echo 4)

echo "=================================================="
echo "ShareFS Server - Build"
echo "=================================================="
echo ""

# Build wxWidgets for MinGW if requested
if [ "$BUILD_WXWIDGETS" = true ]; then
    echo "Building wxWidgets for MinGW..."
    echo "This may take 10-15 minutes..."
    echo ""
    
    WXDIR="$HOME/wxWidgets-mingw"
    mkdir -p "$WXDIR"
    cd "$WXDIR"
    
    if [ ! -f "wxWidgets-3.2.4.tar.bz2" ]; then
        echo "Downloading wxWidgets 3.2.4..."
        wget -q --show-progress https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.4/wxWidgets-3.2.4.tar.bz2
    fi
    
    if [ ! -d "wxWidgets-3.2.4" ]; then
        echo "Extracting..."
        tar -xjf wxWidgets-3.2.4.tar.bz2
    fi
    
    mkdir -p build-mingw
    cd build-mingw
    
    echo "Configuring wxWidgets..."
    ../wxWidgets-3.2.4/configure \
        --host=x86_64-w64-mingw32 \
        --build=x86_64-linux-gnu \
        --prefix="$WXDIR/install" \
        --disable-shared \
        --enable-unicode \
        --disable-mediactrl \
        --disable-webview \
        > /dev/null 2>&1
    
    echo "Building wxWidgets (this takes a while)..."
    make -j$NPROC > /dev/null 2>&1
    make install > /dev/null 2>&1
    
    cd "$SCRIPT_DIR"
    echo ""
    echo "✓ wxWidgets for MinGW built successfully"
    echo "  Installed to: $WXDIR/install"
    echo ""
fi

# Create MinGW toolchain file if needed
if [ "$BUILD_WINDOWS" = true ] && [ ! -f "mingw-w64-x86_64.cmake" ]; then
    cat > mingw-w64-x86_64.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
fi

# Build Linux
echo "Building for Linux..."
rm -rf build
cmake -S . -B build > /dev/null
cmake --build build -j$NPROC

# Copy Linux binaries to releases
cp build/src/sharefs-server releases/linux/
if [ -f "build/admin/sharefs-admin" ]; then
    cp build/admin/sharefs-admin releases/linux/
fi

echo "✓ Linux build complete"
echo "  Server: releases/linux/sharefs-server"
if [ -f "releases/linux/sharefs-admin" ]; then
    echo "  Admin:  releases/linux/sharefs-admin"
fi

# Build deb package
if [ "$BUILD_DEB" = true ]; then
    echo ""
    echo "Creating .deb package..."
    cd build
    cpack -G DEB > /dev/null 2>&1
    cd "$SCRIPT_DIR"
    
    # Copy deb to releases
    cp build/*.deb releases/linux/
    
    echo "✓ Debian package created"
    echo "  Package: releases/linux/$(ls releases/linux/*.deb 2>/dev/null | head -1 | xargs basename)"
fi

# Build Windows
if [ "$BUILD_WINDOWS" = true ]; then
    echo ""
    echo "Building for Windows..."
    
    rm -rf build-win
    
    if [ "$BUILD_WINDOWS_FULL" = true ]; then
        # Check for wxWidgets
        WXCONFIG="$HOME/wxWidgets-mingw/install/bin/wx-config"
        if [ ! -f "$WXCONFIG" ]; then
            echo "Warning: wxWidgets for MinGW not found."
            echo "Run: ./build.sh --windows-wxwidgets"
            echo "Building server only (no admin GUI)..."
            cmake -S . -B build-win \
                -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake \
                -DSFS_BUILD_ADMIN=OFF > /dev/null
        else
            cmake -S . -B build-win \
                -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake \
                -DwxWidgets_CONFIG_EXECUTABLE="$WXCONFIG" > /dev/null
        fi
    else
        # Server only
        cmake -S . -B build-win \
            -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake \
            -DSFS_BUILD_ADMIN=OFF > /dev/null
    fi
    
    cmake --build build-win -j$NPROC
    
    # Copy Windows binaries to releases
    cp build-win/src/sharefs-server.exe releases/windows/
    cp build-win/src/sharefs-service.exe releases/windows/
    if [ -f "build-win/admin/sharefs-admin.exe" ]; then
        cp build-win/admin/sharefs-admin.exe releases/windows/
    fi
    
    echo "✓ Windows build complete"
    echo "  Server: releases/windows/sharefs-server.exe"
    echo "  Service: releases/windows/sharefs-service.exe"
    if [ -f "releases/windows/sharefs-admin.exe" ]; then
        echo "  Admin:  releases/windows/sharefs-admin.exe"
    fi
fi



# Copy config to releases
cp sharefs.conf.sample releases/linux/sharefs.conf
cp sharefs.conf.sample-windows releases/windows/sharefs.conf

# Copy firewall scripts
if [ -f "scripts/configure-firewall-linux.sh" ]; then
    cp scripts/configure-firewall-linux.sh releases/linux/
    chmod +x releases/linux/configure-firewall-linux.sh
fi
if [ -f "scripts/configure-firewall-windows.bat" ]; then
    cp scripts/configure-firewall-windows.bat releases/windows/
fi

# Create Windows Zip
if [ "$BUILD_ZIP" = true ]; then
    if [ -d "releases/windows" ] && [ "$(ls -A releases/windows)" ]; then
        echo ""
        echo "Creating Windows zip archive..."
        
        # Check if zip is installed
        if ! command -v zip &> /dev/null; then
            echo "Error: 'zip' command not found. Please install it (e.g., sudo apt install zip)."
        else
            # Extract version from CMakeLists.txt
            VERSION=$(grep "project(" CMakeLists.txt | awk '{for(i=1;i<=NF;i++) if($i=="VERSION") print $(i+1)}')
            if [ -z "$VERSION" ]; then VERSION="0.0.0"; fi
            
            ZIP_NAME="sharefs-server_${VERSION}.zip"
            # Remove old zip from destination if it exists
            rm -f "releases/windows/$ZIP_NAME"
            
            # Zip contents of releases/windows
            cd releases/windows
            zip -r "$ZIP_NAME" ./* -x "$ZIP_NAME" > /dev/null
            cd "$SCRIPT_DIR"
            
            echo "✓ Windows zip archive created"
            echo "  Archive: releases/windows/$ZIP_NAME"
        fi
    else
        echo "Warning: No Windows binaries found to zip. Did you forget --windows?"
    fi
fi

# Create Windows NSIS Installer
if [ "$BUILD_WINDOWS" = true ] && [ "$BUILD_ZIP" = true ]; then
    if [ -d "releases/windows" ] && [ "$(ls -A releases/windows)" ]; then
        echo ""
        echo "Creating Windows NSIS installer..."
        
        # Check if makensis is installed
        if ! command -v makensis &> /dev/null; then
            echo "Warning: 'makensis' command not found."
            echo "Install NSIS to create Windows installers (e.g., sudo apt install nsis)."
        else
            # Copy necessary files to build directory for NSIS
            cp LICENSE releases/windows/ 2>/dev/null || echo "Note: LICENSE file not found"
            cp README.md releases/windows/ 2>/dev/null || echo "Note: README.md file not found"
            
            # Build the installer
            makensis -NOCD installer.nsi > /dev/null
            
            if [ -f "sharefs-server_0.1.1-setup.exe" ]; then
                mv sharefs-server_0.1.1-setup.exe releases/windows/
                echo "✓ Windows installer created"
                echo "  Installer: releases/windows/sharefs-server_0.1.1-setup.exe"
            else
                echo "Warning: NSIS installer creation failed"
            fi
        fi
    fi
fi

echo ""
echo "=================================================="
echo "Build complete!"
echo "=================================================="
echo ""
echo "Releases:"
ls -la releases/linux/
if [ "$BUILD_WINDOWS" = true ]; then
    echo ""
    ls -la releases/windows/
fi
