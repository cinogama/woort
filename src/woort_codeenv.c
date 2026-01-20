#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

#include "woort_codeenv.h"
#include "woort_spin.h"
#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_log.h"

static struct _woort_CodeEnv_GlobalCtx
{
    woort_RWSpinlock    m_codeenvs_lock;
    woort_Vector /* woort_CodeEnv* */
                        m_codeenvs; 

} *_codeenv_global_ctx = NULL;

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

struct woort_CodeEnv
{
    woort_AtomicSize m_refcount;

    const woort_Bytecode* m_code_begin;
    const woort_Bytecode* m_code_end;

    woort_Value* m_constant_and_static_storage;
    size_t       m_constant_and_static_storage_count;
};

WOORT_NODISCARD bool woort_CodeEnv_create(
    woort_Vector* /* woort_Bytecode */ moving_bytecodes,
    woort_Vector* /* woort_Value */ moving_constants,
    size_t static_storage_count,
    woort_CodeEnv** out_code_env)
{
    if (!woort_vector_resize(
        moving_constants,
        moving_constants->m_element_size + static_storage_count))
    {
        // Out of memory.
        return false;
    }

    woort_CodeEnv* code_env_instance =
        malloc(sizeof(woort_CodeEnv));

    if (code_env_instance == NULL)
    {
        WOORT_DEBUG("Out of memory");
        return false;
    }

    woort_atomic_store_explicit(
        &code_env_instance->m_refcount,
        1,
        WOORT_ATOMIC_MEMORY_ORDER_RELEASE);

    size_t code_count;
    code_env_instance->m_code_begin =
        woort_vector_move_out(moving_bytecodes, &code_count);

    code_env_instance->m_code_end =
        code_env_instance->m_code_begin + code_count;

    code_env_instance->m_constant_and_static_storage =
        woort_vector_move_out(
            moving_constants,
            &code_env_instance->m_constant_and_static_storage_count);

    // 将新创建的 CodeEnv 注册到全局容器
    woort_rwspinlock_write_lock(&_codeenv_global_ctx->m_codeenvs_lock);
    bool register_result = woort_vector_push_back(
        &_codeenv_global_ctx->m_codeenvs,
        1,
        &code_env_instance);
    woort_rwspinlock_write_unlock(&_codeenv_global_ctx->m_codeenvs_lock);

    if (!register_result)
    {
        // Out of memory.
        woort_CodeEnv_unshare(code_env_instance);
        return false;
    }

    *out_code_env = code_env_instance;
    return true;
}

void woort_CodeEnv_share(woort_CodeEnv* code_env)
{
    woort_atomic_fetch_add_explicit(
        &code_env->m_refcount,
        1,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
}

void _woort_CodeEnv_destroy(woort_CodeEnv* code_env)
{
    // 先从全局容器中移除该 CodeEnv
    woort_rwspinlock_write_lock(&_codeenv_global_ctx->m_codeenvs_lock);
    
    size_t count = _codeenv_global_ctx->m_codeenvs.m_size;
    for (size_t i = 0; i < count; ++i)
    {
        woort_CodeEnv** ptr = (woort_CodeEnv**)woort_vector_at(
            &_codeenv_global_ctx->m_codeenvs, i);
        
        if (*ptr == code_env)
        {
            // 找到目标，使用 erase_at 删除
            woort_vector_erase_at(&_codeenv_global_ctx->m_codeenvs, i);
            break;
        }
    }
    
    woort_rwspinlock_write_unlock(&_codeenv_global_ctx->m_codeenvs_lock);

    // 释放 CodeEnv 占用的资源
    free((void*)code_env->m_code_begin);
    free(code_env->m_constant_and_static_storage);
    free(code_env);
}

void woort_CodeEnv_unshare(woort_CodeEnv* code_env)
{
    if (1 == woort_atomic_fetch_sub_explicit(
        &code_env->m_refcount,
        1,
        WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL))
    {
        // Drop if last owner released.
        _woort_CodeEnv_destroy(code_env);
    }
}

WOORT_NODISCARD bool woort_CodeEnv_find(
    const woort_Bytecode* addr, woort_CodeEnv** out_code_env)
{
    // 获取读锁，允许多线程并发查找
    woort_rwspinlock_read_lock(&_codeenv_global_ctx->m_codeenvs_lock);
    
    size_t count = _codeenv_global_ctx->m_codeenvs.m_size;
    for (size_t i = 0; i < count; ++i)
    {
        woort_CodeEnv** ptr = (woort_CodeEnv**)woort_vector_at(
            &_codeenv_global_ctx->m_codeenvs, i);
        
        woort_CodeEnv* code_env = *ptr;
        
        // 检查地址是否在该 CodeEnv 的代码区间内
        if (addr >= code_env->m_code_begin && addr < code_env->m_code_end)
        {
            *out_code_env = code_env;
            woort_rwspinlock_read_unlock(&_codeenv_global_ctx->m_codeenvs_lock);
            return true;
        }
    }
    
    woort_rwspinlock_read_unlock(&_codeenv_global_ctx->m_codeenvs_lock);
    return false;
}
