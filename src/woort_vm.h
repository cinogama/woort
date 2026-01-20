#pragma once

/*
woort_vm.h
*/

#include "woort_diagnosis.h"
#include "woort_value.h"
#include "woort_opcode_formal.h"
#include "woort_codeenv.h"

#include <stdbool.h>

typedef struct woort_VMRuntime
{
    // VM Runtime status.
    woort_Value*            m_stack;
    // NOTE: m_stack_end 指向栈空间的尾后位置，不可访问其中的内容
    woort_Value*            m_stack_end; 
    woort_Value*            m_sb;
    woort_Value*            m_sp;
    const woort_Bytecode*   m_ip;

    const woort_CodeEnv* m_env;

} woort_VMRuntime;

typedef enum woort_VMRuntime_CallStatus
{
    WOORT_VM_CALL_STATUS_NORMAL,

    WOORT_VM_CALL_STATUS_TBD_BAD_STATUS,


} woort_VMRuntime_CallStatus;

WOORT_NODISCARD bool woort_VMRuntime_init(woort_VMRuntime* vm);
void woort_VMRuntime_deinit(woort_VMRuntime* vm);

WOORT_NODISCARD woort_VMRuntime_CallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm, const woort_Bytecode* func);
