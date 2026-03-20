#pragma once

/*
 * woort_ir_function.h
 *
 * WooRT IR 函数接口
 *
 * 本文件定义了 woort_IRFunction 的接口，用于管理函数的基本块、
 * 参数、常量加载和可变存储。
 *
 * 函数结构：
 * - 每个函数有一个入口基本块（Entry Block）
 * - 可以添加多个基本块用于控制流
 * - 函数参数通过 load_argument 访问
 * - 常量通过 load_const 加载
 * - 可变变量通过 IRStorage 支持
 *
 * 示例：
 *   woort_IRFunction* func;
 *   woort_IRCompiler_add_function(irc, &func);
 *
 *   // 获取入口块
 *   woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
 *
 *   // 加载参数
 *   const woort_IRValue* arg0 = woort_IRFunction_load_argument(func, 0);
 *
 *   // 加载常量
 *   const woort_IRValue* c = woort_IRFunction_load_const(func, const_idx);
 *
 *   // 添加基本块
 *   woort_IRBlock* loop_body;
 *   woort_IRFunction_add_block(func, &loop_body);
 */

#include "woort_ir_types.h"

#include <stdbool.h>

/*
 * woort_IRFunction_get_entry_block
 *
 * 获取函数的入口基本块。
 *
 * 参数：
 *   function - 函数实例
 *
 * 返回值：
 *   入口基本块指针
 *
 * 说明：
 * - 入口块是函数执行的第一块
 * - 每个函数在创建时自动创建入口块
 * - 入口块是唯一没有前驱的块
 * - 函数参数从入口块开始可用
 *
 * 示例：
 *   woort_IRBlock* entry = woort_IRFunction_get_entry_block(func);
 *   // 在 entry 中添加指令...
 */
WOORT_NODISCARD woort_IRBlock* woort_IRFunction_get_entry_block(
    woort_IRFunction* function);

/*
 * woort_IRFunction_add_block
 *
 * 向函数添加一个新的基本块。
 *
 * 参数：
 *   function   - 函数实例
 *   out_block  - 输出参数，接收新创建的基本块指针
 *
 * 返回值：
 *   true  - 块创建成功
 *   false - 内存分配失败
 *
 * 说明：
 * - 新创建的块不会自动连接到控制流
 * - 需要通过 br 或 condbr 指令将块连接到控制流图
 * - 块必须以终止指令（ret, br, condbr）结束
 * - 未被任何分支指向的块（死代码）可能在编译时被移除
 *
 * 示例：
 *   woort_IRBlock* then_block;
 *   woort_IRFunction_add_block(func, &then_block);
 *
 *   woort_IRBlock* else_block;
 *   woort_IRFunction_add_block(func, &else_block);
 *
 *   // 在 entry 块中添加条件分支
 *   woort_IRBlock_condbr(entry, cond, then_block, else_block);
 */
WOORT_NODISCARD bool woort_IRFunction_add_block(
    woort_IRFunction* function,
    woort_IRBlock** out_block);

/*
 * woort_IRFunction_load_argument
 *
 * 加载函数参数作为 SSA 值。
 *
 * 参数：
 *   function - 函数实例
 *   index    - 参数索引（从 0 开始）
 *
 * 返回值：
 *   代表参数值的 IRValue 指针
 *
 * 说明：
 * - 参数索引从 0 开始，0 表示第一个参数
 * - 对于函数 foo(a, b, c)，a 的索引是 0，b 是 1，c 是 2
 * - 返回的 IRValue 可在任何基本块中使用
 * - 如果索引超出参数数量范围，行为未定义
 *
 * 示例：
 *   // 函数定义：func add(x, y)
 *   const woort_IRValue* x = woort_IRFunction_load_argument(func, 0);
 *   const woort_IRValue* y = woort_IRFunction_load_argument(func, 1);
 *   const woort_IRValue* sum = woort_IRBlock_addi(entry, x, y);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRFunction_load_argument(
    woort_IRFunction* function,
    size_t index);

/*
 * woort_IRFunction_load_const
 *
 * 加载全局常量作为 SSA 值。
 *
 * 参数：
 *   function      - 函数实例
 *   global_index  - 全局存储索引（由 woort_IRCompiler_allocate_global 返回）
 *
 * 返回值：
 *   代表常量值的 IRValue 指针
 *
 * 说明：
 * - 从全局存储区加载一个值
 * - global_index 必须是有效的全局索引
 * - 全局存储区的值在 woort_IRCompiler_finish() 后填充
 * - 返回的 IRValue 可在任何基本块中使用
 *
 * 示例：
 *   // 在编译器中分配常量
 *   woort_IRGlobalIndex idx_42 = woort_IRCompiler_allocate_global(irc);
 *
 *   // 在函数中加载
 *   const woort_IRValue* val = woort_IRFunction_load_const(func, idx_42);
 *
 *   // 在 finish 后填充
 *   code_env->m_data_begin[idx_42].m_integer = 42;
 */
WOORT_NODISCARD const woort_IRValue* woort_IRFunction_load_const(
    woort_IRFunction* function,
    woort_IRGlobalIndex global_index);

/*
 * woort_IRFunction_create_storage
 *
 * 创建一个可变存储位置。
 *
 * 参数：
 *   function - 函数实例
 *
 * 返回值：
 *   新创建的 IRStorage 指针，失败返回 NULL
 *
 * 说明：
 * - IRStorage 用于需要多次赋值的变量
 * - 与 IRValue（SSA 形式，单次赋值）不同，Storage 允许多次 store/load
 * - IR 编译器会自动将 Storage 转换为 PHI 节点
 * - IRStorage 的生命周期由函数管理
 *
 * 使用场景：
 * - 循环变量（如 for 循环的迭代器）
 * - 需要条件更新的变量
 * - 累加器
 *
 * 示例：
 *   woort_IRStorage* acc = woort_IRFunction_create_storage(func);
 *
 *   // 初始化
 *   woort_IRBlock_store(entry_block, acc, initial_value);
 *
 *   // 在循环中读取和更新
 *   const woort_IRValue* current = woort_IRStorage_load(loop_block, acc);
 *   const woort_IRValue* new_val = woort_IRBlock_addi(loop_block, current, one);
 *   woort_IRBlock_store(loop_block, acc, new_val);
 */
WOORT_NODISCARD /* OPTIONAL */ woort_IRStorage* woort_IRFunction_create_storage(
    woort_IRFunction* function);

/*
 * woort_IRFunction_set_parameter_count
 *
 * 设置函数的参数数量。
 *
 * 参数：
 *   function - 函数实例
 *   count    - 参数数量
 *
 * 返回值：
 *   true  - 设置成功
 *   false - 设置失败
 *
 * 说明：
 * - 设置函数期望的参数数量
 * - 用于验证 load_argument 的索引范围
 * - 对于可变参数函数，设置最小参数数量
 * - 如果不调用此函数，默认参数数量为 0
 */
WOORT_NODISCARD bool woort_IRFunction_set_parameter_count(
    woort_IRFunction* function,
    size_t count);

/*
 * woort_IRFunction_get_parameter_count
 *
 * 获取函数的参数数量。
 *
 * 参数：
 *   function - 函数实例
 *
 * 返回值：
 *   参数数量
 */
WOORT_NODISCARD size_t woort_IRFunction_get_parameter_count(
    const woort_IRFunction* function);

/*
 * woort_IRFunction_set_variadic
 *
 * 设置函数是否为可变参数函数。
 *
 * 参数：
 *   function   - 函数实例
 *   variadic   - true 表示可变参数，false 表示固定参数
 *
 * 说明：
 * - 可变参数函数可以接受比声明的参数数量更多的参数
 * - 额外的参数通过 UNPACKVEC 或 UNPACKVECX 指令访问
 * - 可变参数的实参数量在运行时确定
 */
void woort_IRFunction_set_variadic(
    woort_IRFunction* function,
    bool variadic);

/*
 * woort_IRFunction_is_variadic
 *
 * 检查函数是否为可变参数函数。
 *
 * 参数：
 *   function - 函数实例
 *
 * 返回值：
 *   true  - 是可变参数函数
 *   false - 是固定参数函数
 */
WOORT_NODISCARD bool woort_IRFunction_is_variadic(
    const woort_IRFunction* function);

/*
 * woort_IRFunction_get_block_count
 *
 * 获取函数中的基本块数量。
 *
 * 参数：
 *   function - 函数实例
 *
 * 返回值：
 *   基本块数量（至少为 1，包含入口块）
 */
WOORT_NODISCARD size_t woort_IRFunction_get_block_count(
    const woort_IRFunction* function);

/*
 * woort_IRStorage_load
 *
 * 从可变存储中加载值。
 *
 * 参数：
 *   block    - 当前基本块（用于生成正确的 PHI 节点）
 *   storage  - 可变存储实例
 *
 * 返回值：
 *   代表当前存储值的 IRValue 指针
 *
 * 说明：
 * - 返回当前存储位置中的值
 * - IR 编译器会根据控制流自动生成正确的 PHI 节点
 * - 必须在 store 之后的块中调用才能获得有意义的值
 *
 * 示例：
 *   const woort_IRValue* current = woort_IRStorage_load(block, storage);
 */
WOORT_NODISCARD const woort_IRValue* woort_IRStorage_load(
    woort_IRBlock* block,
    const woort_IRStorage* storage);

/*
 * woort_IRBlock_store (函数声明，实现在 woort_ir_block.h)
 *
 * 将值存储到可变存储位置。
 *
 * 参数：
 *   block    - 当前基本块
 *   storage  - 可变存储实例
 *   value    - 要存储的值
 *
 * 返回值：
 *   true  - 存储成功
 *   false - 存储失败
 *
 * 说明：
 * - 将 value 存储到 storage 中
 * - 后续的 woort_IRStorage_load 将返回此值（直到下次 store）
 * - IR 编译器会根据控制流自动处理不同路径的值合并
 *
 * 示例：
 *   woort_IRBlock_store(block, counter, new_value);
 */
WOORT_NODISCARD bool woort_IRBlock_store(
    woort_IRBlock* block,
    woort_IRStorage* storage,
    const woort_IRValue* value);
