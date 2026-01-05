#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <threads.h>

#include "woort_codeenv.h"
#include "woort_spin.h"
#include "woort_vector.h"
#include "woort_atomic.h"

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
        return false;

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

void _woort_CodeEnv_upload(woort_CodeEnv* codeenv)
{
    assert(_codeenv_global_ctx != NULL
        && woort_atomic_load_explicit(
            &codeenv->m_refcount, 
            WOORT_ATOMIC_MEMORY_ORDER_RELAXED) == 1);


}
