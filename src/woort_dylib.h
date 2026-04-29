#pragma once

/*
woort_dylib.h
*/

#include "woort.h"
#include "woort_atomic.h"
#include "woort_hashmap.h"
#include "woort_spin.h"

#include <stdbool.h>

#if defined(_WIN32) || defined(_WIN64)
#   include <windows.h>
#elif defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__MACH__)
#   include <dlfcn.h>
#else
#   define WOORT_DYLIB_DISABLED 1
#endif

struct woort_Dylib
{
    /* OPTIONAL */ void*                         m_native_handle;
    /* OPTIONAL */ woort_ExternLibFunc*          m_fake_funcs;
    /* OPTIONAL */ woort_Dylib*                  m_dependenced;
    char*                                       m_name;
    woort_AtomicUInt64                          m_use_count;
    woort_HashMap                               m_resolved_funcs;
    woort_RWSpinlock                            m_resolved_lock;
};

bool _woort_dylib_bootup(void);
void _woort_dylib_shutdown(void);
