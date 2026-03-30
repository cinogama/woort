#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "woomem.h"
#include "woort_gc_closure.h"
#include "woort_diagnosis.h"

const woort_GCUnitProxy g_gcclosure_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD woort_GCClosure* _woort_GCClosure_new(size_t captured_count)
{
    woort_GCClosure* const gcclosure = woort_GCUnit_alloc_attrib(
        A,
        sizeof(woort_GCClosure)
        + captured_count * sizeof(woort_Value));

    gcclosure->m_gc_unit.m_proxy = &g_gcclosure_unit_proxy;

    gcclosure->m_size = captured_count;

    return gcclosure;
}

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_script_func(
    const woort_Bytecode* func, size_t captured_count)
{
    woort_GCClosure* const gcclosure = _woort_GCClosure_new(captured_count);

    gcclosure->m_kind = WOORT_RUNTIME_FUNCTION_KIND_SCRIPT;
    gcclosure->m_func.m_script_function = func;

    return gcclosure;
}
WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_native_func(
    woort_NativeFunction func, size_t captured_count)
{
    woort_GCClosure* const gcclosure = _woort_GCClosure_new(captured_count);

    gcclosure->m_kind = WOORT_RUNTIME_FUNCTION_KIND_NATIVE;
    gcclosure->m_func.m_native_or_jit_function = func;

    return gcclosure;
}
WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_jit_func(
    woort_NativeFunction func, size_t captured_count)
{
    woort_GCClosure* const gcclosure = _woort_GCClosure_new(captured_count);

    gcclosure->m_kind = WOORT_RUNTIME_FUNCTION_KIND_JIT;
    gcclosure->m_func.m_native_or_jit_function = func;

    return gcclosure;
}

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new(
    const woort_GCClosure* func, size_t captured_count)
{
    woort_GCClosure* const gcclosure = _woort_GCClosure_new(captured_count);

    gcclosure->m_kind = func->m_kind;
    gcclosure->m_func = func->m_func;

    return gcclosure;
}
