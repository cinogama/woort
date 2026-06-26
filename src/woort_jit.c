#include "woort_jit.h"
#include "woort_codeenv.h"
#include "woort_hashmap.h"
#include "woort_vector.h"
#include "woort_log.h"
#include "woort_spin.h"

#include <assert.h>

typedef struct woort_JITContext{
    woort_RWSpinlock            m_jit_backend_mx;
    const woort_JIT_Backend*    m_jit_backend;
} woort_JITContext;

static woort_JITContext s_jit_context;

void woort_JIT_bootup(void)
{
    woort_rwspinlock_init(&s_jit_context.m_jit_backend_mx);
    s_jit_context.m_jit_backend = NULL;
}
void woort_JIT_shutdown(void)
{
    woort_rwspinlock_deinit(&s_jit_context.m_jit_backend_mx);
    s_jit_context.m_jit_backend = NULL;
}
