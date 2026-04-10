#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>

#include "woort_codeenv.h"
#include "woort_ir_function.h"
#include "woort_ir_srcloc.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_spin.h"
#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_log.h"
#include "woort_util.h"

#include "woomem.h"

static struct _woort_CodeEnv_GlobalCtx
{
    woort_RWSpinlock    m_codeenvs_lock;
    woort_Vector /* woort_CodeEnv* */ m_codeenvs;

    woort_GCUnitProxy   m_proxy;

} *_codeenv_global_ctx = NULL;

void _woort_CodeEnv_GC_destroy(woort_GCUnit* unit)
{
    woort_CodeEnv* const code_env = (woort_CodeEnv*)unit;
    assert(code_env->m_gc_unit.m_proxy == &_codeenv_global_ctx->m_proxy);

    // 先从全局容器中移除该 CodeEnv
    woort_rwspinlock_write_lock(
        &_codeenv_global_ctx->m_codeenvs_lock);

    size_t count = _codeenv_global_ctx->m_codeenvs.m_size;
    for (size_t i = 0; i < count; ++i)
    {
        woort_CodeEnv** ptr = (woort_CodeEnv**)woort_vector_at(
            &_codeenv_global_ctx->m_codeenvs, i);

        if (*ptr == code_env)
        {
            // 找到目标，使用 erase_at 删除
            (void)woort_vector_erase_at(&_codeenv_global_ctx->m_codeenvs, i);
            break;
        }
    }
    woort_rwspinlock_write_unlock(
        &_codeenv_global_ctx->m_codeenvs_lock);

    woort_hashmap_deinit(&code_env->m_trap_records);

    if (code_env->m_mutex != NULL)
        woort_mutex_destroy(code_env->m_mutex);

    /* 释放源码映射数据 */
    free(code_env->m_source_map.m_entries);
    code_env->m_source_map.m_entries = NULL;
    code_env->m_source_map.m_entry_count = 0;
    woort_StringPool_deinit(&code_env->m_srcloc_string_pool);
}

WOORT_NODISCARD bool woort_CodeEnv_bootup(void)
{
    assert(_codeenv_global_ctx == NULL);

    _codeenv_global_ctx =
        malloc(sizeof(struct _woort_CodeEnv_GlobalCtx));

    if (_codeenv_global_ctx == NULL)
    {
        WOORT_DEBUG("Out of memory");
        return false;
    }

    woort_rwspinlock_init(&_codeenv_global_ctx->m_codeenvs_lock);

    // 初始化存储 CodeEnv 指针的 Vector
    woort_vector_init(&_codeenv_global_ctx->m_codeenvs, sizeof(woort_CodeEnv*));

    _codeenv_global_ctx->m_proxy.m_marker = NULL;
    _codeenv_global_ctx->m_proxy.m_destructor =
        &_woort_CodeEnv_GC_destroy;

    return true;
}
void woort_CodeEnv_shutdown(void)
{
    assert(_codeenv_global_ctx != NULL);

    // 清理存储 CodeEnv 指针的 Vector
    woort_vector_deinit(&_codeenv_global_ctx->m_codeenvs);

    woort_rwspinlock_deinit(&_codeenv_global_ctx->m_codeenvs_lock);

    free(_codeenv_global_ctx);

    _codeenv_global_ctx = NULL;
}

WOORT_NODISCARD bool woort_CodeEnv_create(
    const woort_Bytecode* bytecodes,
    size_t bytecodes_count,
    size_t constant_and_static_storage_count,
    woort_CodeEnv** out_code_env)
{
    _Static_assert(_Alignof(woort_CodeEnv) == _Alignof(woort_Value),
        "woort_CodeEnv and woort_Value must have the same align.");

    // 提前上锁，确保 code_env_instance 不会 Missing mark.
    woort_rwspinlock_write_lock(&_codeenv_global_ctx->m_codeenvs_lock);

    woort_CodeEnv* code_env_instance = NULL;
    woort_Bytecode* const codes =
        woort_GCUnit_alloc_attrib(
            O, bytecodes_count * sizeof(woort_Bytecode));

    if (codes != NULL)
    {
        code_env_instance =
            woort_GCUnit_alloc_attrib(
                AF,
                sizeof(woort_CodeEnv)
                + constant_and_static_storage_count * sizeof(woort_Value));
    }

    bool register_result = false;

    if (code_env_instance != NULL)
    {
        code_env_instance->m_gc_unit.m_proxy =
            &_codeenv_global_ctx->m_proxy;

        code_env_instance->m_hold = true;

        code_env_instance->m_mutex = NULL;

        code_env_instance->m_code_begin = codes;
        code_env_instance->m_code_end =
            code_env_instance->m_code_begin + bytecodes_count;

        memcpy(
            codes,
            bytecodes,
            bytecodes_count * sizeof(woort_Bytecode));

        /* 初始化源码映射为空 */
        code_env_instance->m_source_map.m_entries = NULL;
        code_env_instance->m_source_map.m_entry_count = 0;
        woort_StringPool_init(&code_env_instance->m_srcloc_string_pool);

        code_env_instance->m_data_count = constant_and_static_storage_count;

        // Fill 0 for static storage:
        memset(
            code_env_instance->m_data_begin,
            0,
            constant_and_static_storage_count * sizeof(woort_Value));

        // 将新创建的 CodeEnv 注册到全局容器
        // 
        // NOTE: 因为 woort_CodeEnv 使用 GC 管理，即便此处注册失败也不需要
        // 手动执行释放
        register_result = woort_vector_push_back(
            &_codeenv_global_ctx->m_codeenvs,
            1,
            &code_env_instance);
    }
    else
        WOORT_DEBUG("Out of memory.");

    woort_rwspinlock_write_unlock(
        &_codeenv_global_ctx->m_codeenvs_lock);

    if (!register_result)
    {
        // Out of memory.
        return false;
    }

    woort_hashmap_init(
        &code_env_instance->m_trap_records,
        sizeof(woort_Bytecode*),
        sizeof(woort_Bytecode),
        &woort_util_ptr_hash,
        &woort_util_ptr_equal);

    if (!woort_mutex_create(&code_env_instance->m_mutex))
        return false;

    *out_code_env = code_env_instance;
    return true;
}

void woort_CodeEnv_lock(woort_CodeEnv* code_env)
{
    assert(code_env != NULL);
    assert(code_env->m_mutex != NULL);
    woort_mutex_lock(code_env->m_mutex);
}

void woort_CodeEnv_unlock(woort_CodeEnv* code_env)
{
    assert(code_env != NULL);
    assert(code_env->m_mutex != NULL);
    woort_mutex_unlock(code_env->m_mutex);
}

void woort_CodeEnv_drop(
    woort_CodeEnv* code_env)
{
    assert(code_env->m_hold);
    code_env->m_hold = false;
}

WOORT_NODISCARD bool woort_CodeEnv_query_function(
    woort_CodeEnv* code_env,
    woort_IRFunction* f,
    const woort_Bytecode** out_f_addr)
{
    assert(code_env != NULL);
    assert(f != NULL);
    assert(out_f_addr != NULL);

    const woort_Bytecode* func_addr =
        code_env->m_code_begin + f->m_code_offset;

    if (func_addr < code_env->m_code_begin || func_addr >= code_env->m_code_end)
        return false;

    *out_f_addr = func_addr;
    return true;
}

WOORT_NODISCARD bool woort_CodeEnv_find(
    const woort_Bytecode* addr, woort_CodeEnv** out_code_env)
{
    // 获取读锁，允许多线程并发查找
    woort_rwspinlock_read_lock(
        &_codeenv_global_ctx->m_codeenvs_lock);

    const size_t count = _codeenv_global_ctx->m_codeenvs.m_size;
    for (size_t i = 0; i < count; ++i)
    {
        woort_CodeEnv** const ptr = (woort_CodeEnv**)woort_vector_at(
            &_codeenv_global_ctx->m_codeenvs, i);

        woort_CodeEnv* const code_env = *ptr;

        // 检查地址是否在该 CodeEnv 的代码区间内
        if (addr >= code_env->m_code_begin && addr < code_env->m_code_end)
        {
            *out_code_env = code_env;
            woort_rwspinlock_read_unlock(&_codeenv_global_ctx->m_codeenvs_lock);
            return true;
        }
    }

    woort_rwspinlock_read_unlock(
        &_codeenv_global_ctx->m_codeenvs_lock);
    return false;
}

void woort_CodeEnv_GC_mark_all_envs(void)
{
    woort_rwspinlock_read_lock(
        &_codeenv_global_ctx->m_codeenvs_lock);

    const size_t count = _codeenv_global_ctx->m_codeenvs.m_size;
    for (size_t i = 0; i < count; ++i)
    {
        woort_CodeEnv* const code_env =
            *(woort_CodeEnv**)woort_vector_at(
                &_codeenv_global_ctx->m_codeenvs, i);

        woort_CodeEnv_lock(code_env);
        {
            if (code_env->m_hold)
            {
                // Mark this code.
                woomem_mark_unit_head(code_env);
            }
        }
        woort_CodeEnv_unlock(code_env);
    }

    woort_rwspinlock_read_unlock(
        &_codeenv_global_ctx->m_codeenvs_lock);
}

WOORT_NODISCARD bool woort_CodeEnv_set_trap(woort_Bytecode* code)
{
    // Find env
    woort_CodeEnv* codeenv;
    if (!woort_CodeEnv_find(code, &codeenv))
    {
        // Bad code.
        return false;
    }

    bool r = false;
    woort_CodeEnv_lock(codeenv);
    {
        if (WOORT_HASHMAP_RESULT_OK == woort_hashmap_insert(&codeenv->m_trap_records, &code, code))
        {
            // Traped.
            *code = woort_OpCode_TRAP();
            r = true;
        }
    }
    woort_CodeEnv_unlock(codeenv);
    return r;
}

WOORT_NODISCARD bool woort_CodeEnv_clear_trap(woort_Bytecode* code)
{
    woort_CodeEnv* codeenv;
    if (!woort_CodeEnv_find(code, &codeenv))
    {
        return false;
    }

    bool r = false;
    woort_CodeEnv_lock(codeenv);
    {
        woort_Bytecode* value_addr;
        if (woort_hashmap_find(&codeenv->m_trap_records, &code, &value_addr))
        {
            *code = *value_addr;
            (void)woort_hashmap_remove(&codeenv->m_trap_records, &code);
            r = true;
        }
    }
    woort_CodeEnv_unlock(codeenv);
    return r;
}

/* ========================================================================
 * 源码映射 API
 * ======================================================================== */

void woort_CodeEnv_set_source_maps(
    woort_CodeEnv* env,
    const woort_Vector* per_func_entries,
    uint32_t func_count)
{
    /* 计算总条目数 */
    uint32_t total_entries = 0;
    for (uint32_t i = 0; i < func_count; ++i)
        total_entries += (uint32_t)per_func_entries[i].m_size;

    if (total_entries == 0)
        return;

    /* 分配合并的条目数组 */
    woort_SourceMap_Entry* entries = (woort_SourceMap_Entry*)malloc(
        sizeof(woort_SourceMap_Entry) * total_entries);

    if (entries == NULL)
        return; /* OOM: 映射丢失不影响正确性 */

    /* 合并所有函数的条目 */
    uint32_t offset = 0;
    for (uint32_t i = 0; i < func_count; ++i)
    {
        const woort_Vector* vec = &per_func_entries[i];
        for (size_t j = 0; j < vec->m_size; ++j)
        {
            const woort_SourceMap_Entry* src =
                (const woort_SourceMap_Entry*)woort_vector_at(
                    (woort_Vector*)vec, j);

            entries[offset] = *src;

            /*
             * 将路径字符串复制到 CodeEnv 自己的字符串池中，
             * 确保 IRCompiler deinit 后路径仍然有效。
             */
            if (src->m_location.m_filepath != NULL)
            {
                const char* interned = woort_StringPool_intern(
                    &env->m_srcloc_string_pool,
                    src->m_location.m_filepath);
                entries[offset].m_location.m_filepath = interned;
            }

            offset++;
        }
    }

    assert(offset == total_entries);

    env->m_source_map.m_entries = entries;
    env->m_source_map.m_entry_count = total_entries;
}

WOORT_NODISCARD bool woort_CodeEnv_find_srcloc_by_offset(
    const woort_CodeEnv* env,
    uint32_t bytecode_offset,
    woort_SourceLocation* out_location)
{
    return woort_SourceMap_find_by_offset(
        &env->m_source_map, bytecode_offset, out_location);
}

WOORT_NODISCARD bool woort_CodeEnv_find_offset_by_srcloc(
    const woort_CodeEnv* env,
    const char* filepath,
    uint32_t line,
    uint32_t* out_bytecode_offset)
{
    /*
     * 先将外部 filepath 转换为 CodeEnv 字符串池中的 intern 指针。
     * 如果池中没有该路径，说明没有匹配的映射。
     *
     * 注意：这里不能用 woort_StringPool_intern 因为它会修改池（插入新字符串）。
     * 改用 hashmap_find 做只读查找。
     */
    const char* interned_path = NULL;
    if (filepath != NULL)
    {
        void* value_addr;
        if (woort_hashmap_find(
            &((woort_CodeEnv*)env)->m_srcloc_string_pool.m_map,
            &filepath, &value_addr))
        {
            interned_path = *(const char**)value_addr;
        }
        else
        {
            return false; /* 池中无此路径，不可能有匹配 */
        }
    }

    return woort_SourceMap_find_by_line(
        &env->m_source_map, interned_path, line, out_bytecode_offset);
}