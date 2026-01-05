cmake_minimum_required (VERSION 3.10)

include_directories("../include")

# Only for test:
include_directories("../src")

file(GLOB_RECURSE woort_test_src_cpp *.cpp)
file(GLOB_RECURSE woort_test_src_c *.c)
file(GLOB_RECURSE woort_test_src_hpp *.hpp)
file(GLOB_RECURSE woort_test_src_h *.h)

add_executable(woort_test 
    ${woort_test_src_cpp} 
    ${woort_test_src_c}
    ${woort_test_src_hpp}
    ${woort_test_src_h})

target_link_libraries(woort_test 
    PRIVATE woort
    PRIVATE woort_options)