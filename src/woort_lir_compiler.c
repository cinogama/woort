#include "woort_lir_compiler.h"
#include "woort_log.h"
#include "woort_opcode_formal.h"
#include "woort_linklist.h"
#include "woort_vector.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

struct woort_LIRRegister
{
    char _;
};

struct woort_LIROperand
{
    char _;
};

typedef enum woort_LIRCondBr_Type
{
    WOORT_LIR_COND_BR_TYPE_NEVER,
    WOORT_LIR_COND_BR_TYPE_JT,
    WOORT_LIR_COND_BR_TYPE_JF,
    WOORT_LIR_COND_BR_TYPE_JEQU,
    WOORT_LIR_COND_BR_TYPE_JNEQU,

}woort_LIRCondBr_Type;

struct woort_LIRCondBr
{
    woort_LIRCondBr_Type    m_type;

    /* NOTE: If m_cond_next_block is NULL, means function end. */
    woort_LIRBlock*         m_cond_next_block;
};
struct woort_LIRBlock
{
    woort_Vector /* woort_LIROperand */
        m_operands;

    struct woort_LIRCondBr m_cond_br_next_block;
    struct woort_LIRBlock* m_next_block;
};

struct woort_LIRFunction
{
    woort_LinkList /* woort_LIRRegister */ m_registers;
    woort_LinkList /* woort_LIRBlock */    m_blocks;
};

void _woort_LIRFunction_deinit(woort_LIRFunction* function)
{
    woort_LIRBlock* current_block = woort_linklist_iter(&function->m_blocks);
    while (current_block != NULL)
    {
        woort_vector_deinit(&current_block->m_operands);
        current_block = woort_linklist_next(current_block);
    }
    woort_linklist_deinit(&function->m_blocks);
    woort_linklist_deinit(&function->m_registers);
}

///////////////////////////////////////////////////////////////////////////////

void woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler)
{
    woort_vector_init(
        &lir_compiler->m_code_holder,
        sizeof(woort_Bytecode));

    woort_vector_init(
        &lir_compiler->m_constant_storage_holder,
        sizeof(woort_Value));

    woort_linklist_init(
        &lir_compiler->m_label_list,
        sizeof(woort_LIRCompiler_JmpLabelData));

    lir_compiler->m_static_storage_count = 0;

    woort_linklist_init(
        &lir_compiler->m_functions,
        sizeof(woort_LIRFunction));
}

void woort_LIRCompiler_deinit(woort_LIRCompiler* lir_compiler)
{
    woort_LIRFunction* current_function =
        woort_linklist_iter(&lir_compiler->m_functions);
    while (current_function != NULL)
    {
        _woort_LIRFunction_deinit(current_function);
        current_function = woort_linklist_next(current_function);
    }
    woort_linklist_deinit(&lir_compiler->m_functions);

    woort_linklist_deinit(&lir_compiler->m_label_list);
    woort_vector_deinit(&lir_compiler->m_constant_storage_holder);
    woort_vector_deinit(&lir_compiler->m_code_holder);
}

bool _woort_LIRCompiler_emit(
    woort_LIRCompiler* lir_compiler,
    woort_Bytecode bytecode)
{
    woort_vector_push_back(
        &lir_compiler->m_code_holder,
        1,
        &bytecode);
    return true;
}

bool woort_LIRCompiler_allocate_constant(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_ConstantStorage* out_constant_address)
{
    void* _useless_storage;
    if (!woort_vector_emplace_back(
        &lir_compiler->m_constant_storage_holder,
        1,
        &lir_compiler))
    {
        // Allocation failed.
        return false;
    }

    (void)_useless_storage;
    *out_constant_address =
        (woort_LIRCompiler_ConstantStorage)
        lir_compiler->m_constant_storage_holder.m_size;

    return true;
}
bool woort_LIRCompiler_allocate_static_storage(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_StaticStorage* out_static_storage_address)
{
    *out_static_storage_address =
        (woort_LIRCompiler_StaticStorage)
        lir_compiler->m_static_storage_count++;
    return true;
}
bool woort_LIRCompiler_allocate_label(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_JmpLabel* out_label_address)
{
    woort_LIRCompiler_JmpLabelData* new_label;
    if (!woort_linklist_emplace_back(
        &lir_compiler->m_label_list,
        &new_label))
    {
        // Allocation failed.
        return false;
    }

    new_label->m_binded_code_offset = SIZE_MAX; // Not binded yet.
    *out_label_address = new_label;
    return true;
}

bool woort_LIRCompiler_get_constant(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_ConstantStorage constant_address,
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

bool woort_LIRCompiler_add_function(
    woort_LIRCompiler* lir_compiler, woort_LIRFunction** out_function)
{
    woort_LIRFunction* new_function;

    if (!woort_linklist_emplace_back(
        &lir_compiler->m_functions,
        &new_function))
    {
        // Allocation failed.
        return false;
    }

    woort_linklist_init(
        &new_function->m_registers,
        sizeof(woort_LIRRegister));
    woort_linklist_init(
        &new_function->m_blocks,
        sizeof(woort_LIRBlock));

    *out_function = new_function;
    return true;
}

bool woort_LIRFunction_add_block(
    woort_LIRFunction* lir_function,
    woort_LIRBlock** out_block)
{
    woort_LIRBlock* new_block;
    if (!woort_linklist_emplace_back(
        &lir_function->m_blocks,
        &new_block))
    {
        // Allocation failed.
        return false;
    }
    woort_vector_init(
        &new_block->m_operands, 
        sizeof(struct woort_LIROperand));

    new_block->m_cond_br_next_block.m_type = WOORT_LIR_COND_BR_TYPE_NEVER;
    new_block->m_cond_br_next_block.m_cond_next_block = NULL;
    new_block->m_next_block = NULL;
    
    *out_block = new_block;
    return true;
}