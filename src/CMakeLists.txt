cmake_minimum_required (VERSION 3.10)

file(GLOB_RECURSE woort_src_cpp *.cpp)
file(GLOB_RECURSE woort_src_c *.c)
file(GLOB_RECURSE woort_src_hpp *.hpp)
file(GLOB_RECURSE woort_src_h *.h)

add_library(woort 
    ${woort_src_cpp} 
    ${woort_src_c}
    ${woort_src_hpp}
    ${woort_src_h})

target_include_directories(woort PRIVATE "../include")
target_include_directories(woort PRIVATE "../3rd/woomem/include")

target_compile_definitions(woort PRIVATE -DWOORT_IMPL=1)

if (BUILD_SHARED_LIBS)
    target_compile_definitions(woort 
        PRIVATE -DWOORT_AS_DYLIB=1)
endif()

set_target_properties(
    woort PROPERTIES OUTPUT_NAME libwoort)
set_target_properties(
    woort PROPERTIES PREFIX "")

target_link_libraries(woort 
    PRIVATE woort_options
    PRIVATE ${CMAKE_DL_LIBS}
    PUBLIC woomem)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(woort PROPERTIES RELEASE_POSTFIX "")
    set_target_properties(woort PROPERTIES RELWITHDEBINFO_POSTFIX "")
    set_target_properties(woort PROPERTIES MINSIZEREL_POSTFIX "")
    set_target_properties(woort PROPERTIES DEBUG_POSTFIX "_debug")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(woort PROPERTIES RELEASE_POSTFIX "32")
    set_target_properties(woort PROPERTIES RELWITHDEBINFO_POSTFIX "32")
    set_target_properties(woort PROPERTIES MINSIZEREL_POSTFIX "32")
    set_target_properties(woort PROPERTIES DEBUG_POSTFIX "32_debug")
else()
    message(FATAL_ERROR "Unknown platform.")
endif()

if (MSVC)
    # Atomic support for C11 is available after VS 2022 17.5 preview 2
    # We need to enable C11Atomic support in VS 2022
    if ((MSVC_VERSION GREATER_EQUAL 1930) AND (MSVC_VERSION LESS 1950))
        target_compile_options(woort PRIVATE /experimental:c11atomics)
    endif()
endif()