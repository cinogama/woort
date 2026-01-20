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
        m_ordered_codeenvs;

} *_codeenv_global_ctx = NULL;

bool woort_CodeEnv_bootup(void)
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

    return true;
}
void woort_CodeEnv_shutdown(void)
{
    assert(_codeenv_global_ctx != NULL);

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

bool woort_CodeEnv_create(
    woort_Vector* /* woort_Bytecode */ moving_bytecodes,
    woort_Vector* /* woort_Value */ moving_constants,
    size_t static_storage_count,
    woort_CodeEnv** out_code_env)
{
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

    woort_vector_resize(
        moving_constants,
        moving_constants->m_element_size + static_storage_count);

    code_env_instance->m_constant_and_static_storage =
        woort_vector_move_out(
            moving_constants,
            &code_env_instance->m_constant_and_static_storage_count);

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
void woort_CodeEnv_unshare(woort_CodeEnv* code_env)
{
    if (1 == woort_atomic_fetch_sub_explicit(
        &code_env->m_refcount,
        1,
        WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL))
    {
        // Drop if last owner released.
        free((void*)code_env->m_code_begin);
        free(code_env->m_constant_and_static_storage);

        free(code_env);
    }
}