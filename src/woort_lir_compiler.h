#pragma once

/*
woort_lir_compiler.h
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "woort_linklist.h"
#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"

// Jump label.
typedef struct woort_LIRCompiler_JmpLabelData
{
    /* NOTE: SIZE_MAX means bind less. */
    size_t      m_binded_code_offset;   

} woort_LIRCompiler_JmpLabelData, * woort_LIRCompiler_JmpLabel;

// Constant.
typedef uint64_t woort_LIRCompiler_ConstantStorage;

// Static storage.
typedef uint64_t woort_LIRCompiler_StaticStorage;

// LIRCompiler.
typedef struct woort_LIRCompiler
{
    // Code holder.
    woort_Vector /* woort_Bytecode */
                    m_code_holder;

    // Constant and Global holders.
    woort_Vector /* woort_Value */
                    m_constant_storage_holder;

    // Label data list.
    woort_LinkList /* woort_LIRCompiler_JmpLabelData */
                    m_label_list;

    // Static storage data list.
    size_t          m_static_storage_count;

    // LIR Storage.
    woort_LinkList /* woort_LIRFunction */
                    m_functions;

} woort_LIRCompiler;

void woort_LIRCompiler_init(woort_LIRCompiler* lir_compiler);
void woort_LIRCompiler_deinit(woort_LIRCompiler* lir_compiler);

bool woort_LIRCompiler_allocate_constant(
    woort_LIRCompiler* lir_compiler, 
    woort_LIRCompiler_ConstantStorage* out_constant_address);
bool woort_LIRCompiler_allocate_static_storage(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_StaticStorage* out_static_storage_address);
bool woort_LIRCompiler_allocate_label(
    woort_LIRCompiler* lir_compiler,
    woort_LIRCompiler_JmpLabel* out_label_address);

/*
NOTE: The returned pointer is valid until the next call to
      woort_LIRCompiler_allocate_constant.
*/
bool woort_LIRCompiler_get_constant(
    woort_LIRCompiler* lir_compiler, 
    woort_LIRCompiler_ConstantStorage constant_address,
    woort_Value** out_constant_storage);

typedef struct woort_LIRRegister woort_LIRRegister;
typedef struct woort_LIRFunction woort_LIRFunction;
typedef struct woort_LIRBlock woort_LIRBlock;

bool woort_LIRCompiler_add_function(
    woort_LIRCompiler* lir_compiler,
    woort_LIRFunction** out_function);

/*
NOTE: Function's first block will be treated as entry block.
*/
bool woort_LIRFunction_add_block(
    woort_LIRFunction* lir_function,
    woort_LIRBlock** out_block);
