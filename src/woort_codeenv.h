#pragma once

/*
woort_codeenv.h
*/

#include "woort_opcode_formal.h"
#include "woort_Value.h"

typedef struct woort_CodeEnv
{
    volatile size_t m_refcount;
    
    const woort_Bytecode* m_code_begin;
    const woort_Bytecode* m_code_end;

    woort_Value* m_constant_and_static_storage;
    size_t       m_constant_and_static_storage_count;

} woort_CodeEnv;

bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);

