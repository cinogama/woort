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
#include "woort_gc_closure.h"
#include "woort_gc_string.h"
#include "woort_gc_struct.h"
#include "woort_value.h"
#include "woort_vfs.h"

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

    (void)woort_hashmap_foreach(
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

    /* 释放常量记录数据 */
    for (size_t i = 0; i < code_env->m_const_records.m_size; ++i)
    {
        woort_ConstRecord* rec = (woort_ConstRecord*)woort_vector_at(
            &code_env->m_const_records, i);
        free(rec->m_lib_name);
        free(rec->m_func_name);
    }
    woort_vector_deinit(&code_env->m_const_records);

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

        woort_vector_init(&code_env_instance->m_const_records, sizeof(woort_ConstRecord));

        /* 预填充 m_const_records 为 NIL 类型 */
        {
            void* buffer;
            _Static_assert(WOORT_CONST_TYPE_NIL == 0, "WOORT_CONST_TYPE_NIL should be 0.");

            if (!woort_vector_emplace_back(
                &code_env_instance->m_const_records,
                constant_and_static_storage_count,
                &buffer))
            {
                /* OOM: 不影响正常运行，但序列化将失败 */
                WOORT_DEBUG("Out of memory filling const_records.");
            }
            else
            {
                memset(buffer, 0, sizeof(woort_ConstRecord) * constant_and_static_storage_count);
            }
        }

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

void woort_CodeEnv_drop_all(void)
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
                woort_CodeEnv_drop(code_env);
        }
        woort_CodeEnv_unlock(code_env);
    }

    woort_rwspinlock_read_unlock(
        &_codeenv_global_ctx->m_codeenvs_lock);
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
        if (woort_hashmap_find(&env->m_trap_records, &code, (void**)&value_addr))
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
        if (woort_hashmap_find(&env->m_trap_records, &code, (void**)&value_addr))
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

WOORT_NODISCARD bool woort_CodeEnv_set_const_record(
    woort_CodeEnv* env,
    woort_IRConstantIndex cidx,
    woort_ConstRecordType type,
    /* OPTIONAL */ const char* lib_name,
    /* OPTIONAL */ const char* func_name)
{
    assert(env != NULL);
    assert((size_t)cidx < env->m_data_count);
    assert((size_t)cidx < env->m_const_records.m_size);

    woort_ConstRecord* rec = (woort_ConstRecord*)woort_vector_at(
        &env->m_const_records, cidx);

    rec->m_type = type;

    /* 释放旧名称 */
    free(rec->m_lib_name);
    free(rec->m_func_name);
    rec->m_lib_name = NULL;
    rec->m_func_name = NULL;

    if (lib_name != NULL)
    {
        size_t len = strlen(lib_name);
        rec->m_lib_name = (char*)malloc(len + 1);
        if (rec->m_lib_name == NULL)
            return false;
        memcpy(rec->m_lib_name, lib_name, len + 1);
    }

    if (func_name != NULL)
    {
        size_t len = strlen(func_name);
        rec->m_func_name = (char*)malloc(len + 1);
        if (rec->m_func_name == NULL)
        {
            free(rec->m_lib_name);
            rec->m_lib_name = NULL;
            return false;
        }
        memcpy(rec->m_func_name, func_name, len + 1);
    }

    return true;
}

void woort_CodeEnv_foreach(
    woort_CodeEnv_ForeachCallback callback,
    void* user_data)
{
    woort_rwspinlock_read_lock(&_codeenv_global_ctx->m_codeenvs_lock);

    const size_t count = _codeenv_global_ctx->m_codeenvs.m_size;
    for (size_t i = 0; i < count; ++i)
    {
        woort_CodeEnv* const code_env =
            *(woort_CodeEnv**)woort_vector_at(
                &_codeenv_global_ctx->m_codeenvs, i);

        if (!callback(code_env, user_data))
            break;
    }

    woort_rwspinlock_read_unlock(&_codeenv_global_ctx->m_codeenvs_lock);
}

/* ========================================================================
 * 二进制序列化 / 反序列化
 * ======================================================================== */

 /*
  * 二进制格式版本号与魔数。
  */
#define WOORT_CODEENV_BINARY_MAGIC   0x30314345u  /* "EC10" */
#define WOORT_CODEENV_BINARY_VERSION 1u

  /*
   * 常量类型标签（与 woort_ConstRecordType 一一对应，确保稳定）。
   */
enum {
    _WOORT_BIN_TYPE_NIL = 0,
    _WOORT_BIN_TYPE_INT = 1,
    _WOORT_BIN_TYPE_REAL = 2,
    _WOORT_BIN_TYPE_STRING = 3,
    _WOORT_BIN_TYPE_SCRIPT_FUNC = 4,
    _WOORT_BIN_TYPE_EXTERN_FUNC = 5,
    _WOORT_BIN_TYPE_SCRIPT_CLOSURE = 6,
    _WOORT_BIN_TYPE_EXTERN_CLOSURE = 7,
    _WOORT_BIN_TYPE_BOX_INT = 8,
    _WOORT_BIN_TYPE_BOX_REAL = 9,
    _WOORT_BIN_TYPE_BOX_BOOL = 10,
    _WOORT_BIN_TYPE_STRUCT = 11,
};

_Static_assert(WOORT_CONST_TYPE_NIL == 0, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_INT == 1, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_REAL == 2, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_STRING == 3, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_SCRIPT_FUNC == 4, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_EXTERN_FUNC == 5, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_SCRIPT_CLOSURE == 6, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_EXTERN_CLOSURE == 7, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_BOX_INT == 8, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_BOX_REAL == 9, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_BOX_BOOL == 10, "type tag mismatch");
_Static_assert(WOORT_CONST_TYPE_STRUCT == 11, "type tag mismatch");

/* ================================================================
 * 序列化期间的字符串池（本地使用）
 * ================================================================ */

typedef struct _CodeEnvBinStrPool
{
    woort_Vector m_strings;   /* char* 列表 */
    woort_Vector m_data;      /* 所有字符串数据拼接 */
} _CodeEnvBinStrPool;

static void _bin_strpool_init(_CodeEnvBinStrPool* sp)
{
    woort_vector_init(&sp->m_strings, sizeof(char*));
    woort_vector_init(&sp->m_data, sizeof(char));
}

static void _bin_strpool_deinit(_CodeEnvBinStrPool* sp)
{
    char** strs = (char**)sp->m_strings.m_data;
    for (size_t i = 0; i < sp->m_strings.m_size; ++i)
        free(strs[i]);
    woort_vector_deinit(&sp->m_strings);
    woort_vector_deinit(&sp->m_data);
}

/*
 * 插入字符串，返回其在 m_data 中的偏移量。
 * 如果字符串已存在，返回已有偏移量。
 */
static bool _bin_strpool_insert(
    _CodeEnvBinStrPool* sp,
    /* OPTIONAL */ const char* str,
    /* OPTIONAL */ uint32_t* out_offset)
{
    if (str == NULL)
    {
        if (out_offset != NULL)
            *out_offset = UINT32_MAX;
        return true;
    }

    /* 查找是否已存在 */
    size_t existing_count = sp->m_strings.m_size;
    for (size_t i = 0; i < existing_count; ++i)
    {
        char* existing = ((char**)sp->m_strings.m_data)[i];
        if (strcmp(existing, str) == 0)
        {
            /* 找到：返回已有偏移量 */
            uint32_t off = 0;
            for (size_t j = 0; j < i; ++j)
                off += (uint32_t)strlen(((char**)sp->m_strings.m_data)[j]) + 1;

            if (out_offset != NULL)
                *out_offset = off;
            return true;
        }
    }

    /* 新字符串：复制并追加 */
    size_t len = strlen(str);
    char* copy = (char*)malloc(len + 1);
    if (copy == NULL)
        return false;
    memcpy(copy, str, len + 1);

    uint32_t data_offset = (uint32_t)sp->m_data.m_size;

    if (!woort_vector_push_back(&sp->m_strings, 1u, &copy))
    {
        free(copy);
        return false;
    }

    if (!woort_vector_push_back(&sp->m_data, len + 1, copy))
    {
        free(copy);
        sp->m_strings.m_size--;
        return false;
    }

    if (out_offset != NULL)
        *out_offset = data_offset;
    return true;
}

/*
 * 从字符串池读取字符串。
 * data: 池数据区起始地址。
 * offset: 字符串在池中的偏移。
 * 返回指向池数据区的稳定指针，或 NULL。
 */
static /* OPTIONAL */ const char* _bin_strpool_get(
    const char* data, uint32_t offset)
{
    if (offset == UINT32_MAX)
        return NULL;
    return data + offset;
}

/* ================================================================
 * 序列化辅助：缓冲写入器
 * ================================================================ */

typedef struct _BinWriter
{
    woort_Vector m_buf;  /* unsigned char */
    size_t m_pos;
} _BinWriter;

static void _bin_writer_init(_BinWriter* w)
{
    woort_vector_init(&w->m_buf, sizeof(unsigned char));
    w->m_pos = 0;
}

static void _bin_writer_deinit(_BinWriter* w)
{
    woort_vector_deinit(&w->m_buf);
}

static bool _bin_write_raw(_BinWriter* w, const void* data, size_t len)
{
    if (len == 0)
        return true;
    return woort_vector_push_back(&w->m_buf, len, data);
}

static bool _bin_write_u8(_BinWriter* w, uint8_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_u32(_BinWriter* w, uint32_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_u64(_BinWriter* w, uint64_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_i64(_BinWriter* w, int64_t v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static bool _bin_write_f64(_BinWriter* w, double v)
{
    return _bin_write_raw(w, &v, sizeof(v));
}

static void* _bin_writer_detach(_BinWriter* w, size_t* out_len)
{
    *out_len = w->m_buf.m_size;
    void* data = malloc(w->m_buf.m_size);
    if (data != NULL && w->m_buf.m_size > 0)
        memcpy(data, w->m_buf.m_data, w->m_buf.m_size);
    return data;
}

/* ================================================================
 * 反序列化辅助：缓冲读取器
 * ================================================================ */

typedef struct _BinReader
{
    const unsigned char* m_data;
    size_t m_size;
    size_t m_pos;
} _BinReader;

static bool _bin_reader_init_memory(
    _BinReader* r, const void* data, size_t size)
{
    r->m_data = (const unsigned char*)data;
    r->m_size = size;
    r->m_pos = 0;
    return true;
}

static bool _bin_read_raw(_BinReader* r, void* out, size_t len)
{
    if (r->m_pos + len > r->m_size)
        return false;
    memcpy(out, r->m_data + r->m_pos, len);
    r->m_pos += len;
    return true;
}

static bool _bin_read_u8(_BinReader* r, uint8_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_u32(_BinReader* r, uint32_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_u64(_BinReader* r, uint64_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_i64(_BinReader* r, int64_t* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

static bool _bin_read_f64(_BinReader* r, double* out)
{
    return _bin_read_raw(r, out, sizeof(*out));
}

/*
 * Hashmap foreach 回调上下文和回调函数。
 * woort_HashMap 没有迭代器，必须使用 foreach 模式。
 */

 /* 用于字符串池构建阶段，收集 extern constants 的 key 字符串 */
struct _SaveStrPoolCtx {
    _CodeEnvBinStrPool* m_sp;
    bool* m_ok;
};

static bool _save_strpool_add_key(
    const void* key, void* value, void* user_data)
{
    (void)value;
    struct _SaveStrPoolCtx* ctx = (struct _SaveStrPoolCtx*)user_data;
    *ctx->m_ok = *ctx->m_ok
        && _bin_strpool_insert(ctx->m_sp, *(const char**)key, NULL);
    return true;
}

/* 用于写入 extern 常量映射表 */
struct _WriteExternConstCtx {
    _CodeEnvBinStrPool* m_sp;
    _BinWriter* m_w;
    bool* m_ok;
};

static bool _write_extern_const(
    const void* key, void* value, void* user_data)
{
    struct _WriteExternConstCtx* ctx =
        (struct _WriteExternConstCtx*)user_data;
    uint32_t name_off;
    *ctx->m_ok = *ctx->m_ok
        && _bin_strpool_insert(ctx->m_sp, *(const char**)key, &name_off);
    *ctx->m_ok = *ctx->m_ok && _bin_write_u32(ctx->m_w, name_off);
    *ctx->m_ok = *ctx->m_ok
        && _bin_write_u32(ctx->m_w, *(woort_IRConstantIndex*)value);
    return *ctx->m_ok;
}

/* 用于写入 trap 记录表 */
struct _WriteTrapCtx {
    const woort_Bytecode* m_code_begin;
    _BinWriter* m_w;
    bool* m_ok;
};

static bool _write_trap(
    const void* key, void* value, void* user_data)
{
    struct _WriteTrapCtx* ctx = (struct _WriteTrapCtx*)user_data;
    const woort_Bytecode* addr = *(const woort_Bytecode**)key;
    woort_Bytecode orig = *(woort_Bytecode*)value;
    uint32_t off = (uint32_t)(addr - ctx->m_code_begin);
    *ctx->m_ok = *ctx->m_ok && _bin_write_u32(ctx->m_w, off);
    *ctx->m_ok = *ctx->m_ok && _bin_write_u32(ctx->m_w, orig);
    return *ctx->m_ok;
}

/* ================================================================
 * 序列化主函数
 * ================================================================ */

WOORT_NODISCARD bool woort_CodeEnv_save_binary(
    woort_CodeEnv* code_env, void** out_buffer, size_t* out_len)
{
    assert(code_env != NULL);
    assert(out_buffer != NULL);
    assert(out_len != NULL);

    *out_buffer = NULL;
    *out_len = 0;

    woort_CodeEnv_lock(code_env);

    _BinWriter w;
    _bin_writer_init(&w);

    _CodeEnvBinStrPool strpool;
    _bin_strpool_init(&strpool);

    bool ok = true;

    const size_t code_size = (size_t)(code_env->m_code_end - code_env->m_code_begin);
    const size_t data_count = code_env->m_data_count;

    /*
     * 写入头部。
     */
    ok = ok && _bin_write_u32(&w, WOORT_CODEENV_BINARY_MAGIC);
    ok = ok && _bin_write_u32(&w, WOORT_CODEENV_BINARY_VERSION);
    ok = ok && _bin_write_u64(&w, (uint64_t)code_size);
    ok = ok && _bin_write_u64(&w, (uint64_t)data_count);

    /*
     * 写入字节码。
     */
    if (code_size > 0)
        ok = ok && _bin_write_raw(&w, code_env->m_code_begin, code_size * sizeof(woort_Bytecode));

    /*
     * 串池阶段：遍历所有需要写入的字符串，建立字符串池。
     */

     /* 遍历常量数据，收集字符串/库名/函数名 */
    for (size_t i = 0; ok && i < data_count; ++i)
    {
        const woort_ConstRecord* rec = (const woort_ConstRecord*)woort_vector_at(
            &code_env->m_const_records, i);

        switch (rec->m_type)
        {
        case WOORT_CONST_TYPE_STRING:
        {
            const woort_GCString* gcs = code_env->m_data_begin[i].m_string;
            if (gcs != NULL)
                ok = ok && _bin_strpool_insert(&strpool, gcs->m_content, NULL);
            break;
        }
        case WOORT_CONST_TYPE_EXTERN_FUNC:
        case WOORT_CONST_TYPE_EXTERN_CLOSURE:
        {
            const char* lib_name = rec->m_lib_name;
            const char* func_name = rec->m_func_name;

            /* 如果 record 中没有登记库名/函数名，尝试从 dylib 运行时解析 */
            if (lib_name == NULL || func_name == NULL)
            {
                woort_NativeFunction nf;
                if (rec->m_type == WOORT_CONST_TYPE_EXTERN_CLOSURE)
                {
                    const woort_GCClosure* closure =
                        code_env->m_data_begin[i].m_closure;
                    if (closure == NULL || closure->m_script_function != NULL)
                    {
                        /* 闭包没有原生函数指针，无法解析 */
                        WOORT_DEBUG("CodeEnv save: cannot resolve extern closure names for const %zu.", i);
                        ok = false;
                        break;
                    }
                    nf = closure->m_native_function;
                }
                else
                {
                    nf = code_env->m_data_begin[i].m_native_function;
                    if (nf == NULL)
                    {
                        WOORT_DEBUG("CodeEnv save: null extern function for const %zu.", i);
                        ok = false;
                        break;
                    }
                }

                /* 尝试从 dylib 注册表查找库和函数名 */
                woort_Dylib* found_lib = NULL;
                if (!woort_Dylib_find_by_resolved_func((void*)nf, &found_lib)
                    || found_lib == NULL)
                {
                    WOORT_DEBUG("CodeEnv save: extern function not found in any loaded library for const %zu.", i);
                    ok = false;
                    break;
                }

                lib_name = found_lib->m_name;

                const char* resolved_name = NULL;
                if (!woort_Dylib_get_function_name(found_lib, (void*)nf, &resolved_name)
                    || resolved_name == NULL)
                {
                    WOORT_DEBUG("CodeEnv save: cannot resolve function name for const %zu.", i);
                    ok = false;
                    break;
                }
                func_name = resolved_name;
            }

            ok = ok && _bin_strpool_insert(&strpool, lib_name, NULL);
            ok = ok && _bin_strpool_insert(&strpool, func_name, NULL);
            break;
        }
        default:
            break;
        }
    }

    /* 遍历 extern constants 名称 */
    if (ok)
    {
        struct _SaveStrPoolCtx ctx = { &strpool, &ok };
        (void)woort_hashmap_foreach(&code_env->m_extern_constants,
            &_save_strpool_add_key, &ctx);
    }

    /* 遍历 extern libs 名称 */
    if (ok)
    {
        for (size_t i = 0; i < code_env->m_extern_libs.m_size; ++i)
        {
            woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(
                &code_env->m_extern_libs, i);
            ok = ok && _bin_strpool_insert(&strpool, lib->m_name, NULL);
        }
    }

    /* 遍历函数边界名称 */
    if (ok)
    {
        for (size_t i = 0; i < code_env->m_function_boundaries.m_size; ++i)
        {
            const woort_FunctionBoundary* fb =
                (const woort_FunctionBoundary*)woort_vector_at(
                    &code_env->m_function_boundaries, i);
            if (fb->m_name != NULL)
                ok = ok && _bin_strpool_insert(&strpool, fb->m_name, NULL);
        }
    }

    /* 遍历源码映射文件路径 */
    if (ok)
    {
        for (uint32_t i = 0; i < code_env->m_source_map.m_entry_count; ++i)
        {
            const woort_SourceMap_Entry* entry = &code_env->m_source_map.m_entries[i];
            if (entry->m_location.m_filepath != NULL)
                ok = ok && _bin_strpool_insert(&strpool, entry->m_location.m_filepath, NULL);
        }
    }

    /*
     * 写入字符串池。
     */
    ok = ok && _bin_write_u64(&w, (uint64_t)strpool.m_data.m_size);
    if (ok && strpool.m_data.m_size > 0)
        ok = ok && _bin_write_raw(&w, strpool.m_data.m_data, strpool.m_data.m_size);

    /*
     * 写入常量数据。
     */
    for (size_t i = 0; ok && i < data_count; ++i)
    {
        const woort_ConstRecord* rec = (const woort_ConstRecord*)woort_vector_at(
            &code_env->m_const_records, i);
        const woort_Value* val = &code_env->m_data_begin[i];

        ok = ok && _bin_write_u8(&w, (uint8_t)rec->m_type);

        switch (rec->m_type)
        {
        case WOORT_CONST_TYPE_NIL:
            break;

        case WOORT_CONST_TYPE_INT:
            ok = ok && _bin_write_i64(&w, val->m_integer);
            break;

        case WOORT_CONST_TYPE_REAL:
            ok = ok && _bin_write_f64(&w, val->m_real);
            break;

        case WOORT_CONST_TYPE_STRING:
        {
            uint32_t off;
            const woort_GCString* gcs = val->m_string;
            if (gcs != NULL)
            {
                ok = ok && _bin_strpool_insert(&strpool, gcs->m_content, &off);
                ok = ok && _bin_write_u32(&w, off);
            }
            else
            {
                ok = ok && _bin_write_u32(&w, UINT32_MAX);
            }
            break;
        }

        case WOORT_CONST_TYPE_SCRIPT_FUNC:
        {
            /* 保存为相对于 m_code_begin 的偏移量 */
            uint32_t off = (val->m_script_function != NULL)
                ? (uint32_t)(val->m_script_function - code_env->m_code_begin)
                : UINT32_MAX;
            ok = ok && _bin_write_u32(&w, off);
            break;
        }

        case WOORT_CONST_TYPE_EXTERN_FUNC:
        case WOORT_CONST_TYPE_EXTERN_CLOSURE:
        {
            /* 使用 record 中的名字，或运行时解析（与字符串池收集阶段相同逻辑）*/
            const char* lib_name = rec->m_lib_name;
            const char* func_name = rec->m_func_name;

            if (lib_name == NULL || func_name == NULL)
            {
                woort_NativeFunction nf;
                if (rec->m_type == WOORT_CONST_TYPE_EXTERN_CLOSURE)
                {
                    const woort_GCClosure* closure = val->m_closure;
                    if (closure != NULL && closure->m_script_function == NULL)
                        nf = closure->m_native_function;
                    else
                        nf = NULL;
                }
                else
                {
                    nf = val->m_native_function;
                }

                if (nf != NULL)
                {
                    woort_Dylib* found_lib = NULL;
                    if (!woort_Dylib_find_by_resolved_func((void*)nf, &found_lib)
                        || found_lib == NULL)
                    {
                        ok = false;
                        break;
                    }
                    lib_name = found_lib->m_name;
                    if (!woort_Dylib_get_function_name(found_lib, (void*)nf, &func_name))
                    {
                        ok = false;
                        break;
                    }
                }
                else
                {
                    ok = false;
                    break;
                }
            }

            uint32_t lib_off, func_off;
            ok = ok && _bin_strpool_insert(&strpool, lib_name, &lib_off);
            ok = ok && _bin_strpool_insert(&strpool, func_name, &func_off);
            ok = ok && _bin_write_u32(&w, lib_off);
            ok = ok && _bin_write_u32(&w, func_off);
            break;
        }

        case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
        {
            /* 闭包：记录脚本函数偏移 */
            const woort_GCClosure* closure = val->m_closure;
            uint32_t off = UINT32_MAX;
            if (closure != NULL && closure->m_script_function != NULL)
                off = (uint32_t)(closure->m_script_function - code_env->m_code_begin);
            ok = ok && _bin_write_u32(&w, off);
            break;
        }

        case WOORT_CONST_TYPE_BOX_INT:
            ok = ok && _bin_write_i64(&w,
                _woort_unbox_int64((woort_BoxedInt62)val->m_dynamic.m_boxed));
            break;

        case WOORT_CONST_TYPE_BOX_REAL:
            ok = ok && _bin_write_f64(&w,
                _woort_unbox_float64((woort_BoxedFloat63)val->m_dynamic.m_boxed));
            break;

        case WOORT_CONST_TYPE_BOX_BOOL:
            ok = ok && _bin_write_u8(&w,
                _woort_unbox_bool(val->m_dynamic.m_boxed) ? 1 : 0);
            break;

        case WOORT_CONST_TYPE_STRUCT:
        {
            woort_GCStruct* s = val->m_struct;
            uint32_t member_count = (s != NULL) ? (uint32_t)s->m_size : 0;
            ok = ok && _bin_write_u32(&w, member_count);
            for (uint32_t mi = 0; ok && mi < member_count; ++mi)
            {
                /*
                 * 在常量池中查找与 s->m_datas[mi] 值相等的常量索引。
                 * struct 的成员值是 m_data_begin[original_index] 的副本，
                 * 通过值与类型匹配可找到原始常量索引。
                 */
                const woort_Value* mv = &s->m_datas[mi];
                woort_IRConstantIndex found_idx = (woort_IRConstantIndex)mi; /* fallback */
                bool found = false;

                for (size_t j = 0; j < data_count; ++j)
                {
                    const woort_ConstRecord* rec_j =
                        (const woort_ConstRecord*)woort_vector_at(
                            &code_env->m_const_records, j);
                    const woort_Value* cv = &code_env->m_data_begin[j];

                    switch (rec_j->m_type)
                    {
                    case WOORT_CONST_TYPE_NIL:
                        if (mv->m_gcinstance == cv->m_gcinstance)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_INT:
                        if (mv->m_integer == cv->m_integer)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_REAL:
                        if (mv->m_real == cv->m_real)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_STRING:
                        if (mv->m_gcinstance == cv->m_gcinstance)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_SCRIPT_FUNC:
                        if (mv->m_script_function == cv->m_script_function)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_EXTERN_FUNC:
                        if (mv->m_native_function == cv->m_native_function)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
                    case WOORT_CONST_TYPE_EXTERN_CLOSURE:
                        if (mv->m_gcinstance == cv->m_gcinstance)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_BOX_INT:
                    case WOORT_CONST_TYPE_BOX_REAL:
                    case WOORT_CONST_TYPE_BOX_BOOL:
                        if (mv->m_dynamic.m_boxed == cv->m_dynamic.m_boxed)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    case WOORT_CONST_TYPE_STRUCT:
                        if (mv->m_gcinstance == cv->m_gcinstance)
                        { found_idx = (woort_IRConstantIndex)j; found = true; }
                        break;
                    default:
                        break;
                    }
                    if (found)
                        break;
                }
                ok = ok && _bin_write_u32(&w, (uint32_t)found_idx);
            }
            break;
        }

        default:
            WOORT_DEBUG("CodeEnv save: unknown const type %d at %zu.", (int)rec->m_type, i);
            ok = false;
            break;
        }
    }

    /*
     * 写入 extern 常量映射表。
     */
    {
        /* 先计数 */
        uint64_t extern_count = (uint64_t)code_env->m_extern_constants.m_size;
        ok = ok && _bin_write_u64(&w, extern_count);

        if (ok && extern_count > 0)
        {
            struct _WriteExternConstCtx ctx = { &strpool, &w, &ok };
            (void)woort_hashmap_foreach(&code_env->m_extern_constants,
                &_write_extern_const, &ctx);
        }
    }

    /*
     * 写入外部库列表。
     */
    {
        uint64_t lib_count = (uint64_t)code_env->m_extern_libs.m_size;
        ok = ok && _bin_write_u64(&w, lib_count);
        for (size_t i = 0; ok && i < code_env->m_extern_libs.m_size; ++i)
        {
            woort_Dylib* lib = *(woort_Dylib**)woort_vector_at(
                &code_env->m_extern_libs, i);
            uint32_t name_off;
            ok = ok && _bin_strpool_insert(&strpool, lib->m_name, &name_off);
            ok = ok && _bin_write_u32(&w, name_off);
        }
    }

    /*
     * 写入函数边界表。
     */
    {
        uint64_t fb_count = (uint64_t)code_env->m_function_boundaries.m_size;
        ok = ok && _bin_write_u64(&w, fb_count);
        for (size_t i = 0; ok && i < code_env->m_function_boundaries.m_size; ++i)
        {
            const woort_FunctionBoundary* fb =
                (const woort_FunctionBoundary*)woort_vector_at(
                    &code_env->m_function_boundaries, i);
            ok = ok && _bin_write_u32(&w, fb->m_offset_begin);
            ok = ok && _bin_write_u32(&w, fb->m_code_length);

            uint32_t name_off;
            ok = ok && _bin_strpool_insert(&strpool, fb->m_name, &name_off);
            ok = ok && _bin_write_u32(&w, name_off);
        }
    }

    /*
     * 写入源码映射表。
     */
    {
        uint64_t sm_count = (uint64_t)code_env->m_source_map.m_entry_count;
        ok = ok && _bin_write_u64(&w, sm_count);
        for (uint32_t i = 0; ok && i < code_env->m_source_map.m_entry_count; ++i)
        {
            const woort_SourceMap_Entry* entry = &code_env->m_source_map.m_entries[i];
            ok = ok && _bin_write_u32(&w, entry->m_bytecode_offset);

            uint32_t fp_off;
            ok = ok && _bin_strpool_insert(&strpool, entry->m_location.m_filepath, &fp_off);
            ok = ok && _bin_write_u32(&w, fp_off);

            ok = ok && _bin_write_u32(&w, entry->m_location.m_begin_line);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_begin_column);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_end_line);
            ok = ok && _bin_write_u32(&w, entry->m_location.m_end_column);
        }
    }

    /*
     * 写入 trap 记录表。
     */
    {
        uint64_t trap_count = (uint64_t)code_env->m_trap_records.m_size;
        ok = ok && _bin_write_u64(&w, trap_count);

        if (ok && trap_count > 0)
        {
            struct _WriteTrapCtx ctx = { code_env->m_code_begin, &w, &ok };
            (void)woort_hashmap_foreach(&code_env->m_trap_records,
                &_write_trap, &ctx);
        }
    }

    if (ok)
    {
        *out_buffer = _bin_writer_detach(&w, out_len);
        if (*out_buffer == NULL)
            ok = false;
    }

    _bin_strpool_deinit(&strpool);
    _bin_writer_deinit(&w);
    woort_CodeEnv_unlock(code_env);

    return ok;
}

/* ================================================================
 * 反序列化主函数
 * ================================================================ */

WOORT_NODISCARD woort_CodeEnv_RestoreResult woort_CodeEnv_restore_binary(
    woort_VFile* f, woort_CodeEnv** out_code_env)
{
    woort_CodeEnv_RestoreResult result = WOORT_CODEENV_RESTORE_OK;

    assert(f != NULL);
    assert(out_code_env != NULL);

    *out_code_env = NULL;

    /* 读取整个文件到内存 */
    int64_t fsize_val = woort_vfile_size(f);
    if (fsize_val < 0)
        return WOORT_CODEENV_RESTORE_FAIL_READ;
    size_t fsize = (size_t)fsize_val;

    void* raw = malloc(fsize);
    if (raw == NULL)
        return WOORT_CODEENV_RESTORE_FAIL_ALLOC;

    size_t bytes_read = 0;
    if (!woort_vfile_seek(f, 0, SEEK_SET)
        || !woort_vfile_read(f, raw, fsize, &bytes_read)
        || bytes_read != fsize)
    {
        free(raw);
        return WOORT_CODEENV_RESTORE_FAIL_READ;
    }

    _BinReader r;
    _bin_reader_init_memory(&r, raw, fsize);

    woort_CodeEnv* cenv = NULL;

    /* 读取头部 */
    {
        uint32_t magic, version;
        uint64_t code_size, data_count;
        if (!_bin_read_u32(&r, &magic)
            || !_bin_read_u32(&r, &version)
            || !_bin_read_u64(&r, &code_size)
            || !_bin_read_u64(&r, &data_count))
        {
            WOORT_DEBUG("CodeEnv restore: truncated header.");
            result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
            goto _restore_fail_after_read_raw;
        }

        if (magic != WOORT_CODEENV_BINARY_MAGIC)
        {
            WOORT_DEBUG("CodeEnv restore: bad header magic=%08x ver=%u.", magic, version);
            result = WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH;
            goto _restore_fail_after_read_raw;
        }

        if (version != WOORT_CODEENV_BINARY_VERSION)
        {
            WOORT_DEBUG("CodeEnv restore: version mismatch ver=%u.", version);
            result = WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH;
            goto _restore_fail_after_read_raw;
        }

        /* 读取字节码（直接从二进制流读取，woort_CodeEnv_create 会复制）*/
        if (code_size > (r.m_size - r.m_pos) / sizeof(woort_Bytecode))
        {
            WOORT_DEBUG("CodeEnv restore: invalid code size %llu.", (unsigned long long)code_size);
            result = WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE;
            goto _restore_fail_after_read_raw;
        }

        const woort_Bytecode* codes_from_bin = NULL;
        if (code_size > 0)
        {
            codes_from_bin = (const woort_Bytecode*)(r.m_data + r.m_pos);
            r.m_pos += (size_t)code_size * sizeof(woort_Bytecode);
        }

        /* 创建 CodeEnv */
        if (!woort_CodeEnv_create(
                codes_from_bin,
                (size_t)code_size,
                (size_t)data_count,
                &cenv)
            || cenv == NULL)
        {
            result = WOORT_CODEENV_RESTORE_FAIL_CREATE_CODEENV;
            goto _restore_fail_after_read_raw;
        }
    }

    woort_CodeEnv_lock(cenv);

    /* 读取字符串池 */
    {
        uint64_t strpool_size;
        if (!_bin_read_u64(&r, &strpool_size))
        {
            result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
            goto _restore_fail_after_create;
        }

        const char* strpool_data = (strpool_size > 0) ? (const char*)(r.m_data + r.m_pos) : NULL;
        if (strpool_size > 0)
        {
            if (r.m_pos + strpool_size > r.m_size)
            {
                result = WOORT_CODEENV_RESTORE_FAIL_INVALID_STRPOOL;
                goto _restore_fail_after_create;
            }
            r.m_pos += (size_t)strpool_size;
        }

        /* 辅助宏：从字符串池中读取字符串指针 */
#define _RESTORE_STR(off) \
        ((off) == UINT32_MAX ? NULL : _bin_strpool_get(strpool_data, (off)))

        /*
         * 读取常量数据。
         */
        {
            size_t data_count = cenv->m_data_count;
            for (size_t i = 0; i < data_count; ++i)
            {
                uint8_t type;
                if (!_bin_read_u8(&r, &type))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                switch (type)
                {
                case _WOORT_BIN_TYPE_NIL:
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_NIL, NULL, NULL);
                    break;

                case _WOORT_BIN_TYPE_INT:
                {
                    int64_t v;
                    if (!_bin_read_i64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_int(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_INT, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_REAL:
                {
                    double v;
                    if (!_bin_read_f64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_real(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_REAL, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_STRING:
                {
                    uint32_t off;
                    if (!_bin_read_u32(&r, &off))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    const char* s = _RESTORE_STR(off);
                    woort_CodeEnv_set_const_string(cenv, (woort_IRConstantIndex)i,
                        s != NULL ? s : "");
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_STRING, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_SCRIPT_FUNC:
                {
                    uint32_t off;
                    if (!_bin_read_u32(&r, &off))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    const woort_Bytecode* addr = (off != UINT32_MAX && cenv->m_code_end > cenv->m_code_begin)
                        ? cenv->m_code_begin + off : NULL;
                    woort_CodeEnv_set_const_script_function(cenv, (woort_IRConstantIndex)i, addr);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_SCRIPT_FUNC, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_EXTERN_FUNC:
                {
                    uint32_t lib_off, func_off;
                    if (!_bin_read_u32(&r, &lib_off)
                        || !_bin_read_u32(&r, &func_off))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    const char* lib_name = _RESTORE_STR(lib_off);
                    const char* func_name = _RESTORE_STR(func_off);
                    if (lib_name == NULL || func_name == NULL)
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET;
                        goto _restore_fail_after_create;
                    }

                    /* 尝试解析 */
                    woort_Dylib* lib = woort_dylib_load(lib_name, lib_name, NULL, false);
                    if (lib == NULL)
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot load lib '%s'.", lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_NativeFunction nf = (woort_NativeFunction)woort_dylib_load_func(lib, func_name);
                    if (nf == NULL)
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot find func '%s' in lib '%s'.", func_name, lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_CodeEnv_set_const_extern_function(cenv, (woort_IRConstantIndex)i, nf);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_EXTERN_FUNC, lib_name, func_name);
                    (void)woort_CodeEnv_add_extern_lib(cenv, lib);
                    break;
                }

                case _WOORT_BIN_TYPE_SCRIPT_CLOSURE:
                {
                    uint32_t off;
                    if (!_bin_read_u32(&r, &off))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    const woort_Bytecode* addr = (off != UINT32_MAX && cenv->m_code_end > cenv->m_code_begin)
                        ? cenv->m_code_begin + off : NULL;
                    woort_CodeEnv_set_const_script_closure(cenv, (woort_IRConstantIndex)i, addr);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_SCRIPT_CLOSURE, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_EXTERN_CLOSURE:
                {
                    uint32_t lib_off, func_off;
                    if (!_bin_read_u32(&r, &lib_off)
                        || !_bin_read_u32(&r, &func_off))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    const char* lib_name = _RESTORE_STR(lib_off);
                    const char* func_name = _RESTORE_STR(func_off);
                    if (lib_name == NULL || func_name == NULL)
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET;
                        goto _restore_fail_after_create;
                    }

                    woort_Dylib* lib = woort_dylib_load(lib_name, lib_name, NULL, false);
                    if (lib == NULL)
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot load lib '%s'.", lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_NativeFunction nf = (woort_NativeFunction)woort_dylib_load_func(lib, func_name);
                    if (nf == NULL)
                    {
                        WOORT_DEBUG("CodeEnv restore: cannot find func '%s' in lib '%s'.", func_name, lib_name);
                        result = WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE;
                        goto _restore_fail_after_create;
                    }

                    woort_CodeEnv_set_const_extern_closure(cenv, (woort_IRConstantIndex)i, nf);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_EXTERN_CLOSURE, lib_name, func_name);
                    (void)woort_CodeEnv_add_extern_lib(cenv, lib);
                    break;
                }

                case _WOORT_BIN_TYPE_BOX_INT:
                {
                    int64_t v;
                    if (!_bin_read_i64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_box_int(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_BOX_INT, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_BOX_REAL:
                {
                    double v;
                    if (!_bin_read_f64(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_box_real(cenv, (woort_IRConstantIndex)i, v);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_BOX_REAL, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_BOX_BOOL:
                {
                    uint8_t v;
                    if (!_bin_read_u8(&r, &v))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }
                    woort_CodeEnv_set_const_box_bool(cenv, (woort_IRConstantIndex)i, v != 0);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_BOX_BOOL, NULL, NULL);
                    break;
                }

                case _WOORT_BIN_TYPE_STRUCT:
                {
                    uint32_t member_count;
                    if (!_bin_read_u32(&r, &member_count))
                    {
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    /* struct 成员索引表在常数池内按序读取 */
                    woort_IRConstantIndex* members = NULL;
                    if (member_count > 0)
                    {
                        members = (woort_IRConstantIndex*)malloc(
                            member_count * sizeof(woort_IRConstantIndex));
                        if (members == NULL)
                        {
                            result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                            goto _restore_fail_after_create;
                        }
                        for (uint32_t mi = 0; mi < member_count; ++mi)
                        {
                            uint32_t midx;
                            if (!_bin_read_u32(&r, &midx))
                            {
                                free(members);
                                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                                goto _restore_fail_after_create;
                            }
                            members[mi] = (woort_IRConstantIndex)midx;
                        }
                    }

                    woort_CodeEnv_set_const_struct(cenv, (woort_IRConstantIndex)i,
                        members, member_count);
                    (void)woort_CodeEnv_set_const_record(cenv, (woort_IRConstantIndex)i,
                        WOORT_CONST_TYPE_STRUCT, NULL, NULL);

                    free(members);
                    break;
                }

                default:
                    WOORT_DEBUG("CodeEnv restore: unknown const type %d at %zu.", (int)type, i);
                    result = WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE;
                    goto _restore_fail_after_create;
                }
            }
        }

        /*
         * 读取 extern 常量映射表。
         */
        {
            uint64_t extern_count;
            if (!_bin_read_u64(&r, &extern_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t ei = 0; ei < extern_count; ++ei)
            {
                uint32_t name_off;
                uint32_t cidx;
                if (!_bin_read_u32(&r, &name_off)
                    || !_bin_read_u32(&r, &cidx))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                const char* name = _RESTORE_STR(name_off);
                if (name != NULL)
                    (void)woort_CodeEnv_register_extern_constant(cenv, name, cidx);
            }
        }

        /*
         * 读取外部库列表。
         */
        {
            uint64_t lib_count;
            if (!_bin_read_u64(&r, &lib_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t li = 0; li < lib_count; ++li)
            {
                uint32_t name_off;
                if (!_bin_read_u32(&r, &name_off))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                const char* lib_name = _RESTORE_STR(name_off);
                if (lib_name != NULL)
                {
                    woort_Dylib* lib = woort_dylib_load(lib_name, lib_name, NULL, false);
                    if (lib != NULL)
                        (void)woort_CodeEnv_add_extern_lib(cenv, lib);
                }
            }
        }

        /*
         * 读取函数边界表。
         */
        {
            uint64_t fb_count;
            if (!_bin_read_u64(&r, &fb_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t fi = 0; fi < fb_count; ++fi)
            {
                woort_FunctionBoundary fb;
                uint32_t name_off;
                if (!_bin_read_u32(&r, &fb.m_offset_begin)
                    || !_bin_read_u32(&r, &fb.m_code_length)
                    || !_bin_read_u32(&r, &name_off))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                const char* name = _RESTORE_STR(name_off);
                /* intern into CodeEnv string pool for lifetime management */
                if (name != NULL)
                    name = woort_StringPool_intern(&cenv->m_srcloc_string_pool, name);
                fb.m_name = name;

                if (!woort_vector_push_back(
                        &cenv->m_function_boundaries, 1, &fb))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail_after_create;
                }
            }
        }

        /*
         * 读取源码映射表。
         */
        {
            uint64_t sm_count;
            if (!_bin_read_u64(&r, &sm_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            if (sm_count > 0)
            {
                woort_SourceMap_Entry* entries = (woort_SourceMap_Entry*)malloc(
                    sizeof(woort_SourceMap_Entry) * (size_t)sm_count);
                if (entries == NULL)
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_ALLOC;
                    goto _restore_fail_after_create;
                }

                for (uint64_t si = 0; si < sm_count; ++si)
                {
                    uint32_t fp_off;
                    if (!_bin_read_u32(&r, &entries[si].m_bytecode_offset)
                        || !_bin_read_u32(&r, &fp_off)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_begin_line)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_begin_column)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_end_line)
                        || !_bin_read_u32(&r, &entries[si].m_location.m_end_column))
                    {
                        free(entries);
                        result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                        goto _restore_fail_after_create;
                    }

                    const char* fp = _RESTORE_STR(fp_off);
                    if (fp != NULL)
                        fp = woort_StringPool_intern(&cenv->m_srcloc_string_pool, fp);
                    entries[si].m_location.m_filepath = fp;
                }

                free(cenv->m_source_map.m_entries);
                cenv->m_source_map.m_entries = entries;
                cenv->m_source_map.m_entry_count = (uint32_t)sm_count;
            }
        }

        /*
         * 读取 trap 记录表。
         */
        {
            uint64_t trap_count;
            if (!_bin_read_u64(&r, &trap_count))
            {
                result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                goto _restore_fail_after_create;
            }
            for (uint64_t ti = 0; ti < trap_count; ++ti)
            {
                uint32_t off, orig;
                if (!_bin_read_u32(&r, &off)
                    || !_bin_read_u32(&r, &orig))
                {
                    result = WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA;
                    goto _restore_fail_after_create;
                }

                if (cenv->m_code_end > cenv->m_code_begin
                    && (size_t)off < (size_t)(cenv->m_code_end - cenv->m_code_begin))
                {
                    /* 注意：trap 记录在恢复时仅仅存下来，不激活（因为可能字节码已变） */
                    (void)woort_hashmap_insert(
                        &cenv->m_trap_records,
                        &cenv->m_code_begin[off],
                        &orig);
                }
            }
        }
}

    woort_CodeEnv_unlock(cenv);
    free(raw);

    *out_code_env = cenv;
    return WOORT_CODEENV_RESTORE_OK;

_restore_fail_after_create:
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_drop(cenv);
    free(raw);
    return result;

_restore_fail_after_read_raw:
    free(raw);
    return result;
}

WOORT_NODISCARD const char* woort_CodeEnv_restore_failed_desc(
    woort_CodeEnv_RestoreResult rt)
{
    switch (rt)
    {
    case WOORT_CODEENV_RESTORE_OK:
        return "OK";
    case WOORT_CODEENV_RESTORE_FAIL_READ:
        return "Failed to restore binary: read error.";
    case WOORT_CODEENV_RESTORE_FAIL_ALLOC:
        return "Failed to restore binary: out of memory.";
    case WOORT_CODEENV_RESTORE_FAIL_MAGIC_DOESNT_MATCH:
        return "Failed to restore binary: bad magic number.";
    case WOORT_CODEENV_RESTORE_FAIL_VERSION_DOESNT_MATCH:
        return "Failed to restore binary: unsupported binary version.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_CODE_SIZE:
        return "Failed to restore binary: invalid code size.";
    case WOORT_CODEENV_RESTORE_FAIL_CREATE_CODEENV:
        return "Failed to restore binary: internal creation failure.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_STRPOOL:
        return "Failed to restore binary: invalid string pool.";
    case WOORT_CODEENV_RESTORE_FAIL_TRUNCATED_DATA:
        return "Failed to restore binary: truncated data.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_CONST_TYPE:
        return "Failed to restore binary: unknown constant type.";
    case WOORT_CODEENV_RESTORE_FAIL_INVALID_OFFSET:
        return "Failed to restore binary: invalid pool offset.";
    case WOORT_CODEENV_RESTORE_FAIL_EXTERN_RESOLVE:
        return "Failed to restore binary: cannot resolve external symbol.";
    default:
        return "Failed to restore binary: unknown error.";
    }
}

#undef _RESTORE_STR
