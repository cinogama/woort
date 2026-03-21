#pragma once

/*
 * woort_ir_compiler.h
 */

#include "woort_ir_types.h"
#include "woort_diagnosis.h"

#include <stdbool.h>

/*
 * IRCompiler - IR 编译器上下文
 *
 * 负责管理整个 IR 编译过程，包含多个函数和全局存储区域。
 */

/*
 * 初始化 IRCompiler
 * 
 * out_compiler: 输出编译器指针
 * 返回: 成功返回 true
 */
WOORT_NODISCARD bool woort_IRCompiler_init(woort_IRCompiler** out_compiler);

/*
 * 销毁 IRCompiler
 * 
 * compiler: 编译器指针
 */
void woort_IRCompiler_drop(woort_IRCompiler* compiler);

/*
 * 分配全局存储槽
 * 
 * compiler: 编译器指针
 * 返回: 全局索引
 */
WOORT_NODISCARD woort_IRGlobalIndex woort_IRCompiler_alloc_global(woort_IRCompiler* compiler);

/*
 * 添加函数
 * 
 * compiler: 编译器指针
 * param_count: 函数参数数量
 * out_func: 输出函数指针
 * 返回: 成功返回 true
 */
WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* compiler,
    uint32_t param_count,
    woort_IRFunction** out_func);

/*
 * 完成编译，生成 CodeEnv
 * 
 * compiler: 编译器指针
 * out_codeenv: 输出 CodeEnv 指针
 * 返回: 成功返回 true，失败时调用 woort_IRCompiler_get_error 获取错误信息
 */
WOORT_NODISCARD bool woort_IRCompiler_finish(
    woort_IRCompiler* compiler,
    woort_CodeEnv** out_codeenv);

/*
 * 获取最后的错误信息
 * 
 * compiler: 编译器指针
 * 返回: 错误信息字符串（如果没有错误则返回空字符串）
 */
WOORT_NODISCARD const char* woort_IRCompiler_get_error(woort_IRCompiler* compiler);
