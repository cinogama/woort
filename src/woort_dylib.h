#pragma once

/*
woort_dylib.h
*/

#include "woort.h"

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
    /* OPTIONAL */ const woort_ExternLibFunc*    m_fake_funcs;
    /* OPTIONAL */ woort_Dylib*                  m_dependenced;
    char*                                       m_name;
    size_t                                      m_use_count;
};

void _woort_dylib_bootup(void);
void _woort_dylib_shutdown(void);
