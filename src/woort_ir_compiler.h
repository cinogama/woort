#pragma once

/*
 * woort_ir_compiler.h
 *
 * WooRT IR 编译器接口
 *
 * 本文件定义了 woort_IRCompiler 的接口，用于管理整个 IR 编译过程。
 *
 * 典型使用流程：
 * 1. woort_IRCompiler_init() - 创建编译器实例
 * 2. woort_IRCompiler_allocate_global() - 分配全局存储索引
 * 3. woort_IRCompiler_add_function() - 添加函数定义
 * 4. 填充函数内容（通过 IRFunction 和 IRBlock 接口）
 * 5. woort_IRCompiler_finish() - 完成编译，生成 woort_CodeEnv
 * 6. woort_IRCompiler_deinit() - 销毁编译器
 *
 * 示例：
 *   woort_IRCompiler* irc;
 *   woort_IRCompiler_init(&irc);
 *
 *   // 分配常量
 *   woort_IRGlobalIndex idx_42 = woort_IRCompiler_allocate_global(irc);
 *
 *   // 创建函数
 *   woort_IRFunction* func;
 *   woort_IRCompiler_add_function(irc, &func);
 *
 *   // 填充函数内容...
 *
 *   // 完成编译
 *   woort_CodeEnv* code_env;
 *   woort_IRCompiler_finish(irc, &code_env);
 *
 *   // 填充常量值
 *   code_env->m_data_begin[idx_42].m_integer = 42;
 *
 *   woort_IRCompiler_deinit(irc);
 */

#include "woort_ir_types.h"

#include <stdbool.h>

struct woort_CodeEnv;

/*
 * woort_IRCompiler_init
 *
 * 初始化一个 IR 编译器实例。
 *
 * 参数：
 *   out_compiler - 输出参数，接收新创建的编译器实例指针
 *
 * 返回值：
 *   true - 初始化成功
 *   false - 内存分配失败
 *
 * 注意：
 * - 必须在使用编译器前调用
 * - 使用完毕后必须调用 woort_IRCompiler_deinit() 释放资源
 */
WOORT_NODISCARD bool woort_IRCompiler_init(
    woort_IRCompiler** out_compiler);

/*
 * woort_IRCompiler_deinit
 *
 * 销毁 IR 编译器实例并释放所有相关资源。
 *
 * 参数：
 *   compiler - 要销毁的编译器实例
 *
 * 注意：
 * - 销毁后，所有由此编译器创建的 IRValue、IRStorage、IRFunction、IRBlock
 *   指针都将失效
 * - 如果已经调用 woort_IRCompiler_finish() 生成了 CodeEnv，CodeEnv 不受影响
 * - 可以传入 NULL，此时函数不做任何操作
 */
void woort_IRCompiler_deinit(
    /* OPTIONAL */ woort_IRCompiler* compiler);

/*
 * woort_IRCompiler_allocate_global
 *
 * 分配一个全局存储索引。
 *
 * 参数：
 *   compiler - 编译器实例
 *
 * 返回值：
 *   新分配的全局存储索引
 *
 * 说明：
 * - 全局存储索引用于引用 CodeEnv 中的常量或静态存储单元
 * - 索引从 0 开始，按调用顺序递增
 * - 在调用 woort_IRCompiler_finish() 后，可通过 code_env->m_data_begin[index]
 *   访问对应的存储单元
 *
 * 使用场景：
 * - 存储常量值（整数、实数、字符串等）
 * - 存储函数引用（脚本函数、原生函数、JIT 函数）
 * - 静态存储区
 *
 * 示例：
 *   woort_IRGlobalIndex idx = woort_IRCompiler_allocate_global(irc);
 *   // 稍后填充：
 *   code_env->m_data_begin[idx].m_integer = 42;
 */
WOORT_NODISCARD woort_IRGlobalIndex woort_IRCompiler_allocate_global(
    woort_IRCompiler* compiler);

/*
 * woort_IRCompiler_add_function
 *
 * 向编译器添加一个新函数。
 *
 * 参数：
 *   compiler    - 编译器实例
 *   out_function - 输出参数，接收新创建的函数实例指针
 *
 * 返回值：
 *   true  - 函数创建成功
 *   false - 内存分配失败
 *
 * 说明：
 * - 每个函数自动创建一个入口基本块（Entry Block）
 * - 可通过 woort_IRFunction_get_entry_block() 获取入口块
 * - 函数参数通过 woort_IRFunction_load_argument() 访问
 * - 函数内容通过 IRBlock 接口填充
 *
 * 函数顺序：
 * - 函数按添加顺序在最终字节码中排列
 * - 第一个添加的函数的起始地址为 code_env->m_code_begin
 * - 后续函数可通过偏移量访问
 *
 * 示例：
 *   woort_IRFunction* func;
 *   if (!woort_IRCompiler_add_function(irc, &func)) {
 *       // 处理错误...
 *   }
 *   woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
 */
WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* compiler,
    woort_IRFunction** out_function);

/*
 * woort_IRCompiler_finish
 *
 * 完成 IR 编译，生成可执行的 woort_CodeEnv。
 *
 * 参数：
 *   compiler     - 编译器实例
 *   out_code_env - 输出参数，接收生成的 CodeEnv 指针
 *
 * 返回值：
 *   true  - 编译成功
 *   false - 编译失败（如未完成的块、无效的控制流等）
 *
 * 说明：
 * - 此函数执行以下操作：
 *   1. 验证所有基本块都有终止指令
 *   2. 解析所有跳转目标
 *   3. 执行指令选择和优化
 *   4. 分配栈空间和寄存器
 *   5. 生成最终字节码
 *
 * - 生成的 CodeEnv 包含：
 *   - 所有函数的字节码（m_code_begin 到 m_code_end）
 *   - 全局存储区（m_data_begin），大小等于 allocate_global 调用次数
 *
 * - 生成后需要手动填充全局存储区的值：
 *   code_env->m_data_begin[idx].m_integer = 42;
 *   code_env->m_data_begin[func_idx].m_script_function = ...;
 *
 * 注意：
 * - 调用此函数后，编译器仍然可用，但不能再添加新函数
 * - 生成的 CodeEnv 需要使用 woort_CodeEnv_drop() 释放
 * - 如果编译失败，out_code_env 被设置为 NULL
 *
 * 示例：
 *   woort_CodeEnv* code_env;
 *   if (!woort_IRCompiler_finish(irc, &code_env)) {
 *       // 处理编译错误...
 *   }
 *   // 填充常量
 *   code_env->m_data_begin[const_idx].m_integer = 42;
 *   // 使用 code_env...
 *   woort_CodeEnv_drop(code_env);
 */
WOORT_NODISCARD bool woort_IRCompiler_finish(
    woort_IRCompiler* compiler,
    /* OPTIONAL */ woort_CodeEnv** out_code_env);

/*
 * woort_IRCompiler_get_function_count
 *
 * 获取已添加的函数数量。
 *
 * 参数：
 *   compiler - 编译器实例
 *
 * 返回值：
 *   已添加的函数数量
 */
WOORT_NODISCARD size_t woort_IRCompiler_get_function_count(
    const woort_IRCompiler* compiler);

/*
 * woort_IRCompiler_get_global_count
 *
 * 获取已分配的全局存储索引数量。
 *
 * 参数：
 *   compiler - 编译器实例
 *
 * 返回值：
 *   已分配的全局存储索引数量
 */
WOORT_NODISCARD size_t woort_IRCompiler_get_global_count(
    const woort_IRCompiler* compiler);

/*
 * woort_IRCompiler_set_entry_function
 *
 * 设置程序的入口函数。
 *
 * 参数：
 *   compiler - 编译器实例
 *   function - 入口函数，VM 将从此函数开始执行
 *
 * 返回值：
 *   true  - 设置成功
 *   false - 设置失败（如函数不属于此编译器）
 *
 * 说明：
 * - 入口函数是 VM 启动时执行的第一个函数
 * - 如果不调用此函数，默认使用第一个添加的函数作为入口
 * - 入口函数应该无参数或从命令行获取参数
 */
WOORT_NODISCARD bool woort_IRCompiler_set_entry_function(
    woort_IRCompiler* compiler,
    woort_IRFunction* function);
