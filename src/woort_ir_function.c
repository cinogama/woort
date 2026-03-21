/*
 * woort_ir_function.c
 */

#include "woort_ir_internal.h"
#include "woort_ir_function.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WOORT_IR_INITIAL_BLOCK_CAPACITY 16
#define WOORT_IR_INITIAL_PHI_CAPACITY 16

WOORT_NODISCARD bool _woort_ir_function_init(
    woort_IRFunction** out_func,
    woort_IRCompiler* compiler,
    uint32_t param_count,
    uint32_t func_index)
{
    assert(out_func != NULL);
    assert(compiler != NULL);

    woort_IRFunction* func = (woort_IRFunction*)malloc(sizeof(woort_IRFunction));
    if (func == NULL)
    {
        return false;
    }

    func->m_compiler = compiler;
    func->m_index = func_index;
    func->m_param_count = param_count;
    func->m_next_value_index = 0;

    /* Allocate parameter values */
    if (param_count > 0)
    {
        func->m_params = (woort_IRValue*)malloc(sizeof(woort_IRValue) * param_count);
        if (func->m_params == NULL)
        {
            free(func);
            return false;
        }

        for (uint32_t i = 0; i < param_count; ++i)
        {
            func->m_params[i].m_defining_block = NULL;
            func->m_params[i].m_index = func->m_next_value_index++;
            func->m_params[i].m_defining_instr = NULL;
        }
    }
    else
    {
        func->m_params = NULL;
    }

    /* Allocate block storage */
    func->m_blocks = (woort_IRBlock**)malloc(sizeof(woort_IRBlock*) * WOORT_IR_INITIAL_BLOCK_CAPACITY);
    if (func->m_blocks == NULL)
    {
        if (func->m_params != NULL)
        {
            free(func->m_params);
        }
        free(func);
        return false;
    }
    func->m_block_count = 0;
    func->m_block_capacity = WOORT_IR_INITIAL_BLOCK_CAPACITY;

    /* Allocate PHI storage */
    func->m_phis = (woort_IRPHI**)malloc(sizeof(woort_IRPHI*) * WOORT_IR_INITIAL_PHI_CAPACITY);
    if (func->m_phis == NULL)
    {
        if (func->m_params != NULL)
        {
            free(func->m_params);
        }
        free(func->m_blocks);
        free(func);
        return false;
    }
    func->m_phi_count = 0;
    func->m_phi_capacity = WOORT_IR_INITIAL_PHI_CAPACITY;

    /* Create entry block */
    woort_IRBlock* entry_block;
    if (!_woort_ir_block_init(&entry_block, func, 0, true))
    {
        if (func->m_params != NULL)
        {
            free(func->m_params);
        }
        free(func->m_blocks);
        free(func->m_phis);
        free(func);
        return false;
    }

    func->m_entry_block = entry_block;
    func->m_blocks[0] = entry_block;
    func->m_block_count = 1;

    *out_func = func;
    return true;
}

void _woort_ir_function_drop(woort_IRFunction* func)
{
    if (func == NULL)
    {
        return;
    }

    /* Drop all blocks */
    for (uint32_t i = 0; i < func->m_block_count; ++i)
    {
        _woort_ir_block_drop(func->m_blocks[i]);
    }
    free(func->m_blocks);

    /* Drop all PHIs */
    for (uint32_t i = 0; i < func->m_phi_count; ++i)
    {
        _woort_ir_phi_drop(func->m_phis[i]);
    }
    free(func->m_phis);

    /* Drop params */
    if (func->m_params != NULL)
    {
        free(func->m_params);
    }

    free(func);
}

WOORT_NODISCARD woort_IRBlock* woort_IRFunction_get_entry_block(woort_IRFunction* func)
{
    assert(func != NULL);
    return func->m_entry_block;
}

WOORT_NODISCARD const woort_IRValue* woort_IRFunction_get_param(woort_IRFunction* func, uint32_t index)
{
    assert(func != NULL);
    assert(index < func->m_param_count);
    return &func->m_params[index];
}

WOORT_NODISCARD bool _woort_ir_function_ensure_block_capacity(woort_IRFunction* func)
{
    if (func->m_block_count < func->m_block_capacity)
    {
        return true;
    }

    uint32_t new_capacity = func->m_block_capacity * 2;
    woort_IRBlock** new_blocks = (woort_IRBlock**)realloc(
        func->m_blocks,
        sizeof(woort_IRBlock*) * new_capacity);

    if (new_blocks == NULL)
    {
        return false;
    }

    func->m_blocks = new_blocks;
    func->m_block_capacity = new_capacity;
    return true;
}

WOORT_NODISCARD bool _woort_ir_function_add_block_internal(
    woort_IRFunction* func,
    woort_IRBlock** out_block)
{
    if (!_woort_ir_function_ensure_block_capacity(func))
    {
        return false;
    }

    woort_IRBlock* block;
    if (!_woort_ir_block_init(&block, func, func->m_block_count, false))
    {
        return false;
    }

    func->m_blocks[func->m_block_count] = block;
    func->m_block_count++;

    *out_block = block;
    return true;
}

WOORT_NODISCARD bool woort_IRFunction_add_block(
    woort_IRFunction* func,
    woort_IRBlock** out_block)
{
    assert(func != NULL);
    assert(out_block != NULL);

    return _woort_ir_function_add_block_internal(func, out_block);
}

WOORT_NODISCARD woort_IRValue* _woort_ir_function_alloc_value(woort_IRFunction* func)
{
    assert(func != NULL);

    /* Values are allocated as part of instructions or PHIs,
     * this function just returns a unique index */
    woort_IRValue* val = (woort_IRValue*)malloc(sizeof(woort_IRValue));
    if (val == NULL)
    {
        return NULL;
    }

    val->m_defining_block = NULL;
    val->m_index = func->m_next_value_index++;
    val->m_defining_instr = NULL;

    return val;
}

WOORT_NODISCARD bool _woort_ir_function_ensure_phi_capacity(woort_IRFunction* func)
{
    if (func->m_phi_count < func->m_phi_capacity)
    {
        return true;
    }

    uint32_t new_capacity = func->m_phi_capacity * 2;
    woort_IRPHI** new_phis = (woort_IRPHI**)realloc(
        func->m_phis,
        sizeof(woort_IRPHI*) * new_capacity);

    if (new_phis == NULL)
    {
        return false;
    }

    func->m_phis = new_phis;
    func->m_phi_capacity = new_capacity;
    return true;
}

WOORT_NODISCARD woort_IRPHI* woort_IRFunction_create_phi(woort_IRFunction* func, woort_IRBlock* block)
{
    assert(func != NULL);
    assert(block != NULL);

    if (!_woort_ir_function_ensure_phi_capacity(func))
    {
        return NULL;
    }

    woort_IRPHI* phi;
    if (!_woort_ir_phi_init(&phi, block, func->m_next_value_index++))
    {
        return NULL;
    }

    func->m_phis[func->m_phi_count] = phi;
    func->m_phi_count++;

    return phi;
}
