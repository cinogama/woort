cmake_minimum_required(VERSION 3.10)

enable_testing()

file(GLOB woort_test_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

foreach(src ${woort_test_sources})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_include_directories(${name}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(${name}
        PRIVATE woort
        PRIVATE woort_options)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
