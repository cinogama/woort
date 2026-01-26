#include "woort_diagnosis.h"
#include "woort_lir_function.h"
#include "woort_linklist.h"
#include "woort_vector.h"
#include "woort_lir.h"
#include "woort_log.h"
#include "woort_bitset.h"
#include "woort_lir_block.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void woort_LIRFunction_init(woort_LIRFunction* function)
{
    woort_linklist_init(
        &function->m_register_list,
        sizeof(woort_LIRRegister));
    woort_vector_init(
        &function->m_argument_registers,
        sizeof(woort_LIRRegister*));
    woort_vector_init(
        &function->m_created_blocks,
        sizeof(woort_LIRBlock*));

    woort_LIRBlock_init(&function->m_entry_block, 0);
}
void woort_LIRFunction_deinit(woort_LIRFunction* function)
{
    // Free block instance.
    for (size_t i = 0;
        i < function->m_created_blocks.m_size;
        ++i)
    {
        woort_LIRBlock* const block_to_free =
            *(woort_LIRBlock**)woort_vector_at(
                &function->m_created_blocks, i);

        woort_LIRBlock_deinit(block_to_free);
        free(block_to_free);
    }
    woort_LIRBlock_deinit(&function->m_entry_block);
    woort_linklist_deinit(&function->m_register_list);
    woort_vector_deinit(&function->m_argument_registers);
    woort_vector_deinit(&function->m_created_blocks);
}

WOORT_NODISCARD bool woort_LIRFunction_alloc_block(
    woort_LIRFunction* function,
    woort_LIRBlock** out_block)
{
    woort_LIRBlock* new_block = malloc(sizeof(woort_LIRBlock));
    if (new_block == NULL)
    {
        WOORT_DEBUG("Out of memory.");
        return false;
    }

    if (!woort_vector_push_back(
        &function->m_created_blocks, 
        1,
        &new_block))
    {
        // Out of memory.
        free(new_block);
        return false;
    }

    woort_LIRBlock_init(
        new_block,
        (woort_LIRBlockId)function->m_created_blocks.m_size);
    *out_block = new_block;

    return true;
}

WOORT_NODISCARD bool woort_LIRFunction_alloc_register(
    woort_LIRFunction* function,
    woort_LIRRegister** out_register)
{
    woort_LIRRegister* new_register;
    if (!woort_linklist_emplace_back(
        &function->m_register_list, (void**)&new_register))
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

WOORT_NODISCARD bool woort_LIRFunction_get_argument_register(
    woort_LIRFunction* function,
    uint16_t index,
    woort_LIRRegister** out_register)
{
    // Addressing limit.
    assert(index < INT16_MAX);

    if (function->m_argument_registers.m_size <= index)
    {
        const size_t current_argument_registers_size = function->m_argument_registers.m_size;
        if (!woort_vector_resize(&function->m_argument_registers, index + 1))
        {
            // Failed to resize argument register vector.
            return false;
        }

        // Clear new added elements.
        for (size_t i = current_argument_registers_size;
            i < function->m_argument_registers.m_size;
            ++i)
        {
            woort_LIRRegister** arg_reg_ptr =
                (woort_LIRRegister**)woort_vector_at(
                    &function->m_argument_registers,
                    i);
            *arg_reg_ptr = NULL;
        }
    }

    woort_LIRRegister** argument_register = 
        woort_vector_at(&function->m_argument_registers, index);

    if (*argument_register == NULL)
    {
        // This argument register does not exist.
        woort_LIRRegister* new_argument_register;
        if (!woort_LIRFunction_alloc_register(function, &new_argument_register))
        {
            // Failed to allocate argument register.
            return false;
        }

        assert(new_argument_register->m_alive_range[0] == SIZE_MAX
            && new_argument_register->m_alive_range[1] == SIZE_MAX);

        new_argument_register->m_assigned_bp_offset = /* TBD */-1 - index;

        woort_LIRRegister** arg_reg_ptr =
            (woort_LIRRegister**)woort_vector_at(
                &function->m_argument_registers,
                index);

        assert(*arg_reg_ptr == NULL);
        *arg_reg_ptr = new_argument_register;
        argument_register = arg_reg_ptr;
    }
    *out_register = *argument_register;
    return true;
}

void _woort_LIRRegister_mark_register_active_range(
    woort_LIRRegister* target_register,
    size_t instr_index)
{
    if (target_register->m_assigned_bp_offset < 0)
        // Function arguments, skip.
        return;

    assert(target_register->m_assigned_bp_offset == INT16_MAX);
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

WOORT_NODISCARD bool woort_LIRFunction_register_allocation(
    woort_LIRFunction* function, size_t* out_stack_usage)
{
    // Mark active range for all registers.
    return false;
}
