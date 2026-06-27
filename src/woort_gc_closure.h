#pragma once

/*
woort_gc_closure.h
*/

#include "woort_gc_closure_types.h"

#include "woort_gc_units.h"
#include "woort_value.h"

#include <stddef.h>

extern const woort_GCUnitProxy WOORT_GCCLOSURE_UNIT_PROXY;

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_script_func(
    const woort_Bytecode* func);
WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_native_func(
    woort_NativeFunction func);

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_script_func_for_env_constant(
    woort_CodeEnv* cenv,
    const woort_Bytecode* func);
WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new_native_func_for_env_constant(
    woort_CodeEnv* cenv,
    woort_NativeFunction func);

WOORT_NODISCARD woort_GCClosure* woort_GCClosure_new(
    const woort_GCClosure* func, size_t captured_count);
