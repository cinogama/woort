cmake_minimum_required (VERSION 3.10)

# Main test
add_executable(woort_test 
    test_main.c)

target_include_directories(woort_test 
    PRIVATE "../include"
    PRIVATE "../src")

target_link_libraries(woort_test 
    PRIVATE woort
    PRIVATE woort_options)

# IR test
add_executable(woort_ir_test
    test_ir.c)

target_include_directories(woort_ir_test 
    PRIVATE "../include"
    PRIVATE "../src")

target_link_libraries(woort_ir_test 
    PRIVATE woort
    PRIVATE woort_options)