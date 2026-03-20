cmake_minimum_required (VERSION 3.10)

add_executable(test_ir test_ir.c)

target_include_directories(test_ir 
    PRIVATE "../include"
    PRIVATE "../src")

target_link_libraries(test_ir 
    PRIVATE woort
    PRIVATE woort_options)