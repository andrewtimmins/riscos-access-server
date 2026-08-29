cmake_minimum_required(VERSION 3.16)
project(sharefs-server VERSION 0.1.7 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable POSIX APIs on Unix-like hosts.
# On Darwin, defining _POSIX_C_SOURCE alone puts the system headers into strict
# POSIX mode, which hides strcasecmp/strncasecmp from <strings.h>. wxWidgets
# needs them, so ask for the BSD extensions instead.
if(APPLE)
    add_compile_definitions(_DARWIN_C_SOURCE)
elseif(NOT WIN32)
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

option(SFS_ENABLE_WARNINGS "Enable extra warnings" ON)
option(SFS_BUILD_ADMIN "Build wxWidgets admin GUI" ON)

if(SFS_ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /permissive-)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    endif()
endif()

add_subdirectory(src)

include(CTest)
add_subdirectory(tests)

# Admin GUI (wxWidgets required)
if(SFS_BUILD_ADMIN)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_subdirectory(admin)
endif()

# Install sample config
install(FILES sharefs.conf.sample DESTINATION share/sharefs)

# Install systemd service file (Linux only)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES debian/sharefs.service DESTINATION lib/systemd/system)
endif()

# CPack configuration for Debian packages
set(CPACK_PACKAGE_NAME "sharefs-server")
set(CPACK_PACKAGE_VENDOR "Andy Timmins")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ShareFS Server")
set(CPACK_PACKAGE_DESCRIPTION "ShareFS Server allows modern computers to share files with RISC OS machines using the native ShareFS protocol. Includes file operations, directory browsing, Freeway broadcasts, and Access+ authentication.")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT "https://github.com/andrewtimmins/sharefs-server/issues")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/andrewtimmins/sharefs-server")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Debian-specific settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Andy Timmins")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Dependencies - systemd for service management
if(SFS_BUILD_ADMIN)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd, libwxgtk3.2-1")
else()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd")
endif()

# Debian maintainer scripts for service management
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA 
    "${CMAKE_SOURCE_DIR}/debian/postinst"
    "${CMAKE_SOURCE_DIR}/debian/prerm"
    "${CMAKE_SOURCE_DIR}/debian/postrm"
)

# Generate package with standard naming
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

include(CPack)
