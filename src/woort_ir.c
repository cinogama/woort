/*
 * woort_ir.c
 * 
 * IR 编译器核心实现：生命周期管理、IRCompiler 接口。
 */

#include "woort_ir_internal.h"
#include "woort_hashmap.h"

#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * 全局资源管理（保留接口兼容性）
 ******************************************************************************/

bool woort_IRCompiler_bootup(void)
{
    /* 不再需要全局初始化 */
    return true;
}

void woort_IRCompiler_shutdown(void)
{
    /* 不再需要全局清理 */
}

/*******************************************************************************
 * 内部辅助函数
 ******************************************************************************/

woort_IRValue* _woort_IRValue_create_const(
    woort_IRCompiler* compiler,
    woort_IRGlobalIndex global_index)
{
    woort_IRValue* val = (woort_IRValue*)malloc(sizeof(woort_IRValue));
    if (!val)
    {
        return NULL;
    }
    
    val->m_kind = WOORT_IRVALUE_KIND_CONST;
    val->m_data.m_global_index = global_index;
    
    woort_vector_push_back(&compiler->m_value_pool, 1, &val);
    return val;
}

woort_IRValue* _woort_IRValue_create_argument(
    woort_IRCompiler* compiler,
    size_t argument_index)
{
    woort_IRValue* val = (woort_IRValue*)malloc(sizeof(woort_IRValue));
    if (!val)
    {
        return NULL;
    }
    
    val->m_kind = WOORT_IRVALUE_KIND_ARGUMENT;
    val->m_data.m_argument_index = argument_index;
    
    woort_vector_push_back(&compiler->m_value_pool, 1, &val);
    return val;
}

woort_IRInstruction* _woort_IRInstruction_create(
    woort_IRCompiler* compiler,
    woort_IRInstructionKind kind,
    woort_IRBlock* parent_block)
{
    woort_IRInstruction* inst = (woort_IRInstruction*)malloc(sizeof(woort_IRInstruction));
    if (!inst)
    {
        return NULL;
    }
    
    memset(inst, 0, sizeof(woort_IRInstruction));
    inst->m_kind = WOORT_IRVALUE_KIND_INSTRUCTION;
    inst->m_inst_kind = kind;
    inst->m_parent_block = parent_block;
    
    woort_vector_push_back(&compiler->m_instruction_pool, 1, &inst);
    return inst;
}

void _woort_IRBlock_add_instruction(
    woort_IRBlock* block,
    woort_IRInstruction* inst)
{
    woort_vector_push_back(&block->m_instructions, 1, &inst);
}

void _woort_IRBlock_add_successor(
    woort_IRBlock* block,
    woort_IRBlock* successor)
{
    /* 添加后继 */
    woort_vector_push_back(&block->m_successors, 1, &successor);
    
    /* 添加前驱 */
    woort_vector_push_back(&successor->m_predecessors, 1, &block);
}

bool _woort_IRValue_is_valid(const woort_IRValue* value)
{
    return value != NULL && value->m_kind != WOORT_IRVALUE_KIND_INVALID;
}

/*******************************************************************************
 * IRCompiler 实例管理
 ******************************************************************************/

bool woort_IRCompiler_create(/* OPTIONAL */ woort_IRCompiler** out_compiler)
{
    if (!out_compiler)
    {
        return false;
    }
    
    woort_IRCompiler* compiler = (woort_IRCompiler*)malloc(sizeof(woort_IRCompiler));
    if (!compiler)
    {
        *out_compiler = NULL;
        return false;
    }
    
    memset(compiler, 0, sizeof(woort_IRCompiler));
    
    /* 初始化内存池 */
    woort_vector_init(&compiler->m_value_pool, sizeof(woort_IRValue*));
    woort_vector_init(&compiler->m_instruction_pool, sizeof(woort_IRInstruction*));
    woort_vector_init(&compiler->m_block_pool, sizeof(woort_IRBlock*));
    woort_vector_init(&compiler->m_function_pool, sizeof(woort_IRFunction*));
    woort_vector_init(&compiler->m_storage_pool, sizeof(woort_IRStorage*));
    
    woort_vector_init(&compiler->m_functions, sizeof(woort_IRFunction*));
    woort_vector_init(&compiler->m_const_values, sizeof(woort_IRValue*));
    
    *out_compiler = compiler;
    return true;
}

void woort_IRCompiler_destroy(/* OPTIONAL */ woort_IRCompiler* compiler)
{
    if (!compiler)
    {
        return;
    }
    
    /* 释放所有 IRValue */
    for (size_t i = 0; i < compiler->m_value_pool.m_size; ++i)
    {
        woort_IRValue* val = *(woort_IRValue**)woort_vector_at(&compiler->m_value_pool, i);
        free(val);
    }
    woort_vector_deinit(&compiler->m_value_pool);
    
    /* 释放所有 IRInstruction */
    for (size_t i = 0; i < compiler->m_instruction_pool.m_size; ++i)
    {
        woort_IRInstruction* inst = *(woort_IRInstruction**)woort_vector_at(&compiler->m_instruction_pool, i);
        free(inst);
    }
    woort_vector_deinit(&compiler->m_instruction_pool);
    
    /* 释放所有 IRBlock */
    for (size_t i = 0; i < compiler->m_block_pool.m_size; ++i)
    {
        woort_IRBlock* block = *(woort_IRBlock**)woort_vector_at(&compiler->m_block_pool, i);
        woort_vector_deinit(&block->m_instructions);
        woort_vector_deinit(&block->m_predecessors);
        woort_vector_deinit(&block->m_successors);
        free(block);
    }
    woort_vector_deinit(&compiler->m_block_pool);
    
    /* 释放所有 IRFunction */
    for (size_t i = 0; i < compiler->m_function_pool.m_size; ++i)
    {
        woort_IRFunction* func = *(woort_IRFunction**)woort_vector_at(&compiler->m_function_pool, i);
        woort_vector_deinit(&func->m_blocks);
        woort_vector_deinit(&func->m_argument_values);
        woort_vector_deinit(&func->m_storages);
        free(func);
    }
    woort_vector_deinit(&compiler->m_function_pool);
    
    /* 释放所有 IRStorage */
    for (size_t i = 0; i < compiler->m_storage_pool.m_size; ++i)
    {
        woort_IRStorage* storage = *(woort_IRStorage**)woort_vector_at(&compiler->m_storage_pool, i);
        woort_vector_deinit(&storage->m_values_per_block);
        free(storage);
    }
    woort_vector_deinit(&compiler->m_storage_pool);
    
    /* 释放编译器自身的向量 */
    woort_vector_deinit(&compiler->m_functions);
    woort_vector_deinit(&compiler->m_const_values);
    
    free(compiler);
}

/*******************************************************************************
 * 全局存储管理
 ******************************************************************************/

woort_IRGlobalIndex woort_IRCompiler_allocate_global(woort_IRCompiler* compiler)
{
    return compiler->m_global_count++;
}

bool woort_IRCompiler_allocate_global_range(
    woort_IRCompiler* compiler,
    size_t count,
    /* OPTIONAL */ woort_IRGlobalIndex* out_begin)
{
    woort_IRGlobalIndex begin = compiler->m_global_count;
    compiler->m_global_count += count;
    
    if (out_begin)
    {
        *out_begin = begin;
    }
    return true;
}

/*******************************************************************************
 * 函数管理
 ******************************************************************************/

bool woort_IRCompiler_add_function(
    woort_IRCompiler* compiler,
    /* OPTIONAL */ woort_IRFunction** out_function)
{
    if (!compiler)
    {
        return false;
    }
    
    woort_IRFunction* func = (woort_IRFunction*)malloc(sizeof(woort_IRFunction));
    if (!func)
    {
        if (out_function)
        {
            *out_function = NULL;
        }
        return false;
    }
    
    memset(func, 0, sizeof(woort_IRFunction));
    func->m_compiler = compiler;
    
    woort_vector_init(&func->m_blocks, sizeof(woort_IRBlock*));
    woort_vector_init(&func->m_argument_values, sizeof(woort_IRValue*));
    woort_vector_init(&func->m_storages, sizeof(woort_IRStorage*));
    
    /* 创建入口块 */
    woort_IRBlock* entry_block = NULL;
    if (!woort_IRFunction_add_block(func, &entry_block))
    {
        woort_vector_deinit(&func->m_blocks);
        woort_vector_deinit(&func->m_argument_values);
        woort_vector_deinit(&func->m_storages);
        free(func);
        if (out_function)
        {
            *out_function = NULL;
        }
        return false;
    }
    
    entry_block->m_is_entry = true;
    func->m_entry_block = entry_block;
    
    woort_vector_push_back(&compiler->m_function_pool, 1, &func);
    woort_vector_push_back(&compiler->m_functions, 1, &func);
    
    if (out_function)
    {
        *out_function = func;
    }
    return true;
}

size_t woort_IRCompiler_get_function_count(const woort_IRCompiler* compiler)
{
    if (!compiler)
    {
        return 0;
    }
    return compiler->m_functions.m_size;
}

/*******************************************************************************
 * 编译完成
 ******************************************************************************/

bool woort_IRCompiler_finish(
    woort_IRCompiler* compiler,
    /* OPTIONAL */ woort_CodeEnv** out_code_env)
{
    if (!compiler || !out_code_env)
    {
        return false;
    }
    
    if (compiler->m_functions.m_size == 0)
    {
        *out_code_env = NULL;
        return false;
    }
    
    woort_IRCodeGenContext ctx;
    if (!_woort_IRCodeGenContext_init(&ctx, compiler))
    {
        *out_code_env = NULL;
        return false;
    }
    
    woort_IRFunction* first_func = *(woort_IRFunction**)woort_vector_at(&compiler->m_functions, 0);
    
    if (!_woort_IRCodeGen_compile_function(&ctx, first_func))
    {
        _woort_IRCodeGenContext_deinit(&ctx);
        *out_code_env = NULL;
        return false;
    }
    
    bool result = _woort_IRCodeGen_create_code_env(&ctx, out_code_env);
    
    _woort_IRCodeGenContext_deinit(&ctx);
    
    return result;
}
