cmake_minimum_required (VERSION 3.10)

file(GLOB_RECURSE woort_test_src_cpp *.cpp)
file(GLOB_RECURSE woort_test_src_c *.c)
file(GLOB_RECURSE woort_test_src_hpp *.hpp)
file(GLOB_RECURSE woort_test_src_h *.h)

add_executable(woort_test 
    ${woort_test_src_cpp} 
    ${woort_test_src_c}
    ${woort_test_src_hpp}
    ${woort_test_src_h})


target_include_directories(woort_test 
    PRIVATE "../include"
    # Only for test:
    PRIVATE "../src")

target_link_libraries(woort_test 
    PRIVATE woort
    PRIVATE woort_options)

add_executable(test_ir test_ir.c)

target_include_directories(test_ir 
    PRIVATE "../include"
    PRIVATE "../src")

target_link_libraries(test_ir 
    PRIVATE woort
    PRIVATE woort_options)