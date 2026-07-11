cmake_minimum_required (VERSION 3.10)

file(GLOB_RECURSE woort_src_c *.c)
file(GLOB_RECURSE woort_src_h *.h)
file(GLOB_RECURSE woort_src_cpp *.cpp)

if (MSVC)
    list(APPEND woort_src_c resources.rc)
endif()

add_library(woort 
    ${woort_src_c}
    ${woort_src_h}
    ${woort_src_cpp}
    "../include/woort.h")

target_compile_definitions(woort PRIVATE -DWOORT_IMPL)

if (WOORT_SPECIFY_PLATFORM_TYPE STREQUAL "WOORT_PLATFORM_32")
    target_compile_definitions(woort PRIVATE -DWOORT_PLATFORM_32)
elseif (WOORT_SPECIFY_PLATFORM_TYPE STREQUAL "WOORT_PLATFORM_64")
    target_compile_definitions(woort PRIVATE -DWOORT_PLATFORM_64)
endif()

set_target_properties(
    woort PROPERTIES OUTPUT_NAME libwoort)
set_target_properties(
    woort PROPERTIES PREFIX "")

set(woort_required_libs "")

if (WOORT_BUILD_WITH_ASMJIT)
    list(APPEND woort_required_libs asmjit)
    target_include_directories(woort PRIVATE "../3rd/asmjit")
    target_compile_definitions(woort PRIVATE -DWOORT_BUILD_WITH_ASMJIT)
endif()

target_link_libraries(woort 
    PRIVATE woort_options
    PRIVATE ${CMAKE_DL_LIBS}
    PUBLIC ${woort_required_libs}
    PUBLIC Threads::Threads)

if (BUILD_SHARED_LIBS)
    if (WOORT_BUILD_WITH_ASMJIT)
        set_target_properties(
            asmjit PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

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
    if ((MSVC_VERSION GREATER_EQUAL 1930))
        target_compile_options(woort PRIVATE /experimental:c11atomics)
    endif()
endif()