# wxWidgets Admin GUI
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
    src/ConfigIO.cpp
    src/ServerPanel.cpp
    src/SharesPanel.cpp
    src/PrintersPanel.cpp
    src/MimePanel.cpp
    src/ControlPanel.cpp
)

add_executable(access-admin WIN32 ${ADMIN_SOURCES})

target_include_directories(access-admin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CMAKE_CROSSCOMPILING AND WIN32)
    # Apply wx-config flags directly for cross-compile
    separate_arguments(WX_CXX_FLAGS_LIST UNIX_COMMAND "${WX_CXX_FLAGS}")
    target_compile_options(access-admin PRIVATE ${WX_CXX_FLAGS_LIST})
    
    # Link flags need special handling
    target_link_libraries(access-admin PRIVATE ${WX_LINK_FLAGS})
    
    # Static linking of C/C++ runtime
    target_link_options(access-admin PRIVATE -static -static-libgcc -static-libstdc++)
else()
    target_link_libraries(access-admin PRIVATE ${wxWidgets_LIBRARIES})
endif()

install(TARGETS access-admin RUNTIME DESTINATION bin)
