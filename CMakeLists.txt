cmake_minimum_required (VERSION 3.13)

project ("woort")

option(WOORT_BUILD_FOR_ASAN_TEST    "Build woort for code asan test" OFF)

add_library(woort_options INTERFACE)
target_compile_features(woort_options INTERFACE c_std_11)
target_include_directories(woort_options INTERFACE "include")

if (BUILD_SHARED_LIBS)
    target_compile_definitions(woort_options 
        INTERFACE -DWOORT_SHARED_LIB)
else()
    target_compile_definitions(woort_options 
        INTERFACE -DWOORT_STATIC_LIB)
endif()

if (MSVC)
    if (POLICY CMP0141)
        cmake_policy(SET CMP0141 NEW)
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT 
            "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,"
            "$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,"
            "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
    endif()
    target_compile_options(woort_options INTERFACE "/utf-8")
    target_compile_definitions(woort_options INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

if (WOORT_BUILD_FOR_ASAN_TEST)
    if(MSVC)
        target_compile_options(woort_options INTERFACE "/fsanitize=address")
        target_link_options(woort_options INTERFACE "/fsanitize=address")
    else()
        target_compile_options(woort_options INTERFACE "-fsanitize=address")
        target_link_options(woort_options INTERFACE "-fsanitize=address")
    endif()
endif()

add_subdirectory ("3rd/woomem")
add_subdirectory ("src")

enable_testing()
add_subdirectory ("test")
