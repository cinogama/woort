#pragma once

/*
woort_platform.h - Platform detection macros
*/

/* OS detection */
#if defined(_WIN32)
#   define WOORT_PLATFORM_OS_WINDOWS
#elif defined(__linux__)
#   define WOORT_PLATFORM_OS_LINUX
#elif defined(__ANDROID__)
#   define WOORT_PLATFORM_OS_ANDROID
#elif defined(__APPLE__)
#   include <TargetConditionals.h>
#   if TARGET_OS_OSX
#       define WOORT_PLATFORM_OS_MACOS
#   elif TARGET_OS_IPHONE
#       define WOORT_PLATFORM_OS_IOS
#   endif
#endif

/* Architecture detection */
#if defined(_M_IX86) || defined(__i386__)
#   ifndef WOORT_PLATFORM_32
#       define WOORT_PLATFORM_32
#   endif
#   define WOORT_PLATFORM_X86
#elif defined(__x86_64__) || defined(_M_X64)
#   ifndef WOORT_PLATFORM_64
#       define WOORT_PLATFORM_64
#   endif
#   define WOORT_PLATFORM_X64
#elif defined(_M_ARM) || defined(__arm__)
#   ifndef WOORT_PLATFORM_32
#       define WOORT_PLATFORM_32
#   endif
#   define WOORT_PLATFORM_ARM
#elif defined(__aarch64__) || defined(_M_ARM64)
#   ifndef WOORT_PLATFORM_64
#       define WOORT_PLATFORM_64
#   endif
#   define WOORT_PLATFORM_ARM64
#else
#   if defined(WOORT_PLATFORM_32) && defined(WOORT_PLATFORM_64)
#       error "WOORT_PLATFORM_32 and WOORT_PLATFORM_64 both defined, unexpected."
#   elif !defined(WOORT_PLATFORM_32) && !defined(WOORT_PLATFORM_64)
#       error "Unknown platform, you must specify platform manually."
#   endif
#endif
