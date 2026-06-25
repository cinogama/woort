#pragma once

#include "woort_value.h"

#include <stdbool.h>

typedef struct woort_JIT_Interface
{
    char _;

} woort_JIT_Interface;

WOORT_NODISCARD bool woort_JIT_compile_function(
    const woort_JIT_Interface* interface,
    const woort_Bytecode* function_entry,
    woort_JitFunction* out_compiled_jit_function);
