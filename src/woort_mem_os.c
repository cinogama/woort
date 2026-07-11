/*
woort_mem_os.c
OS-level virtual memory primitives (merged win32 + unix).
*/

#include "woort_mem_os.h"
#include "woort_platform.h"

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   include <sys/mman.h>
#   include <unistd.h>
#   include <errno.h>
#endif

size_t woort_mem_os_page_size(void)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return (size_t)sysInfo.dwPageSize;
#elif defined(WOORT_PLATFORM_OS_POSIX)
    return (size_t)getpagesize();
#else
    return 4096;
#endif
}

/* OPTIONAL */ void* woort_mem_os_reserve_memory(size_t size)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    return VirtualAlloc(
        NULL,
        size,
        MEM_RESERVE,
        PAGE_NOACCESS);
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   if defined(__EMSCRIPTEN__)
    void* result = mmap(
        NULL,
        size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0);
#   else
    void* result = mmap(
        NULL,
        size,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
#   endif
    return result == MAP_FAILED ? NULL : result;
#else
    return NULL;
#endif
}

int /* 0 means OK */ woort_mem_os_commit_memory(void* addr, size_t size)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    void* result = VirtualAlloc(
        addr,
        size,
        MEM_COMMIT,
        PAGE_READWRITE);
    return result == NULL ? (int)GetLastError() : 0;
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   if defined(__EMSCRIPTEN__)
    return 0;
#   else
    int result = mprotect(
        addr,
        size,
        PROT_READ | PROT_WRITE);
    return result == 0 ? 0 : errno;
#   endif
#else
    return -1;
#endif
}

int /* 0 means OK */ woort_mem_os_decommit_memory(void* addr, size_t size)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    BOOL result = VirtualFree(
        addr,
        size,
        MEM_DECOMMIT);
    return result == FALSE ? (int)GetLastError() : 0;
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   if defined(__EMSCRIPTEN__)
    return 0;
#   else
    int result = mprotect(
        addr,
        size,
        PROT_NONE);
    return result == 0 ? 0 : errno;
#   endif
#else
    return -1;
#endif
}

int /* 0 means OK */ woort_mem_os_release_memory(void* addr, size_t size)
{
#if defined(WOORT_PLATFORM_OS_WINDOWS)
    (void)size;
    BOOL result = VirtualFree(
        addr,
        0,
        MEM_RELEASE);
    return result == FALSE ? (int)GetLastError() : 0;
#elif defined(WOORT_PLATFORM_OS_POSIX)
    int result = munmap(
        addr,
        size);
    return result == 0 ? 0 : errno;
#else
    (void)size;
    return -1;
#endif
}
