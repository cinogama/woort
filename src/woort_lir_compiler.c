#include "woort_lir_compiler.h"
#include "woort_log.h"
#include "woort_opcode_formal.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

#define WOORT_LIR_COMPILER_STORAGE_GROUP_SIZE 16

bool woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler)
{
    lir_compiler->m_code_holder = 
        malloc(8 * sizeof(woort_Bytecode));
    lir_compiler->m_storage_holder =
        malloc(
            2 * WOORT_LIR_COMPILER_STORAGE_GROUP_SIZE * sizeof(woort_Value_t));

    if (lir_compiler->m_code_holder == NULL
        || lir_compiler->m_storage_holder == NULL)
    {
        WOORT_DEBUG("Allocation failed.");

        free(lir_compiler->m_code_holder);
        free(lir_compiler->m_storage_holder);

        return false;
    }

    lir_compiler->m_code_capacity = 8;
    lir_compiler->m_code_size = 0;

    lir_compiler->m_storage_group_capacity = 2;
    lir_compiler->m_constant_group_index = 0;
    lir_compiler->m_constant_next_index = 0;
    lir_compiler->m_global_group_index = 1;
    lir_compiler->m_global_next_index = 0;

    return true;
}

bool woort_LIRCompiler_emit(woort_LIRCompiler* lir_compiler, woort_Bytecode bytecode)
{
    if (lir_compiler->m_code_size >= lir_compiler->m_code_capacity)
    {
        // Need to reallocate.
        size_t new_capacity = lir_compiler->m_code_capacity * 2;
        woort_Bytecode* new_holder = 
            realloc(
                lir_compiler->m_code_holder, 
                new_capacity * sizeof(woort_Bytecode));

        if (new_holder == NULL)
        {
            WOORT_DEBUG("Reallocation failed.");
            return false;
        }
        lir_compiler->m_code_holder = new_holder;
        lir_compiler->m_code_capacity = new_capacity;
    }
    lir_compiler->m_code_holder[lir_compiler->m_code_size++] = bytecode;
    return true;
}