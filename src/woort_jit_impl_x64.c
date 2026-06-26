#pragma once

#include "woort_jit_impl_x64.h"


static bool woort_JIT_prologue_for_backend_x64(
    const woort_Bytecode* function, 
    size_t function_len, 
    woort_JIT_Backend_Emmiter* out_emmiter)
{
    (void)function;
    (void)function_len;
    
    *out_emmiter = NULL;
    return false;
}

static bool woort_JIT_epilogue_for_backend_x64(
    woort_JIT_Backend_Emmiter emmiter,
    woort_JitFunction* out_code)
{
    (void)emmiter;

    *out_code = NULL;
    return false;
}

static bool woort_JIT_check_for_backend_x64(
    woort_JIT_Backend_Emmiter emmiter)
{
    (void)emmiter;

    return false;
}

static void woort_JIT_code_droper_for_backend_x64(
    woort_JitFunction* code)
{
    (void)code;
}

static const woort_OpcodeDispatchers 
    _WOORT_JIT_BACKEND_CODE_DISPATCHERS_IMPL_X64;

const woort_JIT_Backend WOORT_JIT_BACKEND_IMPL_X64 = {
    .m_emit_prologue = woort_JIT_prologue_for_backend_x64,
    .m_emit_epilogue = woort_JIT_epilogue_for_backend_x64,
    .m_check_state = woort_JIT_check_for_backend_x64,
    .m_dispatchers = &_WOORT_JIT_BACKEND_CODE_DISPATCHERS_IMPL_X64,
    .m_code_dropper = &woort_JIT_code_droper_for_backend_x64,
};