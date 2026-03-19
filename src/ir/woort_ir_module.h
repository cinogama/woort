#pragma once

/*
 * woort_ir_module.h
 */

#include "woort_ir_function.h"
#include "woort_ir_arena.h"
#include <stdbool.h>

typedef struct woort_IRModule
{
    woort_IRFunction**      m_functions;
    uint32_t                m_function_count;
    uint32_t                m_function_capacity;

    woort_IRArena*          m_arena;

} woort_IRModule;

WOORT_NODISCARD bool woort_IRModule_create(woort_IRModule** out_module);

void woort_IRModule_destroy(woort_IRModule* module);

WOORT_NODISCARD bool woort_IRModule_add_function(
    woort_IRModule* module,
    const char* name,
    uint32_t param_count,
    woort_IRFunction** out_func);