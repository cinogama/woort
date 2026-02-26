#pragma once

/*
woort_codeenv.h
*/

#include "woort_diagnosis.h"
#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_gc_units.h"

#include <stdbool.h>

WOORT_NODISCARD bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);

typedef struct woort_CodeEnv {
    woort_GCUnit m_gc_unit;

    bool m_hold;

    woort_Bytecode* m_code_begin;
    woort_Bytecode* m_code_end;

    woort_Value* m_data_begin;
} woort_CodeEnv;
_Static_assert(offsetof(woort_CodeEnv, m_gc_unit) == 0, 
    "woort_GCUnit must be head of woort_CodeEnv.");

WOORT_NODISCARD bool woort_CodeEnv_create(
    const woort_Bytecode* bytecodes,
    size_t bytecodes_count,
    size_t constant_and_static_storage_count,
    woort_CodeEnv** out_code_env);

void woort_CodeEnv_drop(
    woort_CodeEnv* code_env);

WOORT_NODISCARD bool woort_CodeEnv_find(
    const woort_Bytecode* addr, woort_CodeEnv** out_code_env);

void woort_CodeEnv_GC_mark_all_envs(void);