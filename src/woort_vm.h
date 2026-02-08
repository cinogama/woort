#pragma once

/*
woort_vm.h
*/

#include "woort.h"

#include "woort_diagnosis.h"
#include "woort_value.h"
#include "woort_opcode_formal.h"
#include "woort_codeenv.h"

#include <stdbool.h>

typedef struct woort_VMRuntime
{
    // VM Runtime status.
    uint32_t                m_stack_realloc_version;
    woort_Value*            m_stack;
    // NOTE: m_stack_end 指向栈空间的尾后位置，不可访问其中的内容
    woort_Value*            m_stack_end; 
    woort_Value*            m_sb;
    woort_Value*            m_sp;
    const woort_Bytecode*   m_ip;

    const woort_CodeEnv* m_env;

} woort_VMRuntime;

WOORT_NODISCARD bool woort_VMRuntime_init(woort_VMRuntime* vm);
void woort_VMRuntime_deinit(woort_VMRuntime* vm);

WOORT_NODISCARD woort_VmCallStatus woort_VMRuntime_invoke(
    woort_VMRuntime* vm, const woort_Bytecode* func);
