#pragma once

/*
 * woort_ir_compiler.h
 *
 * 顶层 IR 编译器：管理函数、常量/静态存储，编排 finish 流程。
 */

#include "woort.h"

#include "woort_ir_function.h"
#include "woort_ir_srcloc.h"
#include "woort_codeenv.h"
#include "woort_diagnosis.h"

#include <stdbool.h>

struct woort_IRCompiler
{
    woort_LinkList /* woort_IRFunction */ m_ir_functions;

    uint32_t m_constant_alloc_count;
    uint32_t m_static_storage_alloc_count;

    woort_Vector /* woort_Bytecode */ m_commited_codes;

    /* 源码路径字符串池（intern 去重） */
    woort_StringPool m_string_pool;

};

void woort_IRCompiler_init(woort_IRCompiler* c);
void woort_IRCompiler_deinit(woort_IRCompiler* c);

/*
 * intern 一个路径字符串到编译器的字符串池。
 * 返回池中的稳定指针（相同内容返回相同指针）。
 * 返回 NULL 表示 OOM。
 */
WOORT_NODISCARD /* OPTIONAL */ const char* woort_IRCompiler_intern_string(
    woort_IRCompiler* c, const char* str);
