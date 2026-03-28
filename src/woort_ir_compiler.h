#pragma once

/*
 * woort_ir_compiler.h
 *
 * 顶层 IR 编译器：管理函数、常量/静态存储，编排 finish 流程。
 */

#include "woort_ir_function.h"
#include "woort_codeenv.h"
#include "woort_diagnosis.h"

#include <stdbool.h>

typedef struct woort_IRCompiler
{
    woort_LinkList /* woort_IRFunction */ m_ir_functions;

    uint32_t m_constant_alloc_count;
    uint32_t m_static_storage_alloc_count;

    woort_Vector /* woort_Bytecode */ m_commited_codes;

} woort_IRCompiler;

void woort_IRCompiler_init(woort_IRCompiler* c);
void woort_IRCompiler_deinit(woort_IRCompiler* c);

WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* c, uint32_t param_count, woort_IRFunction** out_f);

WOORT_NODISCARD woort_IRConstantIndex woort_IRCompiler_add_constant(woort_IRCompiler* c);
WOORT_NODISCARD woort_IRStaticIndex woort_IRCompiler_add_static(woort_IRCompiler* c);

WOORT_NODISCARD bool woort_IRCompiler_finish(woort_IRCompiler* c, woort_CodeEnv** out_cenv);
