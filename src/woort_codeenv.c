#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

#include "woort_codeenv.h"
#include "woort_dylib.h"
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

static bool _extern_constants_free_key(
    const void* key,
    void* value,
    void* user_data)
{
    (void)value;
    (void)user_data;
    char* str = *(char**)key;
    free(str);
    return true;
}

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

    woort_hashmap_foreach(
        &code_env->m_extern_constants,
        &_extern_constants_free_key,
        NULL);
    woort_hashmap_deinit(&code_env->m_extern_constants);

    if (code_env->m_mutex != NULL)
        woort_mutex_destroy(code_env->m_mutex);

    /* 释放源码映射数据 */
    free(code_env->m_source_map.m_entries);
    code_env->m_source_map.m_entries = NULL;
    code_env->m_source_map.m_entry_count = 0;
    woort_StringPool_deinit(&code_env->m_srcloc_string_pool);

    /* 释放函数边界数据 */
    woort_vector_deinit(&code_env->m_function_boundaries);

    /* 释放关联的外部库 */
    for (size_t i = 0; i < code_env->m_extern_libs.m_size; ++i)
    {
        woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(&code_env->m_extern_libs, i);
        woort_dylib_unload(lib, WOORT_DYLIB_UNREF);
    }
    woort_vector_deinit(&code_env->m_extern_libs);
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

        /* 初始化函数边界为空 */
        woort_vector_init(&code_env_instance->m_function_boundaries,
            sizeof(woort_FunctionBoundary));

        code_env_instance->m_data_count = constant_and_static_storage_count;

        woort_vector_init(&code_env_instance->m_extern_libs, sizeof(woort_Dylib*));

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

    woort_hashmap_init(
        &code_env_instance->m_extern_constants,
        sizeof(const char*),
        sizeof(woort_IRConstantIndex),
        &woort_util_cstr_hash,
        &woort_util_cstr_equal);

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

/*
 * woort_CodeEnv_set_trap
 *
 * 将指定位置的字节码替换为 DEBUGTRAP 指令，同时将原始指令保存到
 * m_trap_records 哈希表中（key = 字节码地址, value = 原始指令值）。
 * 若该地址已被 trap，则不做任何操作并返回 false。
 */
WOORT_NODISCARD bool woort_CodeEnv_set_trap(
    woort_CodeEnv* env,
    woort_Bytecode* code)
{
    assert(code >= env->m_code_begin && code < env->m_code_end);

    if (*code == woort_OpCode_DEBUGTRAP())
        return false;

    bool r = false;
    woort_CodeEnv_lock(env);
    {
        /* 插入前 *code 仍为原始指令，hashmap 会按值拷贝保存 */
        if (WOORT_HASHMAP_RESULT_OK == woort_hashmap_insert(&env->m_trap_records, &code, code))
        {
            *code = woort_OpCode_DEBUGTRAP();
            r = true;
        }
    }
    woort_CodeEnv_unlock(env);
    return r;
}

/*
 * woort_CodeEnv_clear_trap
 *
 * 从 m_trap_records 中查找指定地址的 trap 记录，将原始指令写回
 * 字节码位置，然后移除该记录。若该地址未被 trap，返回 false。
 */
WOORT_NODISCARD bool woort_CodeEnv_clear_trap(
    woort_CodeEnv* env,
    woort_Bytecode* code)
{
    assert(code >= env->m_code_begin && code < env->m_code_end);

    if (*code != woort_OpCode_DEBUGTRAP())
        return false;

    bool r = false;
    woort_CodeEnv_lock(env);
    {
        woort_Bytecode* value_addr;
        if (woort_hashmap_find(&env->m_trap_records, &code, &value_addr))
        {
            /* value_addr 指向哈希表中保存的原始指令值，将其写回 *code */
            *code = *value_addr;
            (void)woort_hashmap_remove(&env->m_trap_records, &code);
            r = true;
        }
    }
    woort_CodeEnv_unlock(env);
    return r;
}

/*
 * woort_CodeEnv_raw_trap
 *
 * 读取指定位置的字节码指令，若该位置已被 trap（即当前指令为
 * DEBUGTRAP），则从 m_trap_records 查找并返回原始指令；否则直接
 * 返回 *code。适用于反汇编或检查可能存在断点的字节码。
 *
 * 无锁快速路径：若 *code 不是 DEBUGTRAP，直接返回，无需加锁。
 */
WOORT_NODISCARD woort_Bytecode woort_CodeEnv_raw_trap(
    woort_CodeEnv* env,
    const woort_Bytecode* code)
{
    assert(code >= env->m_code_begin && code < env->m_code_end);

    /* 无锁快速路径：不是 DEBUGTRAP 说明没有 trap，直接返回 */
    if (*code != woort_OpCode_DEBUGTRAP())
        return *code;

    woort_Bytecode r = woort_OpCode_DEBUGTRAP();
    woort_CodeEnv_lock(env);
    {
        woort_Bytecode* value_addr;
        if (woort_hashmap_find(&env->m_trap_records, &code, &value_addr))
        {
            /* 找到 trap 记录，取出保存的原始指令 */
            r = *value_addr;
        }
    }

    /*
     * 若在无锁检查与加锁之间，另一个线程刚好清除了该 trap，
     * 则 *code 已被恢复为原始指令，此时应重新读取 *code。
     */
    if (r != woort_OpCode_DEBUGTRAP())
        r = *code;

    woort_CodeEnv_unlock(env);
    return r;
}

/* ========================================================================
 * 源码映射 API
 * ======================================================================== */

void woort_CodeEnv_set_source_maps(
    woort_CodeEnv* env,
    const woort_Vector* function_source_map)
{
    const uint32_t func_count = (uint32_t)function_source_map->m_size;

    /* 计算总条目数 */
    uint32_t total_entries = 0;
    for (uint32_t i = 0; i < func_count; ++i)
    {
        const woort_Function_SourceMap* fsm =
            (const woort_Function_SourceMap*)woort_vector_at(
                (woort_Vector*)function_source_map, i);
        total_entries += (uint32_t)fsm->m_entries.m_size;
    }

    if (total_entries == 0)
    {
        /* 即使没有源码映射条目，仍然记录函数边界 */
        goto build_function_boundaries;
    }

    /* 分配合并的条目数组 */
    woort_SourceMap_Entry* entries = (woort_SourceMap_Entry*)malloc(
        sizeof(woort_SourceMap_Entry) * total_entries);

    if (entries == NULL)
        goto build_function_boundaries; /* OOM: 映射丢失不影响正确性 */

    /* 合并所有函数的条目 */
    uint32_t offset = 0;
    for (uint32_t i = 0; i < func_count; ++i)
    {
        const woort_Function_SourceMap* fsm =
            (const woort_Function_SourceMap*)woort_vector_at(
                (woort_Vector*)function_source_map, i);
        const woort_Vector* vec = &fsm->m_entries;
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

build_function_boundaries:

    /*
     * 构建函数边界表。
     * 即使没有源码映射条目，函数边界信息仍然有用。
     */
    if (func_count == 0)
        return;

    for (uint32_t i = 0; i < func_count; ++i)
    {
        const woort_Function_SourceMap* fsm =
            (const woort_Function_SourceMap*)woort_vector_at(
                (woort_Vector*)function_source_map, i);
        const woort_IRFunction* f = fsm->m_ir_function;

        woort_FunctionBoundary boundary;
        boundary.m_offset_begin = (uint32_t)f->m_code_offset;
        boundary.m_code_length = (uint32_t)f->m_code_length;

        if (f->m_name != NULL)
        {
            const char* interned = woort_StringPool_intern(
                &env->m_srcloc_string_pool,
                f->m_name);
            boundary.m_name = interned;
        }
        else
        {
            boundary.m_name = NULL;
        }

        /* 忽略 push_back 失败 —— 边界信息丢失不影响正确性 */
        (void)woort_vector_push_back(&env->m_function_boundaries, 1, &boundary);
    }
}

WOORT_NODISCARD bool woort_CodeEnv_find_srcloc_by_offset(
    const woort_CodeEnv* env,
    uint32_t bytecode_offset,
    woort_SourceLocation* out_location)
{
    return woort_SourceMap_find_by_offset(
        &env->m_source_map, bytecode_offset, out_location);
}

WOORT_NODISCARD /* OPTIONAL */ const char* woort_CodeEnv_find_function_name_by_offset(
    const woort_CodeEnv* env,
    uint32_t bytecode_offset)
{
    const woort_Vector* vec = &env->m_function_boundaries;
    const uint32_t count = (uint32_t)vec->m_size;

    if (count == 0)
        return NULL;

    /*
     * 二分查找：找到最后一个 m_offset_begin <= bytecode_offset 的条目。
     * 由于条目按 m_offset_begin 升序排列且函数区间不重叠，
     * 该条目即为目标函数。
     */
    uint32_t lo = 0;
    uint32_t hi = count;

    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        const woort_FunctionBoundary* mid_entry =
            (const woort_FunctionBoundary*)woort_vector_at(
                (woort_Vector*)vec, mid);
        if (mid_entry->m_offset_begin <= bytecode_offset)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return NULL;

    const woort_FunctionBoundary* found =
        (const woort_FunctionBoundary*)woort_vector_at(
            (woort_Vector*)vec, lo - 1);

    /* 验证偏移量在函数范围内 */
    if (bytecode_offset >= found->m_offset_begin + found->m_code_length)
        return NULL;

    return found->m_name;
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

WOORT_NODISCARD bool woort_CodeEnv_register_extern_constant(
    woort_CodeEnv* env,
    const char* name,
    woort_IRConstantIndex cidx)
{
    assert(env != NULL);
    assert(name != NULL);

    size_t name_len = strlen(name);
    char* name_copy = (char*)malloc(name_len + 1);
    if (name_copy == NULL)
        return false;

    memcpy(name_copy, name, name_len + 1);

    woort_hashmap_Result result = woort_hashmap_insert(
        &env->m_extern_constants, &name_copy, &cidx);

    if (result == WOORT_HASHMAP_RESULT_ALREADY_EXIST
        || result == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
    {
        free(name_copy);
        return result == WOORT_HASHMAP_RESULT_ALREADY_EXIST ? false : false;
    }

    return true;
}

WOORT_NODISCARD bool woort_CodeEnv_find_extern_constant(
    const woort_CodeEnv* env,
    const char* name,
    woort_IRConstantIndex* out_cidx)
{
    assert(env != NULL);
    assert(name != NULL);
    assert(out_cidx != NULL);

    void* value_addr;
    if (!woort_hashmap_find((woort_HashMap*)&env->m_extern_constants, &name, &value_addr))
        return false;

    *out_cidx = *(woort_IRConstantIndex*)value_addr;
    return true;
}

WOORT_NODISCARD bool woort_CodeEnv_add_extern_lib(
    woort_CodeEnv* env,
    woort_Dylib* lib)
{
    assert(env != NULL);
    assert(lib != NULL);

    if (!woort_vector_push_back(&env->m_extern_libs, 1, &lib))
        return false;

    /* 增加库的引用计数，确保在 CodeEnv 生命期内库不被释放 */
    woort_dylib_keep(lib);

    return true;
}
