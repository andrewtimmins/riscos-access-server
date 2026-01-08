add_library(ras STATIC
    log.c
    config.c
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

add_executable(access
    main.c
)

target_include_directories(ras
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(access
    ras
)

if(WIN32)
    target_link_libraries(access ws2_32)
    
    # Windows service executable
    add_executable(access-service
        service.c
    )
    
    target_link_libraries(access-service
        ras
        ws2_32
        advapi32
    )
    
    install(TARGETS access-service RUNTIME DESTINATION bin)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(access Threads::Threads)
endif()

install(TARGETS access RUNTIME DESTINATION bin)
