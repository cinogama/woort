#include "woort_jit.h"
#include "woort_codeenv.h"
#include "woort_hashmap.h"
#include "woort_vector.h"
#include "woort_log.h"
#include "woort_spin.h"
#include "woort_util.h"

#include <assert.h>

typedef struct woort_JITContext {
    woort_RWSpinlock            m_jit_backend_mx;
    const woort_JIT_Backend* m_jit_backend;
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

void woort_JIT_set_backend(const woort_JIT_Backend* backend)
{
    woort_rwspinlock_write_lock(&s_jit_context.m_jit_backend_mx);
    s_jit_context.m_jit_backend = backend;
    woort_rwspinlock_write_unlock(&s_jit_context.m_jit_backend_mx);
}

static /* OPTIONAL */ const woort_JIT_Backend* _woort_JIT_get_backend(void)
{
    woort_rwspinlock_read_lock(&s_jit_context.m_jit_backend_mx);
    const woort_JIT_Backend* backend = s_jit_context.m_jit_backend;
    woort_rwspinlock_read_unlock(&s_jit_context.m_jit_backend_mx);
    return backend;
}

static bool /* false if break loop. */ _woort_JIT_walk_through_function_to_compile(
    const void* key,
    void* value,
    void* user_data)
{
    const woort_Bytecode* function = *(const woort_Bytecode**)key;
    (void)value;
    (void)user_data;


}

// Main body.
WOORT_NODISCARD woort_JIT_Result woort_JIT_compile_env(woort_CodeEnv* cenv)
{
    /* OPTIONAL */const woort_JIT_Backend* const backend =
        _woort_JIT_get_backend();

    if (backend == NULL)
        return WOORT_JIT_NO_BACKEND;

    woort_JIT_Result jit_compile_result = WOORT_JIT_OK_FINISHED;

    woort_HashMap /* const woort_Bytecode*, /* OPTIONAL * / woort_JitFunction */
        jit_compiled_functions_record;

    woort_hashmap_init(
        &jit_compiled_functions_record,
        sizeof(const woort_Bytecode*),
        sizeof(woort_JitFunction),
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    // Walk through all bounded function.
    woort_JitFunction null_jit_func = NULL;

    const woort_FunctionBoundary* const env_function_boundaries =
        cenv->m_function_boundaries.m_data;

    for (size_t fidx = 0; fidx < cenv->m_function_boundaries.m_size; ++fidx)
    {
        const woort_Bytecode* const script_function =
            cenv->m_code_begin + env_function_boundaries->m_offset_begin;

        const woort_hashmap_Result result = woort_hashmap_insert(
            &jit_compiled_functions_record,
            &script_function,
            &null_jit_func);

        assert(result != WOORT_HASHMAP_RESULT_ALREADY_EXIST);
        if (result == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
        {
            jit_compile_result = WOORT_JIT_FAILED_OUT_OF_MEMORY;
            goto _label_jit_failed;
        }
    }

    // Ok, walk through backend to generate jit codes and update CALLNWO.
    if (!woort_hashmap_foreach(
        &jit_compiled_functions_record,
        _woort_JIT_walk_through_function_to_compile,
        NULL))
    {
        jit_compile_result = WOORT_JIT_FAILED_OUT_OF_MEMORY;
        goto _label_jit_failed;
    }

    // Ok, all function has been compiled.

_label_jit_failed:
    if (jit_compile_result != WOORT_JIT_OK_FINISHED)
    {
        // Drop generated codes here.
    }

    woort_hashmap_deinit(&jit_compiled_functions_record);
    return jit_compile_result;
}