cmake_minimum_required (VERSION 3.10)

include_directories("../include")

file(GLOB_RECURSE woort_src_cpp *.cpp)
file(GLOB_RECURSE woort_src_c *.c)
file(GLOB_RECURSE woort_src_hpp *.hpp)
file(GLOB_RECURSE woort_src_h *.h)

add_library(woort 
    ${woort_src_cpp} 
    ${woort_src_c}
    ${woort_src_hpp}
    ${woort_src_h})

set_target_properties(
    woort PROPERTIES OUTPUT_NAME libwoort)
set_target_properties(
    woort PROPERTIES PREFIX "")

target_link_libraries(woort 
    PRIVATE woort_options)

if (MSVC)

    # Atomic support for C11 is available after VS 2022 17.5 preview 2
    # We need to enable C11Atomic support in VS 2022
    if ((MSVC_VERSION GREATER_EQUAL 1930) AND (MSVC_VERSION LESS 1950))
        target_compile_options(woort PRIVATE /experimental:c11atomics)
    endif()
endif()