cmake_minimum_required (VERSION 3.13)


add_library(woort_options INTERFACE)
target_compile_features(woort_options INTERFACE cxx_std_17)
target_compile_features(woort_options INTERFACE c_std_11)

if (MSVC)
	if (POLICY CMP0141)
		cmake_policy(SET CMP0141 NEW)
		set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT 
			"$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,"
			"$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,"
			"$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
		target_compile_options(woort_options INTERFACE "/source-charset:utf-8")
	endif()
endif()

project ("woort")

add_subdirectory ("src")
add_subdirectory ("test")
