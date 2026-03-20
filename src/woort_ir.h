#pragma once

/*
 * woort_ir.h
 * 
 * WooRT IR 编译器接口
 * 
 * 提供类似 LLVM/AsmJit 的无限虚拟寄存器 IR，用于生成 WooRT 字节码。
 * 
 * 设计原则：
 * - 操作数不需要带有类型信息，类型检查由 Woolang 编译器前端负责
 * - 提供无限虚拟寄存器（SSA 形式），IR 负责决定最终生成的指令
 * - 以 Block 为基础，不提供显式跳转指令，由 IR 在块之间自然插入
 * - 提供 Storage 用于非 SSA 形式的 load/store，IR 自动转化为 PHI 指令
 * - 做好指令选择，保证生成的字节码指令数量少、寻址次数少
 */

#include "woort_diagnosis.h"
#include "woort_codeenv.h"

#include <stddef.h>
#include <stdbool.h>

/*******************************************************************************
 * 前向类型声明
 ******************************************************************************/

/*
 * woort_IRGlobalIndex
 * 
 * 全局存储索引，用于标识 CodeEnv 中常量池/静态存储区域的一个槽位。
 * 通过 woort_IRCompiler_allocate_global() 分配，可在编译完成后填充具体值。
 */
typedef size_t woort_IRGlobalIndex;

/*
 * woort_IRCompiler
 * 
 * IR 编译器，管理整个编译过程。
 * 
 * 职责：
 * - 管理全局存储索引的分配
 * - 管理所有 IRFunction 的生命周期
 * - 最终生成 woort_CodeEnv
 * 
 * 用法：
 *   woort_IRCompiler* irc;
 *   woort_IRCompiler_create(&irc);
 *   // ... 添加函数、生成 IR ...
 *   woort_CodeEnv* env;
 *   woort_IRCompiler_finish(irc, &env);
 *   woort_IRCompiler_destroy(irc);
 */
typedef struct woort_IRCompiler woort_IRCompiler;

/*
 * woort_IRFunction
 * 
 * IR 函数，表示一个待编译的函数。
 * 
 * 一个函数包含多个基本块（IRBlock），其中有一个入口块。
 * 函数参数通过 woort_IRFunction_load_argument() 获取。
 */
typedef struct woort_IRFunction woort_IRFunction;

/*
 * woort_IRBlock
 * 
 * IR 基本块，是 IR 构建的基本单位。
 * 
 * 特性：
 * - 一个基本块是一段线性代码，没有内部跳转
 * - 基本块以终结指令结束（ret、br、condbr 等）
 * - 块之间的跳转由 IR 自动处理，不需要显式编写跳转指令
 * - 大部分指令生成接口都在 IRBlock 上
 */
typedef struct woort_IRBlock woort_IRBlock;

/*
 * woort_IRValue
 * 
 * IR 值，表示 SSA 形式的虚拟寄存器。
 * 
 * 特性：
 * - 每个 IRValue 只被赋值一次（SSA 特性）
 * - IRValue 是只读的，创建后不可修改
 * - 可作为指令的操作数传递
 */
typedef struct woort_IRValue woort_IRValue;

/*
 * woort_IRStorage
 * 
 * IR 存储位置，用于需要多次赋值的场景。
 * 
 * 当 SSA 形式编写不方便时，可以使用 Storage：
 * - Storage 允许多次 load 和 store
 * - IR 会自动将 Storage 的使用转化为 PHI 指令
 * 
 * 用法：
 *   woort_IRStorage* storage = woort_IRFunction_create_storage(func);
 *   woort_IRBlock_store(irblock, storage, value1);
 *   const woort_IRValue* loaded = woort_IRBlock_load(irblock, storage);
 */
typedef struct woort_IRStorage woort_IRStorage;

/*******************************************************************************
 * IRCompiler 全局资源管理
 * 
 * IRCompiler 使用全局资源池来管理内存分配，需要在使用前初始化。
 ******************************************************************************/

/*
 * woort_IRCompiler_bootup
 * 
 * 初始化 IRCompiler 所需的全局资源。
 * 
 * 必须在创建任何 IRCompiler 之前调用一次。
 * 通常在 woort_init() 之后调用。
 * 
 * 返回：
 *   true  - 初始化成功
 *   false - 初始化失败（资源不足）
 */
WOORT_NODISCARD bool woort_IRCompiler_bootup(void);

/*
 * woort_IRCompiler_shutdown
 * 
 * 释放 IRCompiler 的全局资源。
 * 
 * 必须在销毁所有 IRCompiler 之后调用。
 * 通常在 woort_shutdown() 之前调用。
 */
void woort_IRCompiler_shutdown(void);

/*******************************************************************************
 * IRCompiler 实例管理
 ******************************************************************************/

/*
 * woort_IRCompiler_create
 * 
 * 创建一个新的 IR 编译器实例。
 * 
 * 参数：
 *   out_compiler - 输出参数，接收创建的编译器实例指针
 * 
 * 返回：
 *   true  - 创建成功
 *   false - 创建失败（内存不足）
 * 
 * 用法：
 *   woort_IRCompiler* irc;
 *   if (!woort_IRCompiler_create(&irc)) {
 *       // 处理错误
 *   }
 */
WOORT_NODISCARD bool woort_IRCompiler_create(
    /* OPTIONAL */ woort_IRCompiler** out_compiler);

/*
 * woort_IRCompiler_destroy
 * 
 * 销毁 IR 编译器实例及其管理的所有资源。
 * 
 * 注意：
 * - 如果已经调用 woort_IRCompiler_finish() 生成了 CodeEnv，
 *   生成的 CodeEnv 仍然有效，需要单独调用 woort_CodeEnv_drop() 释放
 * - 如果未调用 finish()，所有未完成的 IR 数据将被释放
 * 
 * 参数：
 *   compiler - 要销毁的编译器实例
 */
void woort_IRCompiler_destroy(
    /* OPTIONAL */ woort_IRCompiler* compiler);

/*******************************************************************************
 * 全局存储管理
 ******************************************************************************/

/*
 * woort_IRCompiler_allocate_global
 * 
 * 在 CodeEnv 的常量池/静态存储区域分配一个槽位。
 * 
 * 返回的索引可用于：
 * - woort_IRFunction_load_const() 加载常量值
 * - woort_IRBlock_CALLNWO() 等调用指令引用函数
 * 
 * 分配的槽位在 woort_IRCompiler_finish() 后可通过 CodeEnv 填充实际值。
 * 
 * 参数：
 *   compiler - 编译器实例
 * 
 * 返回：
 *   分配的全局存储索引
 * 
 * 用法：
 *   woort_IRGlobalIndex const_idx = woort_IRCompiler_allocate_global(irc);
 *   // 编译完成后：
 *   codeenv->m_data_begin[const_idx].m_integer = 42;
 */
WOORT_NODISCARD woort_IRGlobalIndex woort_IRCompiler_allocate_global(
    woort_IRCompiler* compiler);

/*
 * woort_IRCompiler_allocate_global_range
 * 
 * 在 CodeEnv 的常量池/静态存储区域分配连续的多个槽位。
 * 
 * 参数：
 *   compiler    - 编译器实例
 *   count       - 要分配的槽位数量
 *   out_begin   - 输出参数，接收起始索引（可选）
 * 
 * 返回：
 *   true  - 分配成功
 *   false - 分配失败
 * 
 * 用法：
 *   woort_IRGlobalIndex begin;
 *   woort_IRCompiler_allocate_global_range(irc, 10, &begin);
 *   // 索引范围为 [begin, begin + 10)
 */
WOORT_NODISCARD bool woort_IRCompiler_allocate_global_range(
    woort_IRCompiler* compiler,
    size_t count,
    /* OPTIONAL */ woort_IRGlobalIndex* out_begin);

/*******************************************************************************
 * 函数管理
 ******************************************************************************/

/*
 * woort_IRCompiler_add_function
 * 
 * 向编译器添加一个新函数。
 * 
 * 新创建的函数会自动包含一个入口基本块。
 * 
 * 参数：
 *   compiler     - 编译器实例
 *   out_function - 输出参数，接收创建的函数指针
 * 
 * 返回：
 *   true  - 创建成功
 *   false - 创建失败（内存不足）
 * 
 * 用法：
 *   woort_IRFunction* func;
 *   if (!woort_IRCompiler_add_function(irc, &func)) {
 *       // 处理错误
 *   }
 */
WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* compiler,
    /* OPTIONAL */ woort_IRFunction** out_function);

/*
 * woort_IRCompiler_get_function_count
 * 
 * 获取编译器中的函数数量。
 * 
 * 参数：
 *   compiler - 编译器实例
 * 
 * 返回：
 *   函数数量
 */
WOORT_NODISCARD size_t woort_IRCompiler_get_function_count(
    const woort_IRCompiler* compiler);

/*******************************************************************************
 * 编译完成
 ******************************************************************************/

/*
 * woort_IRCompiler_finish
 * 
 * 完成 IR 编译，生成可执行的 CodeEnv。
 * 
 * 此函数会：
 * 1. 对所有函数执行寄存器分配
 * 2. 执行指令选择和优化
 * 3. 生成最终的字节码
 * 4. 创建 CodeEnv 实例
 * 
 * 调用此函数后，编译器实例仍然有效，可以继续添加新函数，
 * 但之前添加的函数数据已被消费，不可再次使用。
 * 
 * 参数：
 *   compiler     - 编译器实例
 *   out_code_env - 输出参数，接收生成的 CodeEnv
 * 
 * 返回：
 *   true  - 编译成功
 *   false - 编译失败（如 IR 非法、内存不足等）
 * 
 * 用法：
 *   woort_CodeEnv* env;
 *   if (!woort_IRCompiler_finish(irc, &env)) {
 *       // 处理错误
 *   }
 *   // 填充常量...
 *   codeenv->m_data_begin[0].m_integer = 42;
 */
WOORT_NODISCARD bool woort_IRCompiler_finish(
    woort_IRCompiler* compiler,
    /* OPTIONAL */ woort_CodeEnv** out_code_env);

/*
 * woort_IRCompiler_finish_function
 * 
 * 完成 IR 编译，生成单个函数的 CodeEnv。
 * 
 * 仅编译指定的函数，不编译其他函数。
 * 适用于增量编译或测试场景。
 * 
 * 参数：
 *   compiler     - 编译器实例
 *   function     - 要编译的函数
 *   out_code_env - 输出参数，接收生成的 CodeEnv
 * 
 * 返回：
 *   true  - 编译成功
 *   false - 编译失败
 */
WOORT_NODISCARD bool woort_IRCompiler_finish_function(
    woort_IRCompiler* compiler,
    woort_IRFunction* function,
    /* OPTIONAL */ woort_CodeEnv** out_code_env);

/*******************************************************************************
 * IRFunction 接口
 * 
 * IRFunction 表示一个待编译的函数，包含基本块、参数和局部存储。
 ******************************************************************************/

/*
 * woort_IRFunction_get_entry_block
 * 
 * 获取函数的入口基本块。
 * 
 * 入口块是函数执行时首先进入的块，函数参数在入口块中可用。
 * 每个函数创建时自动包含一个入口块。
 * 
 * 参数：
 *   function - 函数实例
 * 
 * 返回：
 *   入口基本块指针，永不失败
 */
WOORT_NODISCARD woort_IRBlock* woort_IRFunction_get_entry_block(
    woort_IRFunction* function);

/*
 * woort_IRFunction_add_block
 * 
 * 向函数添加一个新的基本块。
 * 
 * 新块不与任何块连接，需要通过控制流指令（br、condbr）连接到现有块。
 * 
 * 参数：
 *   function   - 函数实例
 *   out_block  - 输出参数，接收创建的基本块指针
 * 
 * 返回：
 *   true  - 创建成功
 *   false - 创建失败（内存不足）
 * 
 * 用法：
 *   woort_IRBlock* then_block;
 *   woort_IRBlock* else_block;
 *   woort_IRFunction_add_block(func, &then_block);
 *   woort_IRFunction_add_block(func, &else_block);
 */
WOORT_NODISCARD bool woort_IRFunction_add_block(
    woort_IRFunction* function,
    /* OPTIONAL */ woort_IRBlock** out_block);

/*
 * woort_IRFunction_load_const
 * 
 * 加载全局存储中的常量值，创建一个 IRValue。
 * 
 * 参数：
 *   function       - 函数实例
 *   global_index   - 全局存储索引（由 woort_IRCompiler_allocate_global 返回）
 * 
 * 返回：
 *   表示该常量值的 IRValue 指针
 * 
 * 注意：
 *   常量值在 woort_IRCompiler_finish() 之后通过 CodeEnv 填充。
 *   同一个 global_index 多次调用会返回相同的 IRValue。
 */
WOORT_NODISCARD const woort_IRValue* woort_IRFunction_load_const(
    woort_IRFunction* function,
    woort_IRGlobalIndex global_index);

/*
 * woort_IRFunction_load_argument
 * 
 * 加载函数参数，创建一个 IRValue。
 * 
 * 参数索引从 0 开始，对应函数的第一个参数。
 * 
 * 参数：
 *   function       - 函数实例
 *   argument_index - 参数索引（0 = 第一个参数）
 * 
 * 返回：
 *   表示该参数的 IRValue 指针
 * 
 * 注意：
 *   - 参数索引必须在有效范围内（编译时会检查）
 *   - 同一个参数多次调用会返回相同的 IRValue
 * 
 * 用法：
 *   // 获取第一个参数
 *   const woort_IRValue* arg0 = woort_IRFunction_load_argument(func, 0);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRFunction_load_argument(
    woort_IRFunction* function,
    size_t argument_index);

/*
 * woort_IRFunction_create_storage
 * 
 * 创建一个存储位置，用于需要多次赋值的变量。
 * 
 * 当 SSA 形式编写不方便时（如循环中的累加器、条件分支中的变量），
 * 可以使用 Storage 进行 load/store 操作，IR 会自动转化为 PHI 指令。
 * 
 * 参数：
 *   function - 函数实例
 * 
 * 返回：
 *   新创建的 Storage 指针，失败返回 NULL
 * 
 * 用法：
 *   woort_IRStorage* acc = woort_IRFunction_create_storage(func);
 *   // 初始化
 *   woort_IRBlock_store(entry_block, acc, init_value);
 *   // 在循环中修改
 *   const woort_IRValue* loaded = woort_IRBlock_load(loop_block, acc);
 *   const woort_IRValue* new_val = woort_IRBlock_ADDI(loop_block, loaded, one);
 *   woort_IRBlock_store(loop_block, acc, new_val);
 */
WOORT_NODISCARD /* OPTIONAL */ woort_IRStorage* woort_IRFunction_create_storage(
    woort_IRFunction* function);

/*
 * woort_IRFunction_get_compiler
 * 
 * 获取函数所属的编译器实例。
 * 
 * 参数：
 *   function - 函数实例
 * 
 * 返回：
 *   所属的 IRCompiler 指针
 */
WOORT_NODISCARD woort_IRCompiler* woort_IRFunction_get_compiler(
    const woort_IRFunction* function);

/*******************************************************************************
 * IRBlock 控制流接口
 * 
 * 基本块之间的控制流转移。所有控制流指令都是终结指令，
 * 执行后基本块不可再添加其他指令。
 ******************************************************************************/

/*
 * woort_IRBlock_br
 * 
 * 无条件跳转到目标基本块。
 * 
 * 这是一个终结指令，执行后当前基本块结束。
 * 
 * 参数：
 *   block       - 当前基本块
 *   target      - 跳转目标基本块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败（如块已有终结指令）
 * 
 * 用法：
 *   woort_IRBlock_br(entry_block, next_block);
 */
WOORT_NODISCARD bool woort_IRBlock_br(
    woort_IRBlock* block,
    woort_IRBlock* target);

/*
 * woort_IRBlock_condbr_less_then
 * 
 * 条件跳转：如果 lhs < rhs，跳转到 then_block，否则跳转到 else_block。
 * 
 * 这是一个终结指令。
 * 
 * 参数：
 *   block       - 当前基本块
 *   lhs         - 左操作数
 *   rhs         - 右操作数
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 用法：
 *   // if (arg0 < 2) goto less_block else goto greater_block
 *   woort_IRBlock_condbr_less_then(entry, arg0, const_2, less_block, greater_block);
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_less_then(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_greater_then
 * 
 * 条件跳转：如果 lhs > rhs，跳转到 then_block，否则跳转到 else_block。
 * 
 * 参数：
 *   block       - 当前基本块
 *   lhs         - 左操作数
 *   rhs         - 右操作数
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_greater_then(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_less_equal
 * 
 * 条件跳转：如果 lhs <= rhs，跳转到 then_block，否则跳转到 else_block。
 * 
 * 参数：
 *   block       - 当前基本块
 *   lhs         - 左操作数
 *   rhs         - 右操作数
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_less_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_greater_equal
 * 
 * 条件跳转：如果 lhs >= rhs，跳转到 then_block，否则跳转到 else_block。
 * 
 * 参数：
 *   block       - 当前基本块
 *   lhs         - 左操作数
 *   rhs         - 右操作数
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_greater_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_equal
 * 
 * 条件跳转：如果 lhs == rhs，跳转到 then_block，否则跳转到 else_block。
 * 
 * 参数：
 *   block       - 当前基本块
 *   lhs         - 左操作数
 *   rhs         - 右操作数
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_not_equal
 * 
 * 条件跳转：如果 lhs != rhs，跳转到 then_block，否则跳转到 else_block。
 * 
 * 参数：
 *   block       - 当前基本块
 *   lhs         - 左操作数
 *   rhs         - 右操作数
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_not_equal(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_true
 * 
 * 条件跳转：如果 cond 为真（非零），跳转到 then_block，否则跳转到 else_block。
 * 
 * 参数：
 *   block       - 当前基本块
 *   cond        - 条件值（布尔或整数）
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_true(
    woort_IRBlock* block,
    const woort_IRValue* cond,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*
 * woort_IRBlock_condbr_false
 * 
 * 条件跳转：如果 cond 为假（零），跳转到 then_block，否则跳转到 else_block。
 * 
 * 参数：
 *   block       - 当前基本块
 *   cond        - 条件值（布尔或整数）
 *   then_block  - 条件为真时跳转的块
 *   else_block  - 条件为假时跳转的块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_condbr_false(
    woort_IRBlock* block,
    const woort_IRValue* cond,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block);

/*******************************************************************************
 * IRBlock 返回指令
 * 
 * 函数返回指令，用于结束函数执行并返回控制权给调用者。
 ******************************************************************************/

/*
 * woort_IRBlock_ret
 * 
 * 返回一个值给调用者。
 * 
 * 这是一个终结指令。
 * 
 * 参数：
 *   block       - 当前基本块
 *   value       - 要返回的值
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 用法：
 *   woort_IRBlock_ret(block, result_value);
 */
WOORT_NODISCARD bool woort_IRBlock_ret(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*
 * woort_IRBlock_ret_void
 * 
 * 返回 void（无返回值）。
 * 
 * 这是一个终结指令。
 * 
 * 参数：
 *   block       - 当前基本块
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_ret_void(
    woort_IRBlock* block);

/*******************************************************************************
 * IRBlock 函数调用指令
 * 
 * 函数调用指令用于调用不同类型的目标函数：
 * - CALL:     调用脚本函数（通过 IRValue 表示的函数指针）
 * - CALLNWO:  调用脚本函数（通过全局索引）
 * - CALLNFP:  调用原生函数（Native Function Pointer）
 * - CALLNJIT: 调用 JIT 编译的函数
 * 
 * 调用约定：
 * - 参数需要先通过 PUSH 压入栈中
 * - 参数按逆序压入（栈顶是第一个参数）
 * - argc 参数用于生成正确的栈清理指令（POPR/RESULT）
 ******************************************************************************/

/*
 * woort_IRBlock_CALL
 * 
 * 调用一个脚本函数（通过 IRValue 表示的函数指针）。
 * 
 * 参数：
 *   block         - 当前基本块
 *   callee        - 被调用函数的 IRValue（必须是一个函数类型的值）
 *   argc          - 参数数量
 *   out_result    - 输出参数，接收返回值（可选，传 NULL 表示忽略返回值）
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 参数需要先通过 PUSH 压栈
 *   - argc 用于生成栈清理指令
 *   - 如果 out_result 为 NULL，生成 POPR 指令清理返回值
 *   - 如果 out_result 非 NULL，生成 RESULT 指令获取返回值
 * 
 * 用法：
 *   woort_IRBlock_PUSH(block, arg0);
 *   woort_IRBlock_PUSH(block, arg1);
 *   const woort_IRValue* result;
 *   woort_IRBlock_CALL(block, func_value, 2, &result);
 */
WOORT_NODISCARD bool woort_IRBlock_CALL(
    woort_IRBlock* block,
    const woort_IRValue* callee,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * woort_IRBlock_CALLNWO
 * 
 * 调用一个脚本函数（通过全局索引，函数存储在 CodeEnv 的常量池中）。
 * 
 * 参数：
 *   block         - 当前基本块
 *   func_index    - 函数的全局存储索引
 *   argc          - 参数数量
 *   out_result    - 输出参数，接收返回值（可选）
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 这是调用同一 CodeEnv 中其他脚本函数的推荐方式
 *   - 递归调用时使用相同的 func_index
 * 
 * 用法：
 *   woort_IRBlock_PUSH(block, arg);
 *   const woort_IRValue* result;
 *   woort_IRBlock_CALLNWO(block, fib_func_index, 1, &result);
 */
WOORT_NODISCARD bool woort_IRBlock_CALLNWO(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * woort_IRBlock_CALLNFP
 * 
 * 调用一个原生函数（Native Function Pointer）。
 * 
 * 参数：
 *   block         - 当前基本块
 *   func_index    - 原生函数的全局存储索引
 *   argc          - 参数数量
 *   out_result    - 输出参数，接收返回值（可选）
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 原生函数指针在 CodeEnv 填充时设置
 *   - 原生函数签名为 woort_NativeFunction (woort_api (*)(woort_vm, woort_value*))
 * 
 * 用法：
 *   woort_IRBlock_PUSH(block, value_to_print);
 *   woort_IRBlock_CALLNFP(block, print_int_index, 1, NULL);
 */
WOORT_NODISCARD bool woort_IRBlock_CALLNFP(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*
 * woort_IRBlock_CALLNJIT
 * 
 * 调用一个 JIT 编译的函数。
 * 
 * 参数：
 *   block         - 当前基本块
 *   func_index    - JIT 函数的全局存储索引
 *   argc          - 参数数量
 *   out_result    - 输出参数，接收返回值（可选）
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - JIT 函数在运行时动态编译
 *   - 首次调用时可能触发编译
 */
WOORT_NODISCARD bool woort_IRBlock_CALLNJIT(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t argc,
    /* OPTIONAL */ const woort_IRValue** out_result);

/*******************************************************************************
 * IRBlock 栈操作指令
 * 
 * 栈操作用于函数调用前压入参数。
 * 参数按逆序压入（最后一个参数先压入，第一个参数最后压入）。
 ******************************************************************************/

/*
 * woort_IRBlock_PUSH
 * 
 * 将一个值压入栈中，为函数调用准备参数。
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 要压入的值
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 参数需要按逆序压入
 *   - 例如调用 f(a, b, c)，压入顺序为：PUSH(c), PUSH(b), PUSH(a)
 * 
 * 用法：
 *   // 调用 fib(n - 1)
 *   woort_IRBlock_PUSH(block, n_sub_1);
 *   woort_IRBlock_CALLNWO(block, fib_index, 1, &result);
 */
WOORT_NODISCARD bool woort_IRBlock_PUSH(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*
 * woort_IRBlock_PUSH_const
 * 
 * 将一个常量值压入栈中（便捷方法，等同于 load_const + PUSH）。
 * 
 * 参数：
 *   block         - 当前基本块
 *   global_index  - 常量的全局存储索引
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_PUSH_const(
    woort_IRBlock* block,
    woort_IRGlobalIndex global_index);

/*******************************************************************************
 * IRBlock 整数算术指令 (I = Integer)
 * 
 * 整数类型的算术运算指令。操作数和结果都是整数类型。
 * IR 会自动选择最优的字节码指令（如使用累加指令优化）。
 ******************************************************************************/

/*
 * woort_IRBlock_ADDI
 * 
 * 整数加法：result = lhs + rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示加法结果的 IRValue 指针
 * 
 * 用法：
 *   const woort_IRValue* sum = woort_IRBlock_ADDI(block, a, b);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ADDI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_SUBI
 * 
 * 整数减法：result = lhs - rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（被减数）
 *   rhs     - 右操作数（减数）
 * 
 * 返回：
 *   表示减法结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_SUBI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_MULI
 * 
 * 整数乘法：result = lhs * rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示乘法结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MULI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_DIVI
 * 
 * 整数除法：result = lhs / rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（被除数）
 *   rhs     - 右操作数（除数）
 * 
 * 返回：
 *   表示除法结果的 IRValue 指针
 * 
 * 注意：
 *   - 除数为零时行为未定义（运行时可能 panic）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_DIVI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_MODI
 * 
 * 整数取模：result = lhs % rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（被除数）
 *   rhs     - 右操作数（除数）
 * 
 * 返回：
 *   表示取模结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MODI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_NEGI
 * 
 * 整数取负：result = -value
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 操作数
 * 
 * 返回：
 *   表示取负结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NEGI(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 整数比较指令
 * 
 * 整数比较指令返回布尔值。
 ******************************************************************************/

/*
 * woort_IRBlock_LTI
 * 
 * 整数小于比较：result = (lhs < rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LTI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_GTI
 * 
 * 整数大于比较：result = (lhs > rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GTI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_LEI
 * 
 * 整数小于等于比较：result = (lhs <= rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LEI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_GEI
 * 
 * 整数大于等于比较：result = (lhs >= rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GEI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_EQI
 * 
 * 整数等于比较：result = (lhs == rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_NEI
 * 
 * 整数不等于比较：result = (lhs != rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NEI(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*******************************************************************************
 * IRBlock 实数算术指令 (R = Real)
 * 
 * 实数（浮点数）类型的算术运算指令。
 ******************************************************************************/

/*
 * woort_IRBlock_ADDR
 * 
 * 实数加法：result = lhs + rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示加法结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ADDR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_SUBR
 * 
 * 实数减法：result = lhs - rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（被减数）
 *   rhs     - 右操作数（减数）
 * 
 * 返回：
 *   表示减法结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_SUBR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_MULR
 * 
 * 实数乘法：result = lhs * rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示乘法结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MULR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_DIVR
 * 
 * 实数除法：result = lhs / rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（被除数）
 *   rhs     - 右操作数（除数）
 * 
 * 返回：
 *   表示除法结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_DIVR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_MODR
 * 
 * 实数取模：result = lhs % rhs（浮点数取模）
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（被除数）
 *   rhs     - 右操作数（除数）
 * 
 * 返回：
 *   表示取模结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MODR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_NEGR
 * 
 * 实数取负：result = -value
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 操作数
 * 
 * 返回：
 *   表示取负结果的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NEGR(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 实数比较指令
 * 
 * 实数比较指令返回布尔值。
 ******************************************************************************/

/*
 * woort_IRBlock_LTR
 * 
 * 实数小于比较：result = (lhs < rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LTR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_GTR
 * 
 * 实数大于比较：result = (lhs > rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GTR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_LER
 * 
 * 实数小于等于比较：result = (lhs <= rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LER(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_GER
 * 
 * 实数大于等于比较：result = (lhs >= rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GER(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_EQR
 * 
 * 实数等于比较：result = (lhs == rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_NER
 * 
 * 实数不等于比较：result = (lhs != rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数
 *   rhs     - 右操作数
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NER(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*******************************************************************************
 * IRBlock 字符串算术指令 (S = String)
 * 
 * 字符串类型的运算指令。字符串只支持连接和比较操作。
 ******************************************************************************/

/*
 * woort_IRBlock_ADDS
 * 
 * 字符串连接：result = lhs + rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（字符串）
 *   rhs     - 右操作数（字符串）
 * 
 * 返回：
 *   表示连接结果的 IRValue 指针（新字符串）
 * 
 * 注意：
 *   - 会分配新的 GC 字符串对象
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_ADDS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*******************************************************************************
 * IRBlock 字符串比较指令
 * 
 * 字符串比较指令返回布尔值。比较按字典序进行。
 ******************************************************************************/

/*
 * woort_IRBlock_LTS
 * 
 * 字符串小于比较：result = (lhs < rhs)（字典序）
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（字符串）
 *   rhs     - 右操作数（字符串）
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LTS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_GTS
 * 
 * 字符串大于比较：result = (lhs > rhs)（字典序）
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（字符串）
 *   rhs     - 右操作数（字符串）
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GTS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_LES
 * 
 * 字符串小于等于比较：result = (lhs <= rhs)（字典序）
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（字符串）
 *   rhs     - 右操作数（字符串）
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LES(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_GES
 * 
 * 字符串大于等于比较：result = (lhs >= rhs)（字典序）
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（字符串）
 *   rhs     - 右操作数（字符串）
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_GES(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_EQS
 * 
 * 字符串等于比较：result = (lhs == rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（字符串）
 *   rhs     - 右操作数（字符串）
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_EQS(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_NES
 * 
 * 字符串不等于比较：result = (lhs != rhs)
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（字符串）
 *   rhs     - 右操作数（字符串）
 * 
 * 返回：
 *   表示比较结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_NES(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*******************************************************************************
 * IRBlock 逻辑运算指令
 * 
 * 逻辑运算用于布尔值的组合和取反。
 ******************************************************************************/

/*
 * woort_IRBlock_LAND
 * 
 * 逻辑与：result = lhs && rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（布尔值）
 *   rhs     - 右操作数（布尔值）
 * 
 * 返回：
 *   表示逻辑与结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LAND(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_LOR
 * 
 * 逻辑或：result = lhs || rhs
 * 
 * 参数：
 *   block   - 当前基本块
 *   lhs     - 左操作数（布尔值）
 *   rhs     - 右操作数（布尔值）
 * 
 * 返回：
 *   表示逻辑或结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LOR(
    woort_IRBlock* block,
    const woort_IRValue* lhs,
    const woort_IRValue* rhs);

/*
 * woort_IRBlock_LNOT
 * 
 * 逻辑非：result = !value
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 操作数（布尔值）
 * 
 * 返回：
 *   表示逻辑非结果的 IRValue 指针（布尔值）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LNOT(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 数据构造指令
 * 
 * 用于创建复合数据类型：向量（Vec）、映射（Map）、结构体（Struct）和闭包（Closure）。
 ******************************************************************************/

/*
 * woort_IRBlock_MKVEC
 * 
 * 创建一个新的向量（动态数组）。
 * 
 * 参数：
 *   block       - 当前基本块
 *   element_count - 元素数量
 * 
 * 返回：
 *   表示新创建向量的 IRValue 指针
 * 
 * 注意：
 *   - 创建后元素为未初始化状态
 *   - 需要通过 STIDXVEC 指令设置元素
 *   - 向量是 GC 管理的对象
 * 
 * 用法：
 *   const woort_IRValue* vec = woort_IRBlock_MKVEC(block, 3);
 *   woort_IRBlock_STIDXVEC(block, vec, index0, elem0);
 *   woort_IRBlock_STIDXVEC(block, vec, index1, elem1);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKVEC(
    woort_IRBlock* block,
    size_t element_count);

/*
 * woort_IRBlock_MKMAP
 * 
 * 创建一个新的映射（字典/哈希表）。
 * 
 * 参数：
 *   block         - 当前基本块
 *   entry_count   - 预分配的条目数量
 * 
 * 返回：
 *   表示新创建映射的 IRValue 指针
 * 
 * 注意：
 *   - 创建后为空映射
 *   - 需要通过 STIDXMAP* 指令添加键值对
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKMAP(
    woort_IRBlock* block,
    size_t entry_count);

/*
 * woort_IRBlock_MKSTRUCT
 * 
 * 创建一个新的结构体实例。
 * 
 * 参数：
 *   block         - 当前基本块
 *   field_count   - 字段数量
 * 
 * 返回：
 *   表示新创建结构体的 IRValue 指针
 * 
 * 注意：
 *   - 字段通过数字索引访问（从 0 开始）
 *   - 需要通过 STIDSTRUCT 指令设置字段
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKSTRUCT(
    woort_IRBlock* block,
    size_t field_count);

/*
 * woort_IRBlock_MKCLOSURE
 * 
 * 创建一个闭包（捕获环境的函数）。
 * 
 * 参数：
 *   block           - 当前基本块
 *   func_index      - 函数的全局存储索引
 *   capture_count   - 捕获变量数量
 * 
 * 返回：
 *   表示新创建闭包的 IRValue 指针
 * 
 * 注意：
 *   - 捕获变量需要在创建后设置
 *   - 闭包可以作为函数调用
 * 
 * 用法：
 *   const woort_IRValue* closure = woort_IRBlock_MKCLOSURE(block, func_idx, 2);
 *   // 设置捕获变量...
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_MKCLOSURE(
    woort_IRBlock* block,
    woort_IRGlobalIndex func_index,
    size_t capture_count);

/*******************************************************************************
 * IRBlock 类型转换指令
 * 
 * 用于在不同类型之间进行转换。
 * 命名规则：CAST<src_type>_TO_<dst_type>
 ******************************************************************************/

/*
 * woort_IRBlock_CASTI_TO_R
 * 
 * 整数转实数：result = (real)integer
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 整数值
 * 
 * 返回：
 *   表示转换结果的 IRValue 指针（实数类型）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_CASTI_TO_R(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*
 * woort_IRBlock_CASTR_TO_I
 * 
 * 实数转整数：result = (int)real
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 实数值
 * 
 * 返回：
 *   表示转换结果的 IRValue 指针（整数类型）
 * 
 * 注意：
 *   - 执行向零截断（truncation）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_CASTR_TO_I(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*
 * woort_IRBlock_CASTI_TO_S
 * 
 * 整数转字符串：result = (string)integer
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 整数值
 * 
 * 返回：
 *   表示转换结果的 IRValue 指针（字符串类型）
 * 
 * 注意：
 *   - 会分配新的 GC 字符串对象
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_CASTI_TO_S(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*
 * woort_IRBlock_CASTR_TO_S
 * 
 * 实数转字符串：result = (string)real
 * 
 * 参数：
 *   block   - 当前基本块
 *   value   - 实数值
 * 
 * 返回：
 *   表示转换结果的 IRValue 指针（字符串类型）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_CASTR_TO_S(
    woort_IRBlock* block,
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 数据解包指令
 * 
 * 用于将复合数据类型的元素解包到栈或局部变量中。
 ******************************************************************************/

/*
 * woort_IRBlock_UNPACKVEC
 * 
 * 解包向量：将向量的所有元素压入栈中。
 * 
 * 参数：
 *   block   - 当前基本块
 *   vec     - 要解包的向量
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 元素按顺序压入栈
 *   - 用于可变参数函数调用时展开向量参数
 */
WOORT_NODISCARD bool woort_IRBlock_UNPACKVEC(
    woort_IRBlock* block,
    const woort_IRValue* vec);

/*
 * woort_IRBlock_UNPACKVECX
 * 
 * 解包扩展向量：将扩展向量（存储 DynBox 的向量）的所有元素压入栈中。
 * 
 * 参数：
 *   block   - 当前基本块
 *   vec     - 要解包的扩展向量（元素类型为 DynBox）
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 元素按顺序压入栈，每个元素都是 DynBox 类型
 *   - 用于可变参数函数调用时展开动态类型向量参数
 *   - 与 UNPACKVEC 的区别：UNPACKVEC 处理普通类型向量，UNPACKVECX 处理 DynBox 向量
 */
WOORT_NODISCARD bool woort_IRBlock_UNPACKVECX(
    woort_IRBlock* block,
    const woort_IRValue* vec);

/*
 * woort_IRBlock_UNPACKSTRUCT
 * 
 * 解包结构体：将结构体的所有字段压入栈中。
 * 
 * 参数：
 *   block    - 当前基本块
 *   struct_val - 要解包的结构体
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_UNPACKSTRUCT(
    woort_IRBlock* block,
    const woort_IRValue* struct_val);

/*******************************************************************************
 * IRBlock 动态类型指令
 * 
 * 动态类型（DynBox）是一种可以持有任意类型值的容器。
 * 用于实现动态类型语言的特性。
 * 
 * 类型标记：
 *   - 'I': 整数
 *   - 'R': 实数
 *   - 'B': 布尔
 *   - 'X': 动态对象/GC 对象
 ******************************************************************************/

/*
 * woort_IRValue_TypeTag
 * 
 * 类型标记枚举，用于动态类型操作。
 */
typedef enum woort_IRValue_TypeTag
{
    WOORT_IRVALUE_TYPE_TAG_I = 0,  /* 整数 Integer */
    WOORT_IRVALUE_TYPE_TAG_R = 1,  /* 实数 Real */
    WOORT_IRVALUE_TYPE_TAG_B = 2,  /* 布尔 Boolean */
    WOORT_IRVALUE_TYPE_TAG_X = 3   /* 动态对象/扩展类型 Dynamic/eXtended */
} woort_IRValue_TypeTag;

/*
 * woort_IRBlock_BOXDYN
 * 
 * 将值装箱为动态类型（DynBox）。
 * 
 * 参数：
 *   block     - 当前基本块
 *   type_tag  - 值的类型标记
 *   value     - 要装箱的值
 * 
 * 返回：
 *   表示装箱结果的 IRValue 指针（DynBox 类型）
 * 
 * 用法：
 *   const woort_IRValue* boxed = woort_IRBlock_BOXDYN(
 *       block, WOORT_IRVALUE_TYPE_TAG_I, int_value);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_BOXDYN(
    woort_IRBlock* block,
    woort_IRValue_TypeTag type_tag,
    const woort_IRValue* value);

/*
 * woort_IRBlock_UNBOXDYN
 * 
 * 将动态类型（DynBox）拆箱为原始值。
 * 
 * 参数：
 *   block     - 当前基本块
 *   type_tag  - 期望的类型标记
 *   dynbox    - 动态类型值
 * 
 * 返回：
 *   表示拆箱结果的 IRValue 指针
 * 
 * 注意：
 *   - 类型不匹配时行为未定义（运行时可能 panic）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_UNBOXDYN(
    woort_IRBlock* block,
    woort_IRValue_TypeTag type_tag,
    const woort_IRValue* dynbox);

/*
 * woort_IRBlock_CHECKDYN
 * 
 * 检查动态类型（DynBox）是否为指定类型。
 * 
 * 参数：
 *   block     - 当前基本块
 *   type_tag  - 要检查的类型标记
 *   dynbox    - 动态类型值
 * 
 * 返回：
 *   表示检查结果的 IRValue 指针（布尔值）
 *   - true: 类型匹配
 *   - false: 类型不匹配
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_CHECKDYN(
    woort_IRBlock* block,
    woort_IRValue_TypeTag type_tag,
    const woort_IRValue* dynbox);

/*******************************************************************************
 * IRBlock 向量索引访问指令
 * 
 * 向量（Vec）是动态数组，支持通过整数索引读写元素。
 ******************************************************************************/

/*
 * woort_IRBlock_LDIDXVEC
 * 
 * 加载向量元素：result = vec[index]
 * 
 * 参数：
 *   block   - 当前基本块
 *   vec     - 向量值
 *   index   - 索引（整数）
 * 
 * 返回：
 *   表示加载元素的 IRValue 指针
 * 
 * 注意：
 *   - 索引越界时行为未定义
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXVEC(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index);

/*
 * woort_IRBlock_STIDXVEC
 * 
 * 存储向量元素：vec[index] = value
 * 
 * 参数：
 *   block   - 当前基本块
 *   vec     - 向量值（将被修改）
 *   index   - 索引（整数）
 *   value   - 要存储的值
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 索引越界时行为未定义
 */
WOORT_NODISCARD bool woort_IRBlock_STIDXVEC(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index,
    const woort_IRValue* value);

/*
 * woort_IRBlock_LDIDXVECX
 * 
 * 加载扩展向量元素（用于存储 DynBox 的向量）。
 * 
 * 参数：
 *   block   - 当前基本块
 *   vec     - 向量值
 *   index   - 索引（整数）
 * 
 * 返回：
 *   表示加载元素的 IRValue 指针（DynBox 类型）
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXVECX(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index);

/*
 * woort_IRBlock_STIDXVECX
 * 
 * 存储扩展向量元素（用于存储 DynBox 的向量）。
 * 
 * 参数：
 *   block   - 当前基本块
 *   vec     - 向量值
 *   index   - 索引（整数）
 *   value   - 要存储的值（DynBox 类型）
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_STIDXVECX(
    woort_IRBlock* block,
    const woort_IRValue* vec,
    const woort_IRValue* index,
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 映射索引访问指令
 * 
 * 映射（Map/Dict）支持多种类型的键。
 ******************************************************************************/

/*
 * woort_IRBlock_LDIDXMAP
 * 
 * 加载映射元素：result = map[key]
 * 支持整数、实数、布尔、动态类型作为键。
 * 
 * 参数：
 *   block       - 当前基本块
 *   map         - 映射值
 *   key_type    - 键的类型标记
 *   key         - 键值
 * 
 * 返回：
 *   表示加载元素的 IRValue 指针
 * 
 * 注意：
 *   - 键不存在时返回 nil/默认值
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDXMAP(
    woort_IRBlock* block,
    const woort_IRValue* map,
    woort_IRValue_TypeTag key_type,
    const woort_IRValue* key);

/*
 * woort_IRBlock_STIDXMAP
 * 
 * 存储映射元素：map[key] = value
 * 
 * 参数：
 *   block       - 当前基本块
 *   map         - 映射值
 *   key_type    - 键的类型标记
 *   key         - 键值
 *   value       - 要存储的值
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_STIDXMAP(
    woort_IRBlock* block,
    const woort_IRValue* map,
    woort_IRValue_TypeTag key_type,
    const woort_IRValue* key,
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 结构体字段访问指令
 * 
 * 结构体字段通过编译时常量索引访问。
 ******************************************************************************/

/*
 * woort_IRBlock_LDIDSTRUCT
 * 
 * 加载结构体字段：result = struct.field_index
 * 
 * 参数：
 *   block        - 当前基本块
 *   struct_val   - 结构体值
 *   field_index  - 字段索引（编译时常量）
 * 
 * 返回：
 *   表示加载字段的 IRValue 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDSTRUCT(
    woort_IRBlock* block,
    const woort_IRValue* struct_val,
    size_t field_index);

/*
 * woort_IRBlock_STIDSTRUCT
 * 
 * 存储结构体字段：struct.field_index = value
 * 
 * 参数：
 *   block        - 当前基本块
 *   struct_val   - 结构体值
 *   field_index  - 字段索引（编译时常量）
 *   value        - 要存储的值
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 */
WOORT_NODISCARD bool woort_IRBlock_STIDSTRUCT(
    woort_IRBlock* block,
    const woort_IRValue* struct_val,
    size_t field_index,
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 字符串索引访问指令
 * 
 * 字符串支持通过整数索引读取单个字符（返回子字符串）。
 ******************************************************************************/

/*
 * woort_IRBlock_LDIDSTRING
 * 
 * 加载字符串字符：result = string[index]
 * 
 * 参数：
 *   block   - 当前基本块
 *   str     - 字符串值
 *   index   - 索引（整数）
 * 
 * 返回：
 *   表示字符的 IRValue 指针（单字符字符串）
 * 
 * 注意：
 *   - 索引越界时行为未定义
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_LDIDSTRING(
    woort_IRBlock* block,
    const woort_IRValue* str,
    const woort_IRValue* index);

/*******************************************************************************
 * IRStorage 接口
 * 
 * IRStorage 提供了一种非 SSA 形式的变量存储方式。
 * 当 SSA 形式编写不方便时（如循环中的累加器、条件分支中的变量），
 * 可以使用 Storage 进行 load/store 操作。
 * 
 * IR 编译器会自动将 Storage 的使用转化为 PHI 指令。
 * 
 * 使用场景：
 * 1. 循环变量：for (i = 0; i < n; i++) 中的 i
 * 2. 累加器：sum = 0; for (...) { sum += x; } 中的 sum
 * 3. 条件赋值：if (cond) { x = a; } else { x = b; } 中的 x
 * 
 * 用法示例：
 * 
 *   // 创建存储位置
 *   woort_IRStorage* sum = woort_IRFunction_create_storage(func);
 *   
 *   // 初始化
 *   const woort_IRValue* zero = woort_IRFunction_load_const(func, zero_idx);
 *   woort_IRBlock_store(entry_block, sum, zero);
 *   
 *   // 循环中加载、修改、存储
 *   const woort_IRValue* loaded = woort_IRBlock_load(loop_block, sum);
 *   const woort_IRValue* new_sum = woort_IRBlock_ADDI(loop_block, loaded, value);
 *   woort_IRBlock_store(loop_block, sum, new_sum);
 *   
 *   // 最终使用
 *   const woort_IRValue* final_sum = woort_IRBlock_load(exit_block, sum);
 *   woort_IRBlock_ret(exit_block, final_sum);
 ******************************************************************************/

/*
 * woort_IRBlock_load
 * 
 * 从 Storage 加载当前值。
 * 
 * 参数：
 *   block    - 当前基本块
 *   storage  - 存储位置
 * 
 * 返回：
 *   表示当前存储值的 IRValue 指针
 * 
 * 注意：
 *   - 在控制流汇合点，IR 会自动生成 PHI 指令
 *   - 如果在所有路径上都未初始化，行为未定义
 */
WOORT_NODISCARD const woort_IRValue* woort_IRBlock_load(
    woort_IRBlock* block,
    const woort_IRStorage* storage);

/*
 * woort_IRBlock_store
 * 
 * 将值存储到 Storage。
 * 
 * 参数：
 *   block    - 当前基本块
 *   storage  - 存储位置
 *   value    - 要存储的值
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 注意：
 *   - 同一个基本块中多次 store，后者的值会覆盖前者
 *   - 不同基本块中对同一 storage 的 store 会在控制流汇合时形成 PHI
 */
WOORT_NODISCARD bool woort_IRBlock_store(
    woort_IRBlock* block,
    woort_IRStorage* storage,
    const woort_IRValue* value);

/*
 * woort_IRBlock_load_store
 * 
 * 原子加载并存储：加载当前值，同时存储新值。
 * 这是一个便捷方法，等同于 load 后紧接着 store。
 * 
 * 参数：
 *   block        - 当前基本块
 *   storage      - 存储位置
 *   new_value    - 要存储的新值
 *   out_old_value - 输出参数，接收存储前的旧值（可选）
 * 
 * 返回：
 *   true  - 成功
 *   false - 失败
 * 
 * 用法：
 *   const woort_IRValue* old_val;
 *   woort_IRBlock_load_store(block, counter, new_val, &old_val);
 */
WOORT_NODISCARD bool woort_IRBlock_load_store(
    woort_IRBlock* block,
    woort_IRStorage* storage,
    const woort_IRValue* new_value,
    /* OPTIONAL */ const woort_IRValue** out_old_value);

/*
 * woort_IRStorage_is_initialized
 * 
 * 检查 Storage 在当前基本块是否已初始化。
 * 
 * 参数：
 *   storage  - 存储位置
 *   block    - 当前基本块
 * 
 * 返回：
 *   true  - 已初始化
 *   false - 未初始化
 * 
 * 注意：
 *   - 这是一个调试/验证接口，不影响生成的代码
 */
WOORT_NODISCARD bool woort_IRStorage_is_initialized(
    const woort_IRStorage* storage,
    const woort_IRBlock* block);

/*
 * woort_IRStorage_get_function
 * 
 * 获取 Storage 所属的函数。
 * 
 * 参数：
 *   storage  - 存储位置
 * 
 * 返回：
 *   所属的 IRFunction 指针
 */
WOORT_NODISCARD woort_IRFunction* woort_IRStorage_get_function(
    const woort_IRStorage* storage);

/*******************************************************************************
 * IRValue 工具接口
 * 
 * 用于查询 IRValue 的属性。
 ******************************************************************************/

/*
 * woort_IRValue_is_const
 * 
 * 检查 IRValue 是否为常量。
 * 
 * 参数：
 *   value  - IR 值
 * 
 * 返回：
 *   true  - 是常量
 *   false - 不是常量
 */
WOORT_NODISCARD bool woort_IRValue_is_const(
    const woort_IRValue* value);

/*
 * woort_IRValue_is_argument
 * 
 * 检查 IRValue 是否为函数参数。
 * 
 * 参数：
 *   value  - IR 值
 * 
 * 返回：
 *   true  - 是函数参数
 *   false - 不是函数参数
 */
WOORT_NODISCARD bool woort_IRValue_is_argument(
    const woort_IRValue* value);

/*
 * woort_IRValue_get_argument_index
 * 
 * 如果 IRValue 是函数参数，获取其参数索引。
 * 
 * 参数：
 *   value  - IR 值
 * 
 * 返回：
 *   参数索引（0 = 第一个参数）
 * 
 * 注意：
 *   - 如果 IRValue 不是函数参数，行为未定义
 */
WOORT_NODISCARD size_t woort_IRValue_get_argument_index(
    const woort_IRValue* value);

/*
 * woort_IRValue_get_global_index
 * 
 * 如果 IRValue 是常量，获取其全局存储索引。
 * 
 * 参数：
 *   value  - IR 值
 * 
 * 返回：
 *   全局存储索引
 * 
 * 注意：
 *   - 如果 IRValue 不是常量，行为未定义
 */
WOORT_NODISCARD woort_IRGlobalIndex woort_IRValue_get_global_index(
    const woort_IRValue* value);

/*******************************************************************************
 * IRBlock 工具接口
 * 
 * 用于查询 IRBlock 的属性。
 ******************************************************************************/

/*
 * woort_IRBlock_get_function
 * 
 * 获取基本块所属的函数。
 * 
 * 参数：
 *   block  - 基本块
 * 
 * 返回：
 *   所属的 IRFunction 指针
 */
WOORT_NODISCARD woort_IRFunction* woort_IRBlock_get_function(
    const woort_IRBlock* block);

/*
 * woort_IRBlock_is_terminated
 * 
 * 检查基本块是否已有终结指令（ret、br、condbr 等）。
 * 
 * 参数：
 *   block  - 基本块
 * 
 * 返回：
 *   true  - 已有终结指令
 *   false - 尚未终结
 */
WOORT_NODISCARD bool woort_IRBlock_is_terminated(
    const woort_IRBlock* block);

/*
 * woort_IRBlock_get_predecessor_count
 * 
 * 获取基本块的前驱块数量。
 * 
 * 参数：
 *   block  - 基本块
 * 
 * 返回：
 *   前驱块数量
 */
WOORT_NODISCARD size_t woort_IRBlock_get_predecessor_count(
    const woort_IRBlock* block);

/*
 * woort_IRBlock_get_successor_count
 * 
 * 获取基本块的后继块数量。
 * 
 * 参数：
 *   block  - 基本块
 * 
 * 返回：
 *   后继块数量
 */
WOORT_NODISCARD size_t woort_IRBlock_get_successor_count(
    const woort_IRBlock* block);
