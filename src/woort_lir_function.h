#pragma once

/*
woort_lir_function.h
*/
#include "woort_diagnosis.h"
#include "woort_lir.h"
#include "woort_linklist.h"
#include "woort_vector.h"
#include "woort_lir_block.h"

#include <stdbool.h>

// Function.
typedef struct woort_LIRFunction
{
    // Register data list.
    woort_LinkList /* woort_LIRRegister */
        m_register_list;
    woort_Vector /* OPTIONAL woort_LIRRegister* */ 
        m_argument_registers;

    woort_Vector /* woort_LIRBlock* */ 
        m_created_blocks;
    woort_LIRBlock* m_entry_block;

}woort_LIRFunction;

WOORT_NODISCARD bool woort_LIRFunction_init(woort_LIRFunction* function);
void woort_LIRFunction_deinit(woort_LIRFunction* function);

WOORT_NODISCARD bool woort_LIRFunction_alloc_block(
    woort_LIRFunction* function,
    woort_LIRBlock** out_block);

WOORT_NODISCARD bool woort_LIRFunction_alloc_register(
    woort_LIRFunction* function,
    woort_LIRRegister** out_register);

WOORT_NODISCARD bool woort_LIRFunction_load_constant(
    woort_LIRFunction* function,
    woort_LIR_ConstantStorage constant_storage,
    woort_LIRRegister** out_register);

WOORT_NODISCARD bool woort_LIRFunction_load_static(
    woort_LIRFunction* function,
    woort_LIR_StaticStorage static_storage,
    woort_LIRRegister** out_register);

WOORT_NODISCARD bool woort_LIRFunction_get_argument_register(
    woort_LIRFunction* function,
    uint16_t index,
    woort_LIRRegister** out_register);

typedef void(*woort_LIRFunction_CommitCallback)(
    woort_LIRFunction* function,
    void* user_data);

WOORT_NODISCARD bool woort_LIRFunction_register_allocation(
    woort_LIRFunction* function, size_t* out_stack_usage);

/* LIR Emit */
//
//WOORT_NODISCARD bool woort_LIRFunction_bind(
//    woort_LIRFunction* function,
//    woort_LIRLabel* label);
//
//WOORT_NODISCARD bool woort_LIRFunction_emit_loadconst(
//    woort_LIRFunction* function,
//    woort_LIRRegister* aim_r,
//    woort_LIR_ConstantStorage src_c);
//WOORT_NODISCARD bool woort_LIRFunction_emit_loadglobal(
//    woort_LIRFunction* function,
//    woort_LIRRegister* aim_r,
//    woort_LIR_StaticStorage src_s);
//WOORT_NODISCARD bool woort_LIRFunction_emit_store(
//    woort_LIRFunction* function,
//    woort_LIR_StaticStorage aim_s,
//    woort_LIRRegister* src_r);
//WOORT_NODISCARD bool woort_LIRFunction_emit_push(
//    woort_LIRFunction* function,
//    woort_LIRRegister* src_r);
//WOORT_NODISCARD bool woort_LIRFunction_emit_jmp(
//    woort_LIRFunction* function,
//    woort_LIRLabel* target_label);
