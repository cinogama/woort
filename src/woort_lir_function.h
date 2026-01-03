#pragma once

/*
woort_lir_function.h
*/
#include <stdbool.h>

#include "woort_lir.h"
#include "woort_linklist.h"
#include "woort_vector.h"

// Function.
typedef struct woort_LIRFunction
{
    // Label data list.
    woort_LinkList /* woort_LIRLabel */ m_label_list;
    woort_Vector /* woort_LIRLabel* */ m_pending_labels_to_bind;

    // Register data list.
    woort_LinkList /* woort_LIRRegister */ m_register_list;

    // LIR codes
    woort_LinkList /* woort_LIR */ m_lir_list;

}woort_LIRFunction;

void woort_LIRFunction_init(woort_LIRFunction* function);
void woort_LIRFunction_deinit(woort_LIRFunction* function);

bool woort_LIRFunction_alloc_label(
    woort_LIRFunction* function,
    woort_LIRLabel** out_label);

bool woort_LIRFunction_alloc_register(
    woort_LIRFunction* function,
    woort_LIRRegister** out_register);

typedef void(*woort_LIRFunction_CommitCallback)(
    woort_LIRFunction* function,
    void* user_data);

/* LIR Emit */

bool woort_LIRFunction_bind(
    woort_LIRFunction* function,
    woort_LIRLabel* label);

bool woort_LIRFunction_emit_loadconst(
    woort_LIRFunction* function,
    woort_LIRRegister* aim_r,
    woort_LIR_ConstantStorage src_c);
bool woort_LIRFunction_emit_loadglobal(
    woort_LIRFunction* function,
    woort_LIRRegister* aim_r,
    woort_LIR_StaticStorage src_s);
bool woort_LIRFunction_emit_store(
    woort_LIRFunction* function,
    woort_LIR_StaticStorage aim_s,
    woort_LIRRegister* src_r);
bool woort_LIRFunction_emit_push(
    woort_LIRFunction* function,
    woort_LIRRegister* src_r);
bool woort_LIRFunction_emit_jmp(
    woort_LIRFunction* function,
    woort_LIRLabel* target_label);