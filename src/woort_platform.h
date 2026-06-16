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
#   define WOORT_PLATFORM_OS_APPLE 1
#   include <TargetConditionals.h>
#   if TARGET_OS_OSX
#       define WOORT_PLATFORM_OS_MACOS
#   elif TARGET_OS_IPHONE
#       define WOORT_PLATFORM_OS_IOS
#   endif
#endif

/*
 * POSIX umbrella: any non-Windows, unix-like system
 * (Linux/Android/macOS/iOS/generic Unix). Used for features that are common
 * across these platforms, such as dlopen, pthread and fseeko.
 */
#if !defined(WOORT_PLATFORM_OS_WINDOWS) && \
    (defined(WOORT_PLATFORM_OS_LINUX)  || defined(WOORT_PLATFORM_OS_ANDROID) || \
     defined(WOORT_PLATFORM_OS_APPLE)  || \
     defined(__unix__) || defined(__unix) || defined(__MACH__))
#   define WOORT_PLATFORM_OS_POSIX 1
#endif

#if !defined(WOORT_PLATFORM_OS_WINDOWS) && !defined(WOORT_PLATFORM_OS_LINUX)  && \
    !defined(WOORT_PLATFORM_OS_ANDROID) && !defined(WOORT_PLATFORM_OS_MACOS)  && \
    !defined(WOORT_PLATFORM_OS_IOS)     && !defined(WOORT_PLATFORM_OS_POSIX)
#   error "Unknown operating system, please extend woort_platform.h."
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

/* Compiler detection */
#if defined(_MSC_VER)
#   define WOORT_COMPILER_MSVC 1
#endif
#if defined(__clang__)
#   define WOORT_COMPILER_CLANG 1
#endif
/*
 * Note: clang also defines __GNUC__, so the GCC check must exclude clang
 * to avoid mis-classifying clang builds as GCC.
 */
#if defined(__GNUC__) && !defined(__clang__)
#   define WOORT_COMPILER_GCC 1
#endif
/*
 * GCC-compatible family: corresponds to the common
 * "defined(__clang__) || defined(__GNUC__)" idiom used across the codebase.
 */
#if defined(WOORT_COMPILER_CLANG) || defined(WOORT_COMPILER_GCC)
#   define WOORT_COMPILER_GCC_COMPAT 1
#endif
