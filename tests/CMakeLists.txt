cmake_minimum_required(VERSION 3.16)

add_executable(test_names
    test_names.c
    ../src/names.c
)

target_include_directories(test_names PRIVATE ../src)

add_test(NAME test_names COMMAND test_names)
