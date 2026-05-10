#pragma once

/*
woort_platform.h - Platform detection macros
*/

/* OS detection */
#if defined(_WIN32)
#   define WO_PLATFORM_OS_WINDOWS
#elif defined(__linux__)
#   define WO_PLATFORM_OS_LINUX
#endif

/* Architecture detection */
#if defined(_M_IX86) || defined(__i386__)
#   define WO_PLATFORM_32
#   define WO_PLATFORM_X86
#   define WO_VM_SUPPORT_FAST_NO_ALIGN
#elif defined(__x86_64__) || defined(_M_X64)
#   define WO_PLATFORM_64
#   define WO_PLATFORM_X64
#   define WO_VM_SUPPORT_FAST_NO_ALIGN
#elif defined(_M_ARM) || defined(__arm__)
#   define WO_PLATFORM_32
#   define WO_PLATFORM_ARM
#elif defined(__aarch64__) || defined(_M_ARM64)
#   define WO_PLATFORM_64
#   define WO_PLATFORM_ARM64
#else
#   if !defined(WO_PLATFORM_32) && !defined(WO_PLATFORM_64)
#       error "Unknown platform, you must specify platform manually."
#   endif
#endif
