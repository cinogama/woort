#pragma once

/*
woort_ir_compiler.h
*/

#include "woort_ir_function.h"
#include "woort_codeenv.h"

typedef struct woort_IRCompiler
{
    char _;

}woort_IRCompiler;

void woort_IRCompiler_init(woort_IRCompiler* ir_compiler);
void woort_IRCompiler_deinit(woort_IRCompiler* ir_compiler);

woort_IRFunction* woort_IRCompiler_add_function(
    woort_IRCompiler* c, uint32_t param_count);

bool woort_IRCompiler_finish(woort_IRCompiler* c, woort_CodeEnv** out_cenv);
