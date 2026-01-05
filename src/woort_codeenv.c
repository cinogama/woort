#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <threads.h>

#include "woort_codeenv.h"
#include "woort_spin.h"

static struct _woort_CodeEnv_GlobalCtx
{
    woort_RWSpinlock m_codeenvs_lock;

} *_codeenv_global_ctx = NULL;

bool woort_CodeEnv_bootup(void)
{
    assert(_codeenv_global_ctx == NULL);

    _codeenv_global_ctx =
        malloc(sizeof(struct _woort_CodeEnv_GlobalCtx));

    if (_codeenv_global_ctx == NULL)
        return false;

    woort_RWSpinlock_init(&_codeenv_global_ctx->m_codeenvs_lock);

    return true;
}
void woort_CodeEnv_shutdown(void)
{
    assert(_codeenv_global_ctx != NULL);

    woort_RWSpinlock_deinit(&_codeenv_global_ctx->m_codeenvs_lock);

    free(_codeenv_global_ctx);

    _codeenv_global_ctx = NULL;
}
