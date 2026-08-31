# The window. This produces `sharefs` itself rather than a second executable
# beside it: the sources here are the wxWidgets half of one program whose
# command line lives in src/cli.c. A build with SFS_BUILD_ADMIN=OFF gets the
# same binary without the window from src/CMakeLists.txt.
cmake_minimum_required(VERSION 3.16)

# For cross-compilation, use wx-config directly
if(CMAKE_CROSSCOMPILING AND WIN32)
    # Use wx-config from the cross-compiled wxWidgets
    if(DEFINED wxWidgets_CONFIG_EXECUTABLE)
        set(WX_CONFIG ${wxWidgets_CONFIG_EXECUTABLE})
    elseif(DEFINED ENV{WX_CONFIG})
        set(WX_CONFIG $ENV{WX_CONFIG})
    else()
        set(WX_CONFIG "wx-config")
    endif()
    
    # Get compile flags from wx-config
    execute_process(
        COMMAND ${WX_CONFIG} --cxxflags
        OUTPUT_VARIABLE WX_CXX_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    # Get linker flags from wx-config
    execute_process(
        COMMAND ${WX_CONFIG} --libs core,base
        OUTPUT_VARIABLE WX_LINK_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    message(STATUS "Cross-compile wxWidgets CXX flags: ${WX_CXX_FLAGS}")
    message(STATUS "Cross-compile wxWidgets link flags: ${WX_LINK_FLAGS}")
    
    set(WX_FOUND TRUE)
else()
    # Native build - use find_package
    find_package(wxWidgets COMPONENTS core base)
    if(wxWidgets_FOUND)
        include(${wxWidgets_USE_FILE})
        set(WX_FOUND TRUE)
    else()
        set(WX_FOUND FALSE)
    endif()
endif()

if(NOT WX_FOUND)
    message(FATAL_ERROR "wxWidgets not found")
endif()

# Sources
set(ADMIN_SOURCES
    src/main.cpp
    src/MainFrame.cpp
    src/AboutDialog.cpp
    src/ConfigIO.cpp
    src/ServerPanel.cpp
    src/SharesPanel.cpp
    src/PrintersPanel.cpp
    src/MimePanel.cpp
    src/ControlPanel.cpp
    src/EmbeddedServer.cpp
    src/FirstRunDialog.cpp
)

add_executable(sharefs WIN32 MACOSX_BUNDLE ${ADMIN_SOURCES})

# macOS needs a real .app bundle, otherwise the binary launches as a background
# process with no Dock entry and cannot be brought to the front.
if(APPLE)
    set_target_properties(sharefs PROPERTIES
        # ShareFS.app, capitalised, because this name is what the Finder and
        # the Dock show. The identifier matches the launchd agent label in
        # src/autostart.c.
        OUTPUT_NAME "ShareFS"
        MACOSX_BUNDLE_BUNDLE_NAME "ShareFS"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "uk.co.andytimmins.sharefs"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) Andy Timmins"
        MACOSX_BUNDLE_EXECUTABLE_NAME "ShareFS"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
endif()

target_compile_definitions(sharefs PRIVATE
    "SHAREFS_VERSION=\"${PROJECT_VERSION}\""
    "SHAREFS_HOMEPAGE=\"https://github.com/andrewtimmins/sharefs-server\""
)

target_include_directories(sharefs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# The admin runs the server in-process, so it links the same core library that
# sharefs-server wraps.
target_link_libraries(sharefs PRIVATE sfs)
if(WIN32)
    target_link_libraries(sharefs PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(sharefs PRIVATE Threads::Threads)
endif()

if(CMAKE_CROSSCOMPILING AND WIN32)
    # Apply wx-config flags directly for cross-compile
    separate_arguments(WX_CXX_FLAGS_LIST UNIX_COMMAND "${WX_CXX_FLAGS}")
    target_compile_options(sharefs PRIVATE ${WX_CXX_FLAGS_LIST})
    
    # Link flags need special handling
    target_link_libraries(sharefs PRIVATE ${WX_LINK_FLAGS})
    
    # Windows libraries for service status checks and network enumeration
    target_link_libraries(sharefs PRIVATE advapi32 iphlpapi)
    
    # Static linking of C/C++ runtime
    target_link_options(sharefs PRIVATE -static -static-libgcc -static-libstdc++)
else()
    target_link_libraries(sharefs PRIVATE ${wxWidgets_LIBRARIES})
    
    # Link with Windows libraries for service status checks and network enumeration
    if(WIN32)
        target_link_libraries(sharefs PRIVATE advapi32 iphlpapi)
    endif()
endif()

install(TARGETS sharefs
    RUNTIME DESTINATION bin
    BUNDLE  DESTINATION .)

if(NOT WIN32 AND NOT APPLE)
    # sharefs-server and sharefs-admin were the names for six releases and are
    # in people's scripts, launchers and unit files. The binary notices what it
    # was called and behaves as that name used to; see program_name() in
    # src/cli.c.
    install(CODE "
        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
            sharefs \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/sharefs-server\")
        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
            sharefs \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/sharefs-admin\")
    ")
endif()
