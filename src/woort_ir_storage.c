/*
 * woort_ir_storage.c
 * 
 * IR Storage 接口实现。
 */

#include "woort_ir_internal.h"

#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * IRStorage 接口
 ******************************************************************************/

const woort_IRValue* woort_IRBlock_load(
    woort_IRBlock* block,
    const woort_IRStorage* storage)
{
    if (!block || !storage)
    {
        return NULL;
    }
    
    /* 
     * TODO: 实现 PHI 节点生成
     * 
     * 当从 Storage 加载值时，需要检查当前块是否有来自多个前驱的控制流。
     * 如果有多个前驱，需要生成 PHI 指令来合并来自不同路径的值。
     */
    
    /* 简单实现：创建一个表示从 Storage 加载的 IRValue */
    woort_IRValue* val = (woort_IRValue*)malloc(sizeof(woort_IRValue));
    if (!val)
    {
        return NULL;
    }
    
    val->m_kind = WOORT_IRVALUE_KIND_STORAGE_LOAD;
    val->m_data.m_storage = (woort_IRStorage*)storage;
    
    woort_vector_push_back(&g_ir_value_pool, 1, &val);
    
    return val;
}

bool woort_IRBlock_store(
    woort_IRBlock* block,
    woort_IRStorage* storage,
    const woort_IRValue* value)
{
    if (!block || !storage || !value)
    {
        return false;
    }
    
    /* 
     * TODO: 记录存储操作
     * 
     * 需要记录当前块中对 Storage 的存储操作，
     * 以便后续 PHI 节点生成时使用。
     */
    
    return true;
}

bool woort_IRBlock_load_store(
    woort_IRBlock* block,
    woort_IRStorage* storage,
    const woort_IRValue* new_value,
    /* OPTIONAL */ const woort_IRValue** out_old_value)
{
    if (!block || !storage || !new_value)
    {
        return false;
    }
    
    /* 先加载旧值 */
    const woort_IRValue* old_value = woort_IRBlock_load(block, storage);
    
    /* 然后存储新值 */
    if (!woort_IRBlock_store(block, storage, new_value))
    {
        return false;
    }
    
    if (out_old_value)
    {
        *out_old_value = old_value;
    }
    
    return true;
}

bool woort_IRStorage_is_initialized(
    const woort_IRStorage* storage,
    const woort_IRBlock* block)
{
    if (!storage || !block)
    {
        return false;
    }
    
    /* 
     * TODO: 实现初始化检查
     * 
     * 需要检查从入口块到当前块的所有路径上，
     * Storage 是否都被初始化过。
     */
    
    return false;
}

woort_IRFunction* woort_IRStorage_get_function(
    const woort_IRStorage* storage)
{
    if (!storage)
    {
        return NULL;
    }
    return storage->m_function;
}

/*******************************************************************************
 * IRValue 工具接口
 ******************************************************************************/

bool woort_IRValue_is_const(
    const woort_IRValue* value)
{
    if (!value)
    {
        return false;
    }
    return value->m_kind == WOORT_IRVALUE_KIND_CONST;
}

bool woort_IRValue_is_argument(
    const woort_IRValue* value)
{
    if (!value)
    {
        return false;
    }
    return value->m_kind == WOORT_IRVALUE_KIND_ARGUMENT;
}

size_t woort_IRValue_get_argument_index(
    const woort_IRValue* value)
{
    if (!value || value->m_kind != WOORT_IRVALUE_KIND_ARGUMENT)
    {
        return (size_t)-1;
    }
    return value->m_data.m_argument_index;
}

woort_IRGlobalIndex woort_IRValue_get_global_index(
    const woort_IRValue* value)
{
    if (!value || value->m_kind != WOORT_IRVALUE_KIND_CONST)
    {
        return (woort_IRGlobalIndex)-1;
    }
    return value->m_data.m_global_index;
}
