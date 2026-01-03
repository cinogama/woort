#include <assert.h>

#include "woort_lir_function.h"
#include "woort_linklist.h"
#include "woort_vector.h"
#include "woort_lir.h"
#include "woort_log.h"

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
                woort_vector_at(
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

    new_register->m_alive_range[0] = 0;
    new_register->m_alive_range[1] = 0;
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

/* LIR Emit */
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