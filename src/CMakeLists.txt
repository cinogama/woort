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
