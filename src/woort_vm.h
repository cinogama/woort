#pragma once

/*
woort_vm.h
*/

#include "woort_diagnosis.h"
#include "woort_value.h"
#include "woort_opcode_formal.h"

#include <stdbool.h>

typedef struct woort_VMRuntime
{
    // VM Self status.

    // Stack
    woort_Value*            m_stack;
    woort_Value*            m_sb;
    woort_Value*            m_sp;

    // VM Runtime status.
    const woort_Bytecode*   m_ip;
    woort_Value*            m_cs;

} woort_VMRuntime;

WOORT_NODISCARD bool woort_VMRuntime_init(woort_VMRuntime* vm);
void woort_VMRuntime_deinit(woort_VMRuntime* vm);
