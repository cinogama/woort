#pragma once

/*
woort_dylib.h
*/

#include "woort.h"
#include "woort_atomic.h"
#include "woort_hashmap.h"
#include "woort_spin.h"
#include "woort_platform.h"

#include <stdbool.h>

#if defined(WOORT_PLATFORM_OS_WINDOWS)
#   include <windows.h>
#elif defined(WOORT_PLATFORM_OS_POSIX)
#   include <dlfcn.h>
#else
#   define WOORT_DYLIB_DISABLED 1
#endif

struct woort_Dylib
{
    /* OPTIONAL */ void*                        m_native_handle;
    /* OPTIONAL */ woort_ExternLibFunc*         m_fake_funcs;
    /* OPTIONAL */ woort_Dylib*                 m_dependenced;
    char*                                       m_name;
    char*                                       m_path;
    /* OPTIONAL */ char*                        m_script_path;
    woort_AtomicUInt64                          m_use_count;
    woort_HashMap                               m_resolved_funcs;
    woort_RWSpinlock                            m_resolved_lock;
};

WOORT_NODISCARD bool _woort_dylib_bootup(void);
void _woort_dylib_shutdown(void);

/*
ATTENTION: Pay attention to thread safety issues. The dylib obtained here does not have an 
extra reference count. If the last reference is released elsewhere at the same time, you may
get an invalid woort_Dylib. Considering that this interface is only used for internal callstack
tracing, the GC should guarantee that the codeenv corresponding to the library remains alive, 
so no special handling is required. However, other parts do not necessarily have this guarantee.
*/
WOORT_NODISCARD bool woort_Dylib_find_by_resolved_func(
    /* OPTIONAL */ void* addr, woort_Dylib** out_dylib);
WOORT_NODISCARD bool woort_Dylib_get_function_name(
    woort_Dylib* dylib, /* OPTIONAL */ void* addr, const char** out_name);
