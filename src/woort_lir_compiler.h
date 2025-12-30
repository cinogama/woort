#pragma once

/*
woort_lir_compiler.h
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "woort_opcode_formal.h"

typedef uint64_t woort_Value_t;

typedef struct woort_LIRCompiler
{
    // Code holder.
    woort_Bytecode* m_code_holder;
    size_t          m_code_capacity;
    size_t          m_code_size;   

    // Constant and Global holders.
    woort_Value_t*  m_storage_holder;
    size_t          m_storage_group_capacity;
    size_t          m_constant_group_index;
    size_t          m_constant_next_index;
    size_t          m_global_group_index;
    size_t          m_global_next_index;

} woort_LIRCompiler;

bool woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler);
bool woort_LIRCompiler_emit(
    woort_LIRCompiler* lir_compiler, woort_Bytecode bytecode);