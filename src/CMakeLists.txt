add_library(sfs STATIC
    log.c
    config.c
    paths.c
    cli.c
    autostart.c
    names.c
    platform.c
    net.c
    handle.c
    broadcast.c
    server.c
    printer.c
    riscos.c
    accessplus.c
    ops.c
)

# The Windows service is part of the library rather than its own executable, so
# that one binary can be the service, install the service, and put a window on
# the screen. See src/service.h.
if(WIN32)
    target_sources(sfs PRIVATE service.c)
endif()

target_include_directories(sfs
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# Single-sourced from project() so the binary, the packages and the About
# dialog can never disagree about the version (issue #17).
target_compile_definitions(sfs
    PUBLIC "SHAREFS_VERSION=\"${PROJECT_VERSION}\""
)

# PUBLIC, because whichever flavour of the binary links the library needs these
# too, and there is no longer one executable per platform to spell them out on.
if(WIN32)
    target_link_libraries(sfs PUBLIC ws2_32 advapi32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(sfs PUBLIC Threads::Threads)
endif()

# One binary, `sharefs`, with modes rather than siblings. With the GUI enabled
# it is built by admin/CMakeLists.txt from the same command line plus a window;
# this is the headless flavour, for a server that has no wxWidgets on it.
if(NOT SFS_BUILD_ADMIN)
    add_executable(sharefs main.c)
    target_link_libraries(sharefs PRIVATE sfs)
    install(TARGETS sharefs RUNTIME DESTINATION bin)

    if(NOT WIN32)
        # sharefs-server was the name for six releases and is in people's
        # scripts and unit files. The binary recognises what it was called and
        # serves headlessly under the old name; see program_name() in cli.c.
        install(CODE "
            execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
                sharefs \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/sharefs-server\")
        ")
    endif()
endif()
