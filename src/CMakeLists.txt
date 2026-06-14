add_library(sfs STATIC
    log.c
    config.c
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

add_executable(sharefs-server
    main.c
)

target_include_directories(sfs
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sharefs-server
    sfs
)

if(WIN32)
    target_link_libraries(sharefs-server ws2_32)
    
    # Windows service executable
    add_executable(sharefs-service
        service.c
    )
    
    target_link_libraries(sharefs-service
        sfs
        ws2_32
        advapi32
    )
    
    install(TARGETS sharefs-service RUNTIME DESTINATION bin)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(sharefs-server Threads::Threads)
endif()

install(TARGETS sharefs-server RUNTIME DESTINATION bin)
