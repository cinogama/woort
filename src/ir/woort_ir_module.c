#include "woort_ir_module.h"
#include "woort_ir_arena.h"

#include <stdlib.h>
#include <string.h>

#define WOORT_IR_MODULE_INITIAL_FUNCTION_CAPACITY 8
#define WOORT_IR_FUNCTION_INITIAL_LOCAL_CAPACITY 16

static bool _woort_IRFunction_init(
    woort_IRFunction* func,
    woort_IRModule* module,
    const char* name,
    uint32_t param_count,
    woort_IRArena* arena)
{
    func->m_module = module;
    func->m_name = name;
    func->m_id = module->m_function_count;
    func->m_param_count = param_count;

    func->m_entry_block = NULL;
    func->m_block_list = NULL;
    func->m_block_count = 0;

    func->m_locals = woort_IRArena_alloc_array(arena, woort_IRLocal*, WOORT_IR_FUNCTION_INITIAL_LOCAL_CAPACITY);
    if (!func->m_locals)
    {
        return false;
    }
    func->m_local_count = 0;
    func->m_local_capacity = WOORT_IR_FUNCTION_INITIAL_LOCAL_CAPACITY;

    func->m_next_value_id = 0;
    func->m_next_block_id = 0;

    return true;
}

WOORT_NODISCARD bool woort_IRModule_create(woort_IRModule** out_module)
{
    woort_IRModule* module = (woort_IRModule*)malloc(sizeof(woort_IRModule));
    if (!module)
    {
        return false;
    }

    module->m_functions = (woort_IRFunction**)malloc(sizeof(woort_IRFunction*) * WOORT_IR_MODULE_INITIAL_FUNCTION_CAPACITY);
    if (!module->m_functions)
    {
        free(module);
        return false;
    }
    module->m_function_count = 0;
    module->m_function_capacity = WOORT_IR_MODULE_INITIAL_FUNCTION_CAPACITY;

    if (!woort_IRArena_create(64 * 1024, &module->m_arena))
    {
        free(module->m_functions);
        free(module);
        return false;
    }

    *out_module = module;
    return true;
}

void woort_IRModule_destroy(woort_IRModule* module)
{
    if (!module)
    {
        return;
    }

    woort_IRArena_destroy(module->m_arena);
    free(module->m_functions);
    free(module);
}

WOORT_NODISCARD bool woort_IRModule_add_function(
    woort_IRModule* module,
    const char* name,
    uint32_t param_count,
    woort_IRFunction** out_func)
{
    if (module->m_function_count >= module->m_function_capacity)
    {
        uint32_t new_capacity = module->m_function_capacity * 2;
        woort_IRFunction** new_functions = (woort_IRFunction**)realloc(
            module->m_functions,
            sizeof(woort_IRFunction*) * new_capacity);
        if (!new_functions)
        {
            return false;
        }
        module->m_functions = new_functions;
        module->m_function_capacity = new_capacity;
    }

    woort_IRFunction* func = woort_IRArena_alloc_type(module->m_arena, woort_IRFunction);
    if (!func)
    {
        return false;
    }

    if (!_woort_IRFunction_init(func, module, name, param_count, module->m_arena))
    {
        return false;
    }

    module->m_functions[module->m_function_count++] = func;

    if (out_func)
    {
        *out_func = func;
    }

    return true;
}