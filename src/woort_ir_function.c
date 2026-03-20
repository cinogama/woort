/*
 * woort_ir_function.c
 * 
 * IR 函数接口实现。
 */

#include "woort_ir_internal.h"

#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * IRBlock 创建（内部使用）
 ******************************************************************************/

static woort_IRBlock* _woort_IRBlock_create(woort_IRFunction* function)
{
    woort_IRBlock* block = (woort_IRBlock*)malloc(sizeof(woort_IRBlock));
    if (!block)
    {
        return NULL;
    }
    
    memset(block, 0, sizeof(woort_IRBlock));
    block->m_function = function;
    block->m_block_id = function->m_next_block_id++;
    block->m_terminator.m_kind = WOORT_IR_TERMINATOR_NONE;
    
    woort_vector_init(&block->m_instructions, sizeof(woort_IRInstruction*));
    woort_vector_init(&block->m_predecessors, sizeof(woort_IRBlock*));
    woort_vector_init(&block->m_successors, sizeof(woort_IRBlock*));
    
    woort_vector_push_back(&g_ir_block_pool, 1, &block);
    return block;
}

/*******************************************************************************
 * IRFunction 接口实现
 ******************************************************************************/

woort_IRBlock* woort_IRFunction_get_entry_block(woort_IRFunction* function)
{
    if (!function)
    {
        return NULL;
    }
    return function->m_entry_block;
}

bool woort_IRFunction_add_block(
    woort_IRFunction* function,
    /* OPTIONAL */ woort_IRBlock** out_block)
{
    if (!function)
    {
        return false;
    }
    
    woort_IRBlock* block = _woort_IRBlock_create(function);
    if (!block)
    {
        if (out_block)
        {
            *out_block = NULL;
        }
        return false;
    }
    
    woort_vector_push_back(&function->m_blocks, 1, &block);
    
    if (out_block)
    {
        *out_block = block;
    }
    return true;
}

const woort_IRValue* woort_IRFunction_load_const(
    woort_IRFunction* function,
    woort_IRGlobalIndex global_index)
{
    if (!function || !function->m_compiler)
    {
        return NULL;
    }
    
    woort_IRCompiler* compiler = function->m_compiler;
    
    /* 检查是否已缓存 */
    if (global_index < compiler->m_const_values.m_size)
    {
        woort_IRValue* cached = *(woort_IRValue**)woort_vector_at(
            &compiler->m_const_values, global_index);
        if (cached)
        {
            return cached;
        }
    }
    
    /* 创建新的常量 IRValue */
    woort_IRValue* val = _woort_IRValue_create_const(global_index);
    if (!val)
    {
        return NULL;
    }
    
    /* 确保向量足够大 */
    if (global_index >= compiler->m_const_values.m_size)
    {
        size_t old_size = compiler->m_const_values.m_size;
        size_t new_size = global_index + 1;
        woort_vector_resize(&compiler->m_const_values, new_size);
        
        /* 将新扩展的部分初始化为 NULL */
        for (size_t i = old_size; i < new_size; ++i)
        {
            woort_IRValue* null_ptr = NULL;
            woort_vector_push_back(&compiler->m_const_values, 1, &null_ptr);
        }
    }
    
    /* 缓存常量值 */
    woort_IRValue** slot = (woort_IRValue**)woort_vector_at(
        &compiler->m_const_values, global_index);
    *slot = val;
    
    return val;
}

const woort_IRValue* woort_IRFunction_load_argument(
    woort_IRFunction* function,
    size_t argument_index)
{
    if (!function)
    {
        return NULL;
    }
    
    /* 检查是否已缓存 */
    if (argument_index < function->m_argument_values.m_size)
    {
        woort_IRValue* cached = *(woort_IRValue**)woort_vector_at(
            &function->m_argument_values, argument_index);
        if (cached)
        {
            return cached;
        }
    }
    
    /* 创建新的参数 IRValue */
    woort_IRValue* val = _woort_IRValue_create_argument(argument_index);
    if (!val)
    {
        return NULL;
    }
    
    /* 确保向量足够大 */
    while (argument_index >= function->m_argument_values.m_size)
    {
        woort_IRValue* null_ptr = NULL;
        woort_vector_push_back(&function->m_argument_values, 1, &null_ptr);
    }
    
    /* 缓存参数值 */
    woort_IRValue** slot = (woort_IRValue**)woort_vector_at(
        &function->m_argument_values, argument_index);
    *slot = val;
    
    return val;
}

woort_IRStorage* woort_IRFunction_create_storage(woort_IRFunction* function)
{
    if (!function)
    {
        return NULL;
    }
    
    woort_IRStorage* storage = (woort_IRStorage*)malloc(sizeof(woort_IRStorage));
    if (!storage)
    {
        return NULL;
    }
    
    memset(storage, 0, sizeof(woort_IRStorage));
    storage->m_function = function;
    
    woort_vector_init(&storage->m_values_per_block, sizeof(void*) * 2);
    
    woort_vector_push_back(&g_ir_storage_pool, 1, &storage);
    woort_vector_push_back(&function->m_storages, 1, &storage);
    
    return storage;
}

woort_IRCompiler* woort_IRFunction_get_compiler(const woort_IRFunction* function)
{
    if (!function)
    {
        return NULL;
    }
    return function->m_compiler;
}
