#include "woort.h"

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
#include "woort_jit_bridge.h"
#include "woort_platform.h"
#include "woort_gc.h"

#include "woort_jit_x64.h"
#include "woort_jit_arm64.h"

#include <assert.h>
#include <stdlib.h>

typedef struct woort_JITContext {
    woort_RWSpinlock m_jit_backend_mx;
    /* OPTIONAL */ const woort_JIT_Backend* m_jit_backend;
} woort_JITContext;

static woort_JITContext g_jit_context;

WOORT_NODISCARD bool woort_JIT_bootup(bool enable)
{
#ifdef WOORT_BUILD_WITH_ASMJIT
    if (!woort_JIT_Asmjit_bootup())
        return false;
#endif

    woort_rwspinlock_init(&g_jit_context.m_jit_backend_mx);
    g_jit_context.m_jit_backend = NULL;

    if (enable)
    {
#if defined(WOORT_PLATFORM_X64) && defined(WOORT_JIT_SUPPORT_X64)
        woort_JIT_set_backend(&WOORT_JIT_BACKEND_IMPL_X64);
#elif defined(WOORT_PLATFORM_ARM64)  && defined(WOORT_JIT_SUPPORT_ARM64)
        woort_JIT_set_backend(&WOORT_JIT_BACKEND_IMPL_ARM64);
#else
        /* woort_JIT_set_backend(NULL); */
#endif
    }

    return true;
}
void woort_JIT_shutdown(void)
{
    woort_rwspinlock_deinit(&g_jit_context.m_jit_backend_mx);
    g_jit_context.m_jit_backend = NULL;

#ifdef WOORT_BUILD_WITH_ASMJIT
    woort_JIT_Asmjit_shutdown();
#endif
}

void woort_JIT_set_backend(/* OPTIONAL */ const woort_JIT_Backend* backend)
{
    woort_rwspinlock_write_lock(&g_jit_context.m_jit_backend_mx);
    g_jit_context.m_jit_backend = backend;
    woort_rwspinlock_write_unlock(&g_jit_context.m_jit_backend_mx);
}

static /* OPTIONAL */ const woort_JIT_Backend* _woort_JIT_get_backend(void)
{
    woort_rwspinlock_read_lock(&g_jit_context.m_jit_backend_mx);
    const woort_JIT_Backend* backend = g_jit_context.m_jit_backend;
    woort_rwspinlock_read_unlock(&g_jit_context.m_jit_backend_mx);
    return backend;
}

typedef struct woort_JIT_CompileFunctionContext
{
    const woort_FunctionBoundary* m_boundary;
    /* OPTIONAL */ woort_JitFunction m_jit_function;
    size_t m_index; /* = fidx; used as CALLNJIT operand and m_jit_functions subscript */

}woort_JIT_CompileFunctionContext;

typedef struct woort_JIT_CompileWalkContext
{
    const woort_JIT_Backend* m_backend;
    const woort_CodeEnv* m_cenv;
    const woort_HashMap* m_func_map; /* script_function -> CompileFunctionContext */
} woort_JIT_CompileWalkContext;

static bool /* false if break loop. */ _woort_JIT_walk_through_function_to_compile(
    const void* key,
    void* value,
    void* user_data)
{
    const woort_Bytecode* const function =
        *(const woort_Bytecode**)key;

    woort_JIT_CompileFunctionContext* const context =
        (woort_JIT_CompileFunctionContext*)value;

    const woort_JIT_CompileWalkContext* const walk_ctx =
        (const woort_JIT_CompileWalkContext*)user_data;

    const woort_JIT_Backend* const backend = walk_ctx->m_backend;

    const woort_Bytecode* current_opcode = function;

    void* func_jit_ctx;
    if (!backend->m_emit_prologue(walk_ctx->m_cenv, &current_opcode, &func_jit_ctx))
        return false;

    const woort_Bytecode* const function_end = function + context->m_boundary->m_code_length;
    while (current_opcode < function_end)
    {
        if (WOORT_BYTECODE(OP6, *current_opcode) == WOORT_OPCODE_CALLNWO)
        {
            /*
             * Translate CALLNWO -> CALLNJIT, rewriting the operand from the
             * constant slot index (cidx) to the target's subscript in
             * m_jit_functions.  The script_function -> JIT-slot mapping is
             * pre-established (in woort_JIT_compile_env) before this walk,
             * so a miss here is an inconsistency -> fail the whole compile.
             */
            const woort_Opcode_Global cidx =
                WOORT_BYTECODE(MABC26, *current_opcode);
            const woort_Bytecode* const target_script_function =
                walk_ctx->m_cenv->m_data_begin[cidx].m_script_function;

            void* found;
            if (!woort_hashmap_find(
                (woort_HashMap*)walk_ctx->m_func_map,
                &target_script_function, &found))
            {
                return false;
            }

            const woort_JIT_CompileFunctionContext* const target_ctx =
                (const woort_JIT_CompileFunctionContext*)found;

            *(woort_Bytecode*)current_opcode = woort_OpcodeFormal_OP6_MABC26_cons(
                WOORT_OPCODE_CALLNJIT,
                (woort_Opcode_Global)target_ctx->m_index);
        }

        if (!backend->m_pre_dispatch(func_jit_ctx))
            return false;

        const woort_Bytecode* const next_opcode = woort_OpcodeDispatcher_decode(
            current_opcode, backend->m_dispatchers, func_jit_ctx);

        if (!backend->m_post_dispatch(func_jit_ctx))
            return false;

        current_opcode = next_opcode;
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

static bool /* false if break loop. */ _woort_JIT_collect_jit_function(
    const void* key,
    void* value,
    void* user_data)
{
    const woort_Bytecode* const script_function =
        *(const woort_Bytecode**)key;

    woort_JIT_CompileFunctionContext* const context =
        (woort_JIT_CompileFunctionContext*)value;

    woort_CodeEnv_JITCompiledRecord* const out =
        (woort_CodeEnv_JITCompiledRecord*)user_data;

    /* Stored by pre-assigned index (context->m_index), decoupled from
       hashmap iteration order; matches the CALLNJIT operand rewriting. */
    out[context->m_index].m_jit_function    = context->m_jit_function;
    out[context->m_index].m_script_function = script_function;

    return true;
}

/* Main body. */
void woort_JIT_compile_env(woort_CodeEnv* cenv)
{
    /* OPTIONAL */const woort_JIT_Backend* const backend =
        _woort_JIT_get_backend();

    if (backend == NULL || cenv->m_jit_drop_code != NULL)
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

    /* Walk through all bounded function. */
    const woort_FunctionBoundary* const env_function_boundaries =
        (const woort_FunctionBoundary*)cenv->m_function_boundaries.m_data;

    woort_JIT_CompileFunctionContext value;
    value.m_jit_function = NULL;

    for (size_t fidx = 0; fidx < cenv->m_function_boundaries.m_size; ++fidx)
    {
        value.m_boundary = &env_function_boundaries[fidx];
        value.m_index = fidx;

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

    /*
     * One-shot allocate the JIT compiled records array BEFORE the compile
     * walk: JIT emitters embed the absolute address of each record's
     * m_jit_function slot at compile time, so the array must already exist
     * and be stable (never moved) for the CodeEnv's lifetime.
     */
    const size_t jit_func_count = jit_compiled_functions_record.m_size;
    assert(jit_func_count < (size_t)(1u << 26)); /* must fit MABC26 operand */

    cenv->m_jit_functions = (woort_CodeEnv_JITCompiledRecord*)calloc(
        jit_func_count, sizeof(woort_CodeEnv_JITCompiledRecord));

    if (cenv->m_jit_functions == NULL)
    {
        WOORT_DEBUG("Out of memory allocating JIT records.");
        jit_compile_result = false;
        goto _label_jit_failed;
    }

    cenv->m_jit_function_count = jit_func_count;

    /*
     * Pre-fill m_script_function so that a compile-failure rollback can still
     * recover the target function (and thus its cidx) from a CALLNJIT operand.
     * Index == fidx, in lockstep with function_boundaries.
     */
    for (size_t fidx = 0; fidx < jit_func_count; ++fidx)
    {
        cenv->m_jit_functions[fidx].m_script_function =
            cenv->m_code_begin + env_function_boundaries[fidx].m_offset_begin;
    }

    /* Ok, walk through backend to generate jit codes and rewrite CALLNWO. */
    woort_JIT_CompileWalkContext walk_ctx;
    walk_ctx.m_backend = backend;
    walk_ctx.m_cenv = cenv;
    walk_ctx.m_func_map = &jit_compiled_functions_record;

    if (!woort_hashmap_foreach(
        &jit_compiled_functions_record,
        _woort_JIT_walk_through_function_to_compile,
        &walk_ctx))
    {
        jit_compile_result = false;
        goto _label_jit_failed;
    }

    /* Fill each record's m_jit_function (stored by pre-assigned index). */
    if (!woort_hashmap_foreach(
        &jit_compiled_functions_record,
        _woort_JIT_collect_jit_function,
        cenv->m_jit_functions))
    {
        /* Unreachable: callback always returns true after pre-allocation. */
        jit_compile_result = false;
        goto _label_jit_failed;
    }

    /*
     * Update closures only. SCRIPT_FUNC slots are intentionally NOT touched:
     * CALLNJIT resolves jit functions through m_jit_functions[index] now.
     *
     * NOTE: 此处之后不能以失败为结束，因为状态无法简单回滚。
     */
    const woort_ConstRecord* const env_constants =
        (const woort_ConstRecord*)cenv->m_const_records.m_data;

    for (size_t cidx = 0; cidx < cenv->m_const_records.m_size; ++cidx)
    {
        switch (env_constants[cidx].m_type)
        {
        case WOORT_CONST_TYPE_SCRIPT_CLOSURE:
        {
            const woort_GCClosure* const target_closure = cenv->m_data_begin[cidx].m_closure;
            if (target_closure->m_script_function >= cenv->m_code_begin
                && target_closure->m_script_function < cenv->m_code_end)
            {
                woort_JIT_CompileFunctionContext* const ctx = woort_hashmap_at(
                    &jit_compiled_functions_record,
                    &cenv->m_data_begin[cidx].m_closure->m_script_function);

                assert(ctx->m_jit_function != NULL);
                ((woort_GCClosure*)cenv->m_data_begin[cidx].m_closure)->m_jit_function =
                    ctx->m_jit_function;
            }
            /* 
            else: this function constant comes from other codeenv, only REPL
                walk here, just let it do far-call. 
            */

            break;
        }
        default:
            break;
        }
    }

    cenv->m_jit_drop_code = backend->m_drop_code;
    cenv->m_jit_linked = true;

_label_jit_failed:
    if (!jit_compile_result)
    {
        /* Drop generated codes here. */
        (void)woort_hashmap_foreach(
            &jit_compiled_functions_record,
            _woort_JIT_drop_compiled_function,
            (void*)backend);

        /*
         * Rollback CALLNJIT -> CALLNWO. The operand must be translated from
         * the m_jit_functions index back to the constant slot index (cidx).
         * Only reachable if the compile walk already ran (hence rewrote some
         * CALLNWO into CALLNJIT); in that case m_jit_functions is allocated
         * and its m_script_function slots are pre-filled.
         */
        woort_Bytecode* current_opcode = (woort_Bytecode*)cenv->m_code_begin;
        const woort_Bytecode* const env_opcode_end = cenv->m_code_end;

        while (current_opcode < env_opcode_end)
        {
            if (WOORT_BYTECODE(OP6, *current_opcode) == WOORT_OPCODE_CALLNJIT)
            {
                assert(cenv->m_jit_functions != NULL);
                const woort_Opcode_Global jit_index =
                    WOORT_BYTECODE(MABC26, *current_opcode);
                const woort_Bytecode* const target_script_function =
                    cenv->m_jit_functions[jit_index].m_script_function;
                const woort_Opcode_Global cidx =
                    woort_CodeEnv_cidx_for_script_function(cenv, target_script_function);

                *(woort_Bytecode*)current_opcode = woort_OpcodeFormal_OP6_MABC26_cons(
                    WOORT_OPCODE_CALLNWO, cidx);
            }

            current_opcode =
                (woort_Bytecode*)woort_OpcodeDispatcher_decode(
                    current_opcode, NULL, NULL);
        }

        /* Discard the (now useless) records array. */
        if (cenv->m_jit_functions != NULL)
        {
            free(cenv->m_jit_functions);
            cenv->m_jit_functions = NULL;
            cenv->m_jit_function_count = 0;
        }
    }

    woort_hashmap_deinit(&jit_compiled_functions_record);
}

static void _woort_JIT_unjit_all_codeenv_when_vm_suspended(void* useless)
{
    (void)useless;
    woort_CodeEnv_JIT_unjit_all_envs();
}

void woort_JIT_unjit_all_codeenv(void)
{
    bool just_unjit = false;

    woort_rwspinlock_write_lock(&g_jit_context.m_jit_backend_mx);
    if (g_jit_context.m_jit_backend != NULL)
    {
        just_unjit = true;
        g_jit_context.m_jit_backend = NULL;
    }
    woort_rwspinlock_write_unlock(&g_jit_context.m_jit_backend_mx);

    if (just_unjit)
    {
        woort_GC_suspend_all_vm_to_do_sth(
            &_woort_JIT_unjit_all_codeenv_when_vm_suspended, NULL);
    }
}
