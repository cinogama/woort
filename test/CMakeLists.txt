cmake_minimum_required (VERSION 3.10)

add_library(woort_test_options INTERFACE)
target_include_directories(woort_test_options 
    INTERFACE "../include"
    INTERFACE "../src")

target_link_libraries(woort_test_options 
    INTERFACE woort_options
    INTERFACE woort)

# Main test
add_executable(woort_test 
    test_main.c)

target_link_libraries(woort_test 
    PRIVATE woort_test_options)

# IR test
add_executable(woort_ir_test
    test_ir.c)

target_link_libraries(woort_ir_test 
    PRIVATE woort_test_options)

# Exec IR test
add_executable(woort_exec_ir_test
    test_exec_ir.c)

target_link_libraries(woort_exec_ir_test 
    PRIVATE woort_test_options)
