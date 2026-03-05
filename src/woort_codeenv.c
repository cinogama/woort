#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>

#include "woort_codeenv.h"
#include "woort_spin.h"
#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_log.h"

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

    woort_CodeEnv* code_env_instance =
        woort_GCUnit_alloc_attrib(
            AF,
            sizeof(woort_CodeEnv)
            + constant_and_static_storage_count * sizeof(woort_Value)
            + bytecodes_count * sizeof(woort_Bytecode));

    bool register_result = false;

    if (code_env_instance != NULL)
    {
        code_env_instance->m_gc_unit.m_proxy =
            &_codeenv_global_ctx->m_proxy;

        code_env_instance->m_hold = true;
        code_env_instance->m_data_begin =
            (woort_Value*)(code_env_instance + 1);

        code_env_instance->m_code_begin =
            (woort_Bytecode*)(
                code_env_instance->m_data_begin
                + constant_and_static_storage_count);
        code_env_instance->m_code_end =
            code_env_instance->m_code_begin
            + bytecodes_count;

        memcpy(
            code_env_instance->m_code_begin,
            bytecodes,
            bytecodes_count * sizeof(woort_Bytecode));

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

    *out_code_env = code_env_instance;
    return true;
}

void woort_CodeEnv_drop(
    woort_CodeEnv* code_env)
{
    assert(code_env->m_hold);
    code_env->m_hold = false;
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

        if (code_env->m_hold)
        {
            // Mark this code.
            woomem_mark_unit(code_env);
        }
    }

    woort_rwspinlock_read_unlock(
        &_codeenv_global_ctx->m_codeenvs_lock);
}
