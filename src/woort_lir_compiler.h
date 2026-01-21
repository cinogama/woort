#pragma once

/*
woort_lir_compiler.h
*/

#include "woort_diagnosis.h"
#include "woort_linklist.h"
#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"
#include "woort_lir.h"
#include "woort_lir_function.h"
#include "woort_codeenv.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// LIRCompiler.
typedef struct woort_LIRCompiler
{
    // Code holder.
    woort_Vector /* woort_Bytecode */
                    m_code_holder;

    // Constant and Global holders.
    woort_Vector /* woort_Value */
                    m_constant_storage_holder;

    // Static storage data list.
    size_t          m_static_storage_count;

    woort_LinkList /* woort_LIRFunction */
                    m_function_list;

} woort_LIRCompiler;

void woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler);
void woort_LIRCompiler_deinit(woort_LIRCompiler* lir_compiler);

WOORT_NODISCARD bool woort_LIRCompiler_allocate_constant(
    woort_LIRCompiler* lir_compiler, 
    woort_LIR_ConstantStorage* out_constant_address);
WOORT_NODISCARD woort_LIR_StaticStorage woort_LIRCompiler_allocate_static_storage(
    woort_LIRCompiler* lir_compiler);

/*
NOTE: The returned pointer is valid until the next call to
      woort_LIRCompiler_allocate_constant.
*/
WOORT_NODISCARD bool woort_LIRCompiler_get_constant(
    woort_LIRCompiler* lir_compiler, 
    woort_LIR_ConstantStorage constant_address,
    woort_Value** out_constant_storage);

WOORT_NODISCARD bool woort_LIRCompiler_add_function(
    woort_LIRCompiler* lir_compiler,
    woort_LIRFunction** out_function);

typedef enum woort_LIRCompiler_CommitResult
{
    WOORT_LIRCOMPILER_COMMIT_RESULT_OK,

    WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_OUT_OF_MEMORY,
    WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_UNBOUND_LABEL,
    WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_LABEL_TOO_FAR,
    WOORT_LIRCOMPILER_COMMIT_RESULT_FAILED_REGISTER_ALLOCATION,

} woort_LIRCompiler_CommitResult;

WOORT_NODISCARD woort_LIRCompiler_CommitResult woort_LIRCompiler_commit(
    woort_LIRCompiler* lir_compiler,
    woort_CodeEnv** out_codeenv);
