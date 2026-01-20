#include "woort_lir_compiler.h"
#include "woort_log.h"
#include "woort_opcode_formal.h"
#include "woort_linklist.h"
#include "woort_vector.h"
#include "woort_lir.h"
#include "woort_lir_function.h"
#include "woort_util.h"
#include "woort_codeenv.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

void woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler)
{
    woort_vector_init(
        &lir_compiler->m_code_holder,
        sizeof(woort_Bytecode));

    woort_vector_init(
        &lir_compiler->m_constant_storage_holder,
        sizeof(woort_Value));

    lir_compiler->m_static_storage_count = 0;

    woort_linklist_init(
        &lir_compiler->m_function_list,
        sizeof(woort_LIRFunction));
}

void woort_LIRCompiler_deinit(woort_LIRCompiler* lir_compiler)
{
    // Close all functions.
    for (woort_LIRFunction* current_function = woort_linklist_iter(&lir_compiler->m_function_list);
        NULL != current_function;
        current_function = woort_linklist_next(current_function))
    {
        woort_LIRFunction_deinit(current_function);
    }
    woort_linklist_deinit(&lir_compiler->m_function_list);

    woort_vector_deinit(&lir_compiler->m_constant_storage_holder);
    woort_vector_deinit(&lir_compiler->m_code_holder);
}

WOORT_NODISCARD bool woort_LIRCompiler_allocate_constant(
    woort_LIRCompiler* lir_compiler,
    woort_LIR_ConstantStorage* out_constant_address)
{
    *out_constant_address =
        (woort_LIR_ConstantStorage)
        lir_compiler->m_constant_storage_holder.m_size;

    void* _useless_storage;
    if (!woort_vector_emplace_back(
        &lir_compiler->m_constant_storage_holder,
        1,
        &_useless_storage))
    {
        // Allocation failed.
        return false;
    }

    (void)_useless_storage;
    return true;
}
WOORT_NODISCARD bool woort_LIRCompiler_allocate_static_storage(
    woort_LIRCompiler* lir_compiler,
    woort_LIR_StaticStorage* out_static_storage_address)
{
    *out_static_storage_address =
        (woort_LIR_StaticStorage)
        lir_compiler->m_static_storage_count++;
    return true;
}

WOORT_NODISCARD bool woort_LIRCompiler_get_constant(
    woort_LIRCompiler* lir_compiler,
    woort_LIR_ConstantStorage constant_address,
    woort_Value** out_constant_storage)
{
    woort_Value* constant_storage;
    if (!woort_vector_index(
        &lir_compiler->m_constant_storage_holder,
        (size_t)constant_address,
        (void**)&constant_storage))
    {
        // Invalid constant address.
        WOORT_DEBUG("Invalid constant address.");
        return false;
    }
    *out_constant_storage = constant_storage;
    return true;
}

WOORT_NODISCARD bool woort_LIRCompiler_add_function(
    woort_LIRCompiler* lir_compiler,
    woort_LIRFunction** out_function)
{
    woort_LIRFunction* new_function;
    if (!woort_linklist_emplace_back(
        &lir_compiler->m_function_list, (void**)&new_function))
    {
        // Failed to allocate function.
        return false;
    }

    woort_LIRFunction_init(new_function);

    *out_function = new_function;
    return true;
}

void _woort_LIRCompiler_update_following_lir_offsets(
    woort_LIR* start_lir,
    size_t offset_delta)
{
    for (
        woort_LIR* current_lir = start_lir;
        current_lir != NULL;
        current_lir = woort_linklist_next(current_lir))
    {
        current_lir->m_fact_bytecode_offset += offset_delta;
    }
}

bool _woort_LIRCompiler_check_and_update_jcond_lir(
    woort_LIR* jcond_lir,
    woort_LIR* jmp_target_lir,
    size_t length_limit)
{
    assert(jmp_target_lir != NULL && jcond_lir != NULL);

    if (woort_util_abs_diff(
        jcond_lir->m_fact_bytecode_offset,
        jmp_target_lir->m_fact_bytecode_offset) > UINT16_MAX)
    {
        // Update all following lirs' fact bytecode offset.
        _woort_LIRCompiler_update_following_lir_offsets(
            woort_linklist_next(jcond_lir), 1);

        return true;
    }
    return false;
}

bool _woort_LIRCompiler_commit_function_codes(
    woort_LIRCompiler* lir_compiler,
    woort_LIRFunction* function)
{
    for (
        woort_LIR* current_lir = woort_linklist_iter(&function->m_lir_list);
        current_lir != NULL;
        current_lir = woort_linklist_next(current_lir))
    {
        // Emit bytecode.
        if (!woort_LIR_emit_to_bytecode_list(
            current_lir, &lir_compiler->m_code_holder))
            // Out of memory/
            return false;
    }
    return true;
}

woort_LIRCompiler_CommitResult _woort_LIRCompiler_commit_function(
    woort_LIRCompiler* lir_compiler,
    woort_LIRFunction* function)
{
    woort_LIR* const lir =
        woort_linklist_iter(&function->m_lir_list);

    /* Check */
    // 0. Check if jumping label is valid.
    for (
        woort_LIR* current_lir = lir;
        current_lir != NULL;
        current_lir = woort_linklist_next(current_lir))
    {
        woort_LIRLabel* target_label = NULL;
        switch (current_lir->m_opnum_formal)
        {
        case WOORT_LIR_OPNUMFORMAL_LABEL:
            target_label = current_lir->m_opnums.m_label.m_label;
            break;
        case WOORT_LIR_OPNUMFORMAL_R_LABEL:
            target_label = current_lir->m_opnums.m_r_label.m_label;
            break;
        case WOORT_LIR_OPNUMFORMAL_R_R_LABEL:
            target_label = current_lir->m_opnums.m_r_r_label.m_label;
            break;
        default:
            break;
        }

        if (target_label != NULL
            && target_label->m_binded_lir == NULL)
        {
            // Unbound label.
            WOORT_DEBUG(
                "Trying to jump to unbound label `%p` when committing LIR function.",
                target_label);
            return WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_UNBOUND_LABEL;
        }
    }

    /* Register allocation */
    size_t stack_usage;
    if (!woort_LIRFunction_register_allocation(function, &stack_usage))
    {
        WOORT_DEBUG("Failed to allocate registers.");
        return WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_REGISTER_ALLOCATION;
    }

    /* Commit */
    // 0. Mark base offset for this function.
    size_t current_bytecode_offset = lir_compiler->m_code_holder.m_size;
    for (
        woort_LIR* current_lir = lir;
        current_lir != NULL;
        current_lir = woort_linklist_next(current_lir))
    {
        // Update static storage references.
        woort_LIR_update_static_storage(
            current_lir,
            lir_compiler->m_constant_storage_holder.m_size);

        current_lir->m_fact_bytecode_offset = current_bytecode_offset;

        current_bytecode_offset +=
            woort_LIR_ir_length_exclude_jmp(current_lir);
    }

    // 1. Fetch & Update and insert extended jump instructions.
    /*
    NOTE:
    Because conditional jump instructions have other operands occupying instruction space,
    it is easy for the target location to exceed the jump range limit of the instruction.
    Therefore, we need to record these instructions and check if they need to be updated.
    */
    woort_Vector /* woort_LIR* */ jcond_lir_collection;
    woort_vector_init(&jcond_lir_collection, sizeof(woort_LIR*));
    {
        for (
            woort_LIR* current_lir = lir;
            current_lir != NULL;
            current_lir = woort_linklist_next(current_lir))
        {
            switch (current_lir->m_opcode)
            {
            case WOORT_LIR_OPCODE_JNZ:
            case WOORT_LIR_OPCODE_JZ:
            case WOORT_LIR_OPCODE_JEQ:
            case WOORT_LIR_OPCODE_JNEQ:
                if (!woort_vector_push_back(&jcond_lir_collection, 1, &current_lir))
                {
                    // Failed to record jcond lir.
                    woort_vector_deinit(&jcond_lir_collection);
                    return WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_OUT_OF_MEMORY;
                }
                break;
            default:
                assert(
                    current_lir->m_opnum_formal != WOORT_LIR_OPNUMFORMAL_R_LABEL
                    && current_lir->m_opnum_formal != WOORT_LIR_OPNUMFORMAL_R_R_LABEL);
                break;
            }
        }

        // Ok, check if the jump range is exceeded.
        for (size_t i = 0; i < jcond_lir_collection.m_size; )
        {
            woort_LIR* const current_jcond_lir =
                *(woort_LIR**)woort_vector_at(&jcond_lir_collection, i);

            switch (current_jcond_lir->m_opnum_formal)
            {
            case WOORT_LIR_OPNUMFORMAL_R_LABEL:
                if (!current_jcond_lir->m_opnums.m_r_label.m_externed)
                {
                    woort_LIR* const jmp_target_lir =
                        current_jcond_lir->m_opnums.m_r_label.m_label->m_binded_lir;

                    if (_woort_LIRCompiler_check_and_update_jcond_lir(
                        current_jcond_lir,
                        jmp_target_lir,
                        UINT16_MAX))
                    {
                        // Too far to jump directly, need to extern.
                        current_jcond_lir->m_opnums.m_r_label.m_externed = true;

                        // Restart checking from the beginning.
                        i = 0;
                        continue;
                    }
                }
                break;
            case WOORT_LIR_OPNUMFORMAL_R_R_LABEL:
                if (!current_jcond_lir->m_opnums.m_r_r_label.m_externed)
                {
                    woort_LIR* const jmp_target_lir =
                        current_jcond_lir->m_opnums.m_r_r_label.m_label->m_binded_lir;

                    if (_woort_LIRCompiler_check_and_update_jcond_lir(
                        current_jcond_lir,
                        jmp_target_lir,
                        UINT8_MAX))
                    {
                        // Too far to jump directly, need to extern.
                        current_jcond_lir->m_opnums.m_r_r_label.m_externed = true;

                        // Restart checking from the beginning.
                        i = 0;
                        continue;
                    }
                }
                break;
            default:
                WOORT_DEBUG(
                    "Internal error: invalid jcond lir opnum formal `%d`.",
                    current_jcond_lir->m_opnum_formal);
                abort();
            }

            // Go ahead~!
            ++i;
        }
    }
    woort_vector_deinit(&jcond_lir_collection);

    // 2. All jobs done, emit bytecodes.
    assert(stack_usage < UINT16_MAX);

    // PUSH RESERVE STACK_USAGE
    if (!woort_vector_push_back(
        &lir_compiler->m_code_holder,
        1,
        &woort_OpcodeFormal_OP6M2_8_I16_cons(
            WOORT_OPCODE_PUSH, 0, stack_usage)))
    {
        // Out of memory.
        return WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_OUT_OF_MEMORY;
    }

    if (!_woort_LIRCompiler_commit_function_codes(
        lir_compiler, function))
    {
        return WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_OUT_OF_MEMORY;
    }

    return WOORT_LIRCOMPILER_COMMIT_RESULT_OK;
}

WOORT_NODISCARD woort_LIRCompiler_CommitResult woort_LIRCompiler_commit(
    woort_LIRCompiler* lir_compiler,
    woort_CodeEnv** out_codeenv)
{
    woort_LIRFunction* current_function =
        woort_linklist_iter(&lir_compiler->m_function_list);

    while (current_function != NULL)
    {
        const woort_LIRCompiler_CommitResult r =
            _woort_LIRCompiler_commit_function(lir_compiler, current_function);

        if (r != WOORT_LIRCOMPILER_COMMIT_RESULT_OK)
            // Failed.
            return r;

        current_function = woort_linklist_next(current_function);
    }

    // All function commited.
    if (woort_CodeEnv_create(
        &lir_compiler->m_code_holder,
        &lir_compiler->m_constant_storage_holder,
        lir_compiler->m_static_storage_count,
        out_codeenv))
    {
        return WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_OUT_OF_MEMORY;
    }

    return WOORT_LIRCOMPILER_COMMIT_RESULT_OK;
}
