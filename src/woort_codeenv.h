#pragma once

/*
woort_codeenv.h
*/

#include "woort_diagnosis.h"
#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"
#include "woort_atomic.h"

#include <stdbool.h>

WOORT_NODISCARD bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);

typedef struct woort_CodeEnv {
    woort_AtomicSize m_refcount;

    const woort_Bytecode* m_code_begin;
    const woort_Bytecode* m_code_end;

    woort_Value* m_constant_and_static_storage;
    size_t       m_constant_and_static_storage_count;
} woort_CodeEnv;

WOORT_NODISCARD bool woort_CodeEnv_create(
    woort_Vector* /* woort_Bytecode */ moving_bytecodes,
    woort_Vector* /* woort_Value */ moving_constants,
    size_t static_storage_count,
    woort_CodeEnv** out_code_env);

void woort_CodeEnv_share(woort_CodeEnv* code_env);
void woort_CodeEnv_unshare(woort_CodeEnv* code_env);

WOORT_NODISCARD bool woort_CodeEnv_find(
    const woort_Bytecode* addr, woort_CodeEnv** out_code_env);
