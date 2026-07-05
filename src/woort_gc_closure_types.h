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

    const woort_Bytecode* m_script_function;

    union
    {
        woort_NativeFunction m_native_function;
        woort_JitFunction m_jit_function;
    };

    size_t      m_size;
    woort_Value m_datas[];

};
