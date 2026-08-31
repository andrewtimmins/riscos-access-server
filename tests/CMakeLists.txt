cmake_minimum_required(VERSION 3.16)

add_executable(test_names
    test_names.c
    ../src/names.c
)

target_include_directories(test_names PRIVATE ../src)

add_test(NAME test_names COMMAND test_names)

# The 32-bit length the wire protocol carries, and the off_t width trap behind
# issue #22. Header-only, so it needs no source file of its own.
add_executable(test_filedesc
    test_filedesc.c
)

target_include_directories(test_filedesc PRIVATE ../src)

add_test(NAME test_filedesc COMMAND test_filedesc)

# Protocol-level access control tests. These start the real server and drive it
# over UDP, because the properties they check - that a protected share needs
# authentication, and that a handle belongs to the client that opened it -
# cannot be reached from a unit test.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    # Fixture tree the test config points at.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fixtures/Public)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fixtures/Secret)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/Public/ReadMe
         "hello riscos\n")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/Secret/Confidential
         "top secret\n")

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/security_test.conf.in
        ${CMAKE_CURRENT_BINARY_DIR}/security_test.conf
        @ONLY
    )
    add_test(NAME protocol_security
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/protocol_security_test.py
                ${CMAKE_CURRENT_BINARY_DIR}/security_test.conf
                $<TARGET_FILE:sharefs>
    )
    # Binds the real UDP ports, so it must not run alongside another instance.
    set_tests_properties(protocol_security PROPERTIES RUN_SERIAL TRUE)

    # Filename visibility. A RISC OS name starting with '/' is stored with a
    # leading '.', so the catalogue has to list dotfiles - issue #21.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/listing_fixtures/Public/.git)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/listing_fixtures/Public/ReadMe,fff
         "hello riscos\n")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/listing_fixtures/Public/.gitattributes,fff
         "* text=auto\n")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/listing_fixtures/Public/.git/HEAD,fff
         "ref: refs/heads/main\n")

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/listing_test.conf.in
        ${CMAKE_CURRENT_BINARY_DIR}/listing_test.conf
        @ONLY
    )
    add_test(NAME protocol_listing
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/protocol_listing_test.py
                ${CMAKE_CURRENT_BINARY_DIR}/listing_test.conf
                $<TARGET_FILE:sharefs>
    )
    # Binds the same UDP ports as the security test.
    set_tests_properties(protocol_listing PROPERTIES RUN_SERIAL TRUE)
else()
    message(STATUS "Python3 not found - skipping protocol security tests")
endif()
