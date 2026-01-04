#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "woort_lir_function.h"
#include "woort_linklist.h"
#include "woort_vector.h"
#include "woort_lir.h"
#include "woort_log.h"
#include "woort_bitset.h"

void woort_LIRFunction_init(woort_LIRFunction* function)
{
    woort_linklist_init(
        &function->m_label_list,
        sizeof(woort_LIRLabel));
    woort_vector_init(
        &function->m_pending_labels_to_bind,
        sizeof(woort_LIRLabel*));
    woort_linklist_init(
        &function->m_register_list,
        sizeof(woort_LIRRegister));
    woort_linklist_init(
        &function->m_lir_list,
        sizeof(woort_LIR));
}
void woort_LIRFunction_deinit(woort_LIRFunction* function)
{
    woort_linklist_deinit(&function->m_label_list);
    woort_vector_deinit(&function->m_pending_labels_to_bind);
    woort_linklist_deinit(&function->m_register_list);
    woort_linklist_deinit(&function->m_lir_list);
}

bool _woort_LIRFunction_append_lir(woort_LIRFunction* function, woort_LIR** out_lir)
{
    woort_LIR* new_lir;
    if (!woort_linklist_emplace_back(
        &function->m_lir_list, &new_lir))
    {
        // Failed to allocate LIR.
        return false;
    }

    new_lir->m_fact_bytecode_offset = 0;

    if (function->m_pending_labels_to_bind.m_size != 0)
    {
        for (size_t i = 0; i < function->m_pending_labels_to_bind.m_size; ++i)
        {
            woort_LIRLabel* binding_label =
                *(woort_LIRLabel**)woort_vector_at(
                    &function->m_pending_labels_to_bind,
                    i);
#ifndef NDEBUG
            assert(binding_label->m_function == function);
#endif
            if (binding_label->m_binded_lir != NULL)
            {
                // Label already binded.
                assert(binding_label->m_binded_lir == new_lir);

                WOORT_DEBUG(
                    "Warning: Label `%p` bounded multiple times, "
                    "should not happen in normal cases.",
                    binding_label);
            }
            else
                binding_label->m_binded_lir = new_lir;
        }

        // All pending labels has been binded, clear the list.
        woort_vector_clear(&function->m_pending_labels_to_bind);
    }

    *out_lir = new_lir;
    return true;
}

bool woort_LIRFunction_alloc_label(
    woort_LIRFunction* function,
    woort_LIRLabel** out_label)
{
    woort_LIRLabel* new_label;
    if (!woort_linklist_emplace_back(
        &function->m_label_list, &new_label))
    {
        // Failed to allocate label.
        return false;
    }

#ifndef NDEBUG
    new_label->m_function = function;
#endif
    new_label->m_binded_lir = NULL;

    *out_label = new_label;
    return true;
}

bool woort_LIRFunction_alloc_register(
    woort_LIRFunction* function,
    woort_LIRRegister** out_register)
{
    woort_LIRRegister* new_register;
    if (!woort_linklist_emplace_back(
        &function->m_register_list, &new_register))
    {
        // Failed to allocate register.
        return false;
    }

    new_register->m_alive_range[0] = SIZE_MAX;
    new_register->m_alive_range[1] = SIZE_MAX;
    new_register->m_assigned_bp_offset = INT16_MAX;

    *out_register = new_register;
    return true;
}

bool woort_LIRFunction_bind(
    woort_LIRFunction* function,
    woort_LIRLabel* label)
{
    if (label->m_binded_lir != NULL)
    {
        // Label already binded.
        WOORT_DEBUG("Label already binded.");
        return false;
    }
#ifndef NDEBUG
    else if (label->m_function != function)
    {
        // Label does not belong to this function.
        WOORT_DEBUG("Label does not belong to this function.");
        return false;
    }
#endif

    woort_vector_push_back(
        &function->m_pending_labels_to_bind,
        1,
        &label);
    return true;
}

void _woort_LIRRegister_mark_register_active_range(
    woort_LIRRegister* target_register,
    size_t instr_index)
{
    if (target_register->m_alive_range[0] == SIZE_MAX)
    {
        // First time to be used, set the start of alive range.
        target_register->m_alive_range[0] = instr_index;
    }
    // Update the end of alive range.
    target_register->m_alive_range[1] = instr_index;
}

int _woort_register_start_pos_comparator(const void* a, const void* b)
{
    woort_LIRRegister* reg_a = *(woort_LIRRegister**)a;
    woort_LIRRegister* reg_b = *(woort_LIRRegister**)b;

    if (reg_a->m_alive_range[0] < reg_b->m_alive_range[0])
        return -1;
    else if (reg_a->m_alive_range[0] > reg_b->m_alive_range[0])
        return 1;
    return 0;
}

bool woort_LIRFunction_register_allocation(
    woort_LIRFunction* function)
{
    // Mark active range for all registers.
    size_t lir_count = 0;
    for (
        woort_LIR* current_lir = woort_linklist_iter(&function->m_lir_list);
        current_lir != NULL;
        (current_lir = woort_linklist_next(current_lir)), ++lir_count)
    {
        switch (current_lir->m_opnum_formal)
        {
        case WOORT_LIR_OPNUMFORMAL_CS_R:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_cs_r.m_r,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_S_R:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_s_r.m_r,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_R:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r.m_r,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_R_R:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r.m_r1,
                lir_count);
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r.m_r2,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_R_R_R:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r_r.m_r1,
                lir_count);
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r_r.m_r2,
                lir_count);
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r_r.m_r3,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_R_R_COUNT16:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r_count16.m_r1,
                lir_count);
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r_count16.m_r2,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_R_COUNT16:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_count16.m_r,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_R_R_LABEL:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r_label.m_r1,
                lir_count);
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_r_label.m_r2,
                lir_count);
            break;
        case WOORT_LIR_OPNUMFORMAL_R_LABEL:
            _woort_LIRRegister_mark_register_active_range(
                current_lir->m_opnums.m_r_label.m_r,
                lir_count);
            break;
        default:
            // No registration allocation needed.
            break;
        }
    }

    // Ok, all registers active range has been marked.
    // Now we need to allocate registers.
    woort_Vector registers;
    woort_vector_init(&registers, sizeof(woort_LIRRegister*));

    for (
        woort_LIRRegister* current_register = woort_linklist_iter(&function->m_register_list);
        current_register != NULL;
        current_register = woort_linklist_next(current_register))
    {
        if (current_register->m_alive_range[0] != SIZE_MAX)
        {
            woort_vector_push_back(&registers, 1, &current_register);
        }
    }

    // Sort registers by start position.
    qsort(
        registers.m_data,
        registers.m_size,
        sizeof(woort_LIRRegister*),
        _woort_register_start_pos_comparator);

    woort_Bitset bitset;
    woort_bitset_init(&bitset, INT16_MAX);

    woort_Vector active_registers;
    woort_vector_init(&active_registers, sizeof(woort_LIRRegister*));

    bool success = true;
    for (size_t i = 0; i < registers.m_size; ++i)
    {
        woort_LIRRegister* current_register = *(woort_LIRRegister**)woort_vector_at(&registers, i);

        // Expire old intervals.
        for (size_t j = 0; j < active_registers.m_size; )
        {
            woort_LIRRegister* active_register = *(woort_LIRRegister**)woort_vector_at(&active_registers, j);
            if (active_register->m_alive_range[1] < current_register->m_alive_range[0])
            {
                // Expired.
                woort_bitset_reset(&bitset, (size_t)active_register->m_assigned_bp_offset);
                
                // Remove from active list.
                // Swap with last element and pop back.
                if (j != active_registers.m_size - 1)
                {
                    woort_LIRRegister** last_element = 
                        (woort_LIRRegister**)woort_vector_at(&active_registers, active_registers.m_size - 1);
                    woort_LIRRegister** current_element = 
                        (woort_LIRRegister**)woort_vector_at(&active_registers, j);
                    *current_element = *last_element;
                }
                active_registers.m_size--;
            }
            else
            {
                ++j;
            }
        }

        // Allocate register.
        size_t assigned_offset;
        if (woort_bitset_find_first_unset(&bitset, &assigned_offset))
        {
            current_register->m_assigned_bp_offset = (int16_t)assigned_offset;
            woort_bitset_set(&bitset, assigned_offset);
            woort_vector_push_back(&active_registers, 1, &current_register);
        }
        else
        {
            // Failed to allocate register.
            // Spill?
            WOORT_DEBUG("Failed to allocate register.");
            success = false;

            break;
        }
    }

    woort_vector_deinit(&active_registers);
    woort_bitset_deinit(&bitset);
    woort_vector_deinit(&registers);

    return success;
}

#define WOORT_LIR_FUNCTION_EMIT_LIR(LIROP)                          \
    woort_LIR* new_lir;                                         \
    if (!_woort_LIRFunction_append_lir(function, &new_lir))     \
    {                                                           \
        /* Failed to append LIR. */                            \
        return false;                                           \
    }                                                           \
    new_lir->m_opcode = WOORT_LIR_OPCODE_##LIROP;               \
    new_lir->m_opnum_formal = WOORT_LIR_OP_FORMAL_KIND(LIROP);  \
    WOORT_LIR_OP_FORMAL_T(LIROP)* opnums = &new_lir->m_opnums.m_##LIROP                   


bool woort_LIRFunction_emit_loadconst(
    woort_LIRFunction* function,
    woort_LIRRegister* aim_r,
    woort_LIR_ConstantStorage src_c)
{
    WOORT_LIR_FUNCTION_EMIT_LIR(LOAD);
    opnums->m_r = aim_r;
    opnums->m_cs.m_is_constant = true;
    opnums->m_cs.m_constant = src_c;

    return true;
}

bool woort_LIRFunction_emit_loadglobal(
    woort_LIRFunction* function,
    woort_LIRRegister* aim_r,
    woort_LIR_StaticStorage src_s)
{
    WOORT_LIR_FUNCTION_EMIT_LIR(LOAD);
    opnums->m_r = aim_r;
    opnums->m_cs.m_is_constant = false;
    opnums->m_cs.m_static = src_s;

    return true;
}

bool woort_LIRFunction_emit_store(
    woort_LIRFunction* function,
    woort_LIR_StaticStorage aim_s,
    woort_LIRRegister* src_r)
{
    WOORT_LIR_FUNCTION_EMIT_LIR(STORE);
    opnums->m_r = src_r;
    opnums->m_s = aim_s;

    return true;
}

bool woort_LIRFunction_emit_push(
    woort_LIRFunction* function,
    woort_LIRRegister* src_r)
{
    WOORT_LIR_FUNCTION_EMIT_LIR(PUSH);
    opnums->m_r = src_r;

    return true;
}

bool woort_LIRFunction_emit_jmp(
    woort_LIRFunction* function,
    woort_LIRLabel* target_label)
{
    WOORT_LIR_FUNCTION_EMIT_LIR(JMP);
    opnums->m_label = target_label;

    return true;
}

#undef WOORT_LIR_FUNCTION_EMIT_LIR