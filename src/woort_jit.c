#include "woort_jit.h"
#include "woort_codeenv.h"
#include "woort_hashmap.h"
#include "woort_vector.h"
#include "woort_log.h"
#include "woort_spin.h"
#include "woort_util.h"
#include "woort_opcode_dispatcher.h"
#include "woort_value.h"
#include "woort_gc_closure.h"

#include <assert.h>

typedef struct woort_JITContext {
    woort_RWSpinlock m_jit_backend_mx;
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

typedef struct woort_JIT_CompileFunctionContext
{
    const woort_FunctionBoundary* m_boundary;
    /* OPTIONAL */ woort_JitFunction m_jit_function;
    
}woort_JIT_CompileFunctionContext;

static bool /* false if break loop. */ _woort_JIT_walk_through_function_to_compile(
    const void* key,
    void* value,
    void* user_data)
{
    const woort_Bytecode* const function = 
        *(const woort_Bytecode**)key;

    woort_JIT_CompileFunctionContext* const context =
        (woort_JIT_CompileFunctionContext*)value;

    const woort_JIT_Backend* const backend =
        (const woort_JIT_Backend*)user_data;

    void* func_jit_ctx;
    if (!backend->m_emit_prologue(function, context->m_boundary->m_code_length, &func_jit_ctx))
        return false;

    const woort_Bytecode* current_opcode = function;
    const woort_Bytecode* const function_end = function + context->m_boundary->m_code_length;
    while (current_opcode < function_end)
    {
        if (WOORT_BYTECODE(OP6, *current_opcode) == WOORT_OPCODE_CALLNWO)
            // Will be update to CALLNJIT.
            *(woort_Bytecode*)current_opcode = woort_OpcodeFormal_OP6_MABC26_cons(
                WOORT_OPCODE_CALLNJIT, WOORT_BYTECODE(MABC26, *current_opcode));

        current_opcode = woort_OpcodeDispatcher_decode(
            current_opcode, backend->m_dispatchers, func_jit_ctx);

        if (!backend->m_check_state(func_jit_ctx))
            return false;
    }

    woort_JitFunction jit_func_result;
    if (!backend->m_emit_epilogue(func_jit_ctx, &jit_func_result))
        return false;

    assert(jit_func_result != NULL);
    context->m_jit_function = jit_func_result;

    return true;
}

static bool /* false if break loop. */ _woort_JIT_drop_compiled_function(
    const void* key,
    void* value,
    void* user_data)
{
    (void)key;

    woort_JIT_CompileFunctionContext* const context =
        (woort_JIT_CompileFunctionContext*)value;

    const woort_JIT_Backend* const backend =
        (const woort_JIT_Backend*)user_data;

    if (context->m_jit_function != NULL)
        backend->m_drop_code(&context->m_jit_function);

    return true;
}

// Main body.
void woort_JIT_compile_env(woort_CodeEnv* cenv)
{
    /* OPTIONAL */const woort_JIT_Backend* const backend =
        _woort_JIT_get_backend();

    if (backend == NULL)
        return;

    bool jit_compile_result = true;

    woort_HashMap /* const woort_Bytecode*, woort_JIT_CompileFunctionContext */
        jit_compiled_functions_record;

    woort_hashmap_init(
        &jit_compiled_functions_record,
        sizeof(const woort_Bytecode*),
        sizeof(woort_JIT_CompileFunctionContext),
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    // Walk through all bounded function.
    const woort_FunctionBoundary* const env_function_boundaries =
        (const woort_FunctionBoundary*)cenv->m_function_boundaries.m_data;

    woort_JIT_CompileFunctionContext value;
    value.m_jit_function = NULL;

    for (size_t fidx = 0; fidx < cenv->m_function_boundaries.m_size; ++fidx)
    {
        value.m_boundary = &env_function_boundaries[fidx];

        const woort_Bytecode* const script_function =
            cenv->m_code_begin + value.m_boundary->m_offset_begin;

        const woort_hashmap_Result result = woort_hashmap_insert(
            &jit_compiled_functions_record,
            &script_function,
            &value);

        assert(result != WOORT_HASHMAP_RESULT_ALREADY_EXIST);
        if (result == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
        {
            jit_compile_result = false;
            goto _label_jit_failed;
        }
    }

    // Ok, walk through backend to generate jit codes and update CALLNWO.
    if (!woort_hashmap_foreach(
        &jit_compiled_functions_record,
        _woort_JIT_walk_through_function_to_compile,
        (void*)backend))
    {
        jit_compile_result = false;
        goto _label_jit_failed;
    }

    // Ok, all function has been compiled, Update the function constant.
    // NOTE: 此处之后不能以失败为结束，因为状态无法简单回滚。
    const woort_ConstRecord* const env_constants = 
        (const woort_ConstRecord*)cenv->m_const_records.m_data;

    for (size_t cidx = 0; cidx < cenv->m_const_records.m_size; ++cidx)
    {
        switch (env_constants[cidx].m_type)
        {
        case WOORT_CONST_TYPE_SCRIPT_FUNC:
        {
            woort_JIT_CompileFunctionContext* const ctx = woort_hashmap_at(
                &jit_compiled_functions_record,
                &cenv->m_data_begin[cidx].m_script_function);

            assert(ctx->m_jit_function != NULL);
            cenv->m_data_begin[cidx].m_jit_function = ctx->m_jit_function;
            break;
        }
        case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
        {
            woort_JIT_CompileFunctionContext* const ctx = woort_hashmap_at(
                &jit_compiled_functions_record,
                &cenv->m_data_begin[cidx].m_closure->m_script_function);

            assert(ctx->m_jit_function != NULL);
            ((woort_GCClosure*)cenv->m_data_begin[cidx].m_closure)->m_jit_function = 
                ctx->m_jit_function;
            break;
        }
        default:
            break;
        }
    }

_label_jit_failed:
    if (!jit_compile_result)
    {
        // Drop generated codes here.
        (void)woort_hashmap_foreach(
            &jit_compiled_functions_record,
            _woort_JIT_drop_compiled_function,
            (void*)backend);

        // Rollback CALLNJIT to CALLNWO.
        woort_Bytecode* current_opcode = (woort_Bytecode*)cenv->m_code_begin;
        const woort_Bytecode* const env_opcode_end = cenv->m_code_end;

        while (current_opcode < env_opcode_end)
        {
            if (WOORT_BYTECODE(OP6, *current_opcode) == WOORT_OPCODE_CALLNJIT)
                *(woort_Bytecode*)current_opcode = woort_OpcodeFormal_OP6_MABC26_cons(
                    WOORT_OPCODE_CALLNWO, WOORT_BYTECODE(MABC26, *current_opcode));

            current_opcode = 
                (woort_Bytecode*)woort_OpcodeDispatcher_decode(
                    current_opcode, NULL, NULL);
        }
    }

    woort_hashmap_deinit(&jit_compiled_functions_record);
}