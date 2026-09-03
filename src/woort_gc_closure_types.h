#pragma once

/*
woort_gc_closure_types.h
*/

#include "woort_value_types.h"

#include <stddef.h>

struct woort_GCClosure
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */

    /* Might be NULL if native. */
    /* OPTIONAL */ const woort_Bytecode* m_script_function;

    /* If m_script_function is NULL, must be m_native_function. */
    /* Or m_jit_function may be NULL if not JIT. */
    union
    {
        /* OPTIONAL */ woort_NativeFunction m_native_function;
        /* OPTIONAL */ woort_JitFunction m_jit_function;
    };

    size_t      m_size;
    woort_Value m_datas[];

};
