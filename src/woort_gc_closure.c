#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "woomem.h"
#include "woort_gc_closure.h"
#include "woort_diagnosis.h"

const woort_GCUnitProxy WOORT_GCCLOSURE_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD woort_GCClosure* _woort_GCClosure_new(size_t captured_count)
{
    woort_GCClosure* const gcclosure = woort_GCUnit_alloc_delay_init(
        sizeof(woort_GCClosure)
        + captured_count * sizeof(woort_Value));

    gcclosure->m_gc_unit.m_proxy = &WOORT_GCCLOSURE_UNIT_PROXY;

    gcclosure->m_size = captured_count;

    woort_GCUnit_init_delay_alloc(A, gcclosure);

    return gcclosure;
}
WOORT_NODISCARD woort_GCClosure* _woort_GCClosure_new_for_env_constant(
    woort_CodeEnv* cenv)
{
    woort_GCClosure* gcclosure;
    do
    {
        gcclosure = woomem_allocate_begin(
            sizeof(woort_GCClosure));

        if (gcclosure != NULL)
            break;

        woort_CodeEnv_unlock(cenv);
        {
            _woort_GCUnit_alloc_failed();
        }
        woort_CodeEnv_lock(cenv);

    } while (true);

    gcclosure->m_gc_unit.m_proxy = &WOORT_GCCLOSURE_UNIT_PROXY;

    gcclosure->m_size = 0;

    woort_GCUnit_init_delay_alloc(A, gcclosure);

    return gcclosure;
}

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_script_func(
    const woort_Bytecode* func)
{
    woort_GCClosure* const gcclosure = _woort_GCClosure_new(0);

    gcclosure->m_script_function = func;
    gcclosure->m_jit_function = NULL;

    return gcclosure;
}
WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_native_func(
    woort_NativeFunction func)
{
    woort_GCClosure* const gcclosure = _woort_GCClosure_new(0);

    gcclosure->m_script_function = NULL;
    gcclosure->m_native_function = func;

    return gcclosure;
}

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_script_func_for_env_constant(
    woort_CodeEnv* cenv,
    const woort_Bytecode* func)
{
    woort_GCClosure* const gcclosure = 
        _woort_GCClosure_new_for_env_constant(cenv);

    gcclosure->m_script_function = func;
    gcclosure->m_jit_function = NULL;

    return gcclosure;
}

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_native_func_for_env_constant(
    woort_CodeEnv* cenv,
    woort_NativeFunction func)
{
    woort_GCClosure* const gcclosure = 
        _woort_GCClosure_new_for_env_constant(cenv);

    gcclosure->m_script_function = NULL;
    gcclosure->m_native_function = func;

    return gcclosure;
}

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new(
    const woort_GCClosure* func, size_t captured_count)
{
    woort_GCClosure* const gcclosure = _woort_GCClosure_new(captured_count);

    gcclosure->m_script_function = func->m_script_function;
    gcclosure->m_jit_function = func->m_jit_function;

    return gcclosure;
}
