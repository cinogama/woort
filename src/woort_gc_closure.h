#pragma once

/*
woort_gc_map.h
*/

#include <stddef.h>
#include <stdint.h>

#include "woort_gc_units.h"
#include "woort_value.h"

union woort_GCClosure_Function
{
    const woort_Bytecode* m_script_function;
    woort_NativeFunction m_native_or_jit_function;
};

struct woort_GCClosure
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */

    woort_RuntimeFunction_Kind m_kind;
    union woort_GCClosure_Function m_func;

    size_t      m_size;
    woort_Value m_datas[];

};

extern const woort_GCUnitProxy g_gcclosure_unit_proxy;

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_script_func(
    const woort_Bytecode* func, size_t captured_count);
WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_native_func(
    woort_NativeFunction func, size_t captured_count);
WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_jit_func(
    woort_NativeFunction func, size_t captured_count);

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new(
    const woort_GCClosure* func, size_t captured_count);
