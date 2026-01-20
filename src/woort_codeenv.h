#pragma once

/*
woort_codeenv.h
*/

#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"

#include <stdbool.h>

bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);

typedef struct woort_CodeEnv woort_CodeEnv;

bool woort_CodeEnv_create(
    woort_Vector* /* woort_Bytecode */ moving_bytecodes,
    woort_Vector* /* woort_Value */ moving_constants,
    size_t static_storage_count,
    woort_CodeEnv** out_code_env);

void woort_CodeEnv_share(woort_CodeEnv* code_env);
void woort_CodeEnv_unshare(woort_CodeEnv* code_env);

bool woort_CodeEnv_find(
    const woort_Bytecode* addr, woort_CodeEnv** out_code_env);