#pragma once

/*
 * woort_ir_types.h
 *
 * WooRT IR 类型定义
 *
 * 本文件定义了 WooRT IR 系统中使用的核心类型：
 * - woort_IRGlobalIndex: 全局存储索引，用于引用常量池和静态存储区
 * - woort_IRValue: SSA 值，代表无限虚拟寄存器中的一个值
 * - woort_IRStorage: 可变存储位置，用于需要多次赋值的变量
 *
 * IR 设计理念：
 * - SSA 形式：每个 woort_IRValue 只被赋值一次，由 IR 自动处理 PHI 节点
 * - 无限寄存器：不限制虚拟寄存器数量，由 IR 负责寄存器分配
 * - 类型无关：IR 不携带类型信息，类型检查由 Woolang 前端完成
 */

#include "woort_diagnosis.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * woort_IRGlobalIndex
 *
 * 全局存储索引，用于引用 CodeEnv 中的常量或静态存储单元。
 *
 * 全局存储区在调用 woort_IRCompiler_finish() 后对应 woort_CodeEnv::m_data_begin。
 * 索引从 0 开始，按 woort_IRCompiler_allocate_global() 调用顺序递增分配。
 *
 * 使用场景：
 * - 加载常量值（整数、实数、字符串等）
 * - 引用脚本函数（用于函数调用）
 * - 引用原生函数（Native Function）
 * - 引用静态存储区
 *
 * 示例：
 *   woort_IRGlobalIndex const_idx = woort_IRCompiler_allocate_global(irc);
 *   // 在 finish 后设置：code_env->m_data_begin[const_idx].m_integer = 42;
 *   const woort_IRValue* val = woort_IRFunction_load_const(func, const_idx);
 */
typedef size_t woort_IRGlobalIndex;

/*
 * woort_IRValue
 *
 * SSA 值，代表一个不可变的计算结果。
 *
 * 特性：
 * - 每个 IRValue 只被赋值一次（SSA 形式）
 * - 代表无限虚拟寄存器中的一个值
 * - 由 IR 指令生成函数返回（如 woort_IRBlock_addi）
 * - 可作为后续指令的操作数使用
 * - 生命周期由所属的 woort_IRFunction 管理
 *
 * IRValue 可以来自：
 * - 常量加载：woort_IRFunction_load_const()
 * - 参数加载：woort_IRFunction_load_argument()
 * - 指令结果：如 woort_IRBlock_addi() 等计算指令
 * - Storage 加载：woort_IRStorage_load()
 *
 * 注意：
 * - IRValue 是不透明类型，用户不应直接访问其内部结构
 * - IRValue 指针仅在 woort_IRCompiler_deinit() 之前有效
 */
typedef struct woort_IRValue woort_IRValue;

/*
 * woort_IRStorage
 *
 * 可变存储位置，用于需要多次赋值的变量。
 *
 * 与 IRValue 的区别：
 * - IRValue 是 SSA 形式，只能被赋值一次
 * - IRStorage 允许多次 store 和 load
 *
 * IR 编译器会自动将 Storage 的 load/store 转换为适当的 PHI 节点，
 * 以保持 SSA 形式的正确性。
 *
 * 使用场景：
 * - 循环变量（如 for 循环的迭代器）
 * - 需要条件更新的变量
 * - 闭包捕获的变量
 *
 * 示例：
 *   woort_IRStorage* counter = woort_IRFunction_create_storage(func);
 *   woort_IRBlock_store(block, counter, initial_value);
 *   const woort_IRValue* current = woort_IRStorage_load(block, counter);
 *   // ... 修改 counter
 *   woort_IRBlock_store(block, counter, new_value);
 *
 * 注意：
 * - IRStorage 是不透明类型，用户不应直接访问其内部结构
 * - IRStorage 指针仅在 woort_IRCompiler_deinit() 之前有效
 */
typedef struct woort_IRStorage woort_IRStorage;

/*
 * woort_IRFunction
 *
 * IR 函数，代表一个完整的函数定义。
 *
 * 一个 IRFunction 包含：
 * - 入口基本块（Entry Block）：函数执行的第一块
 * - 零或多个其他基本块：用于控制流分支
 * - 参数信息：可通过 woort_IRFunction_load_argument() 访问
 *
 * 函数结构：
 * - 函数以 Entry Block 开始
 * - 每个 Block 以终止指令结束（ret, br, condbr 等）
 * - Block 之间通过分支指令连接形成控制流图
 *
 * 注意：
 * - IRFunction 是不透明类型，用户不应直接访问其内部结构
 * - IRFunction 指针仅在 woort_IRCompiler_deinit() 之前有效
 */
typedef struct woort_IRFunction woort_IRFunction;

/*
 * woort_IRBlock
 *
 * IR 基本块，是控制流的基本单位。
 *
 * 特性：
 * - 包含一系列按顺序执行的指令
 * - 以一个终止指令结束（ret, br, condbr 等）
 * - 一旦终止指令被添加，Block 就不能再添加其他指令
 * - Block 可有多个前驱和多个后继
 *
 * 控制流：
 * - Entry Block：函数的入口点，无前驱
 * - 普通块：通过分支指令与其他块连接
 * - 终止块：以 ret 指令结束，无后继
 *
 * 指令添加：
 * - 所有指令通过 woort_IRBlock_* 函数添加
 * - 指令按添加顺序执行
 * - 终止指令必须是最后一条指令
 *
 * 注意：
 * - IRBlock 是不透明类型，用户不应直接访问其内部结构
 * - IRBlock 指针仅在 woort_IRCompiler_deinit() 之前有效
 */
typedef struct woort_IRBlock woort_IRBlock;

/*
 * woort_IRCompiler
 *
 * IR 编译器上下文，管理整个 IR 编译过程。
 *
 * 生命周期：
 * 1. woort_IRCompiler_init() - 初始化编译器
 * 2. woort_IRCompiler_allocate_global() - 分配全局存储索引
 * 3. woort_IRCompiler_add_function() - 添加函数
 * 4. ... 填充函数内容 ...
 * 5. woort_IRCompiler_finish() - 完成编译，生成 woort_CodeEnv
 * 6. woort_IRCompiler_deinit() - 销毁编译器
 *
 * 职责：
 * - 管理全局存储区索引分配
 * - 管理所有 IRFunction 的生命周期
 * - 最终将 IR 编译为可执行的 woort_CodeEnv
 *
 * 注意：
 * - IRCompiler 是不透明类型，用户不应直接访问其内部结构
 */
typedef struct woort_IRCompiler woort_IRCompiler;

/*
 * woort_IRType
 *
 * IR 值的类型标识，用于类型转换和动态类型操作。
 *
 * 类型系统：
 * - WOORT_IR_TYPE_INTEGER: 整数类型
 * - WOORT_IR_TYPE_REAL: 实数（浮点）类型
 * - WOORT_IR_TYPE_BOOLEAN: 布尔类型
 * - WOORT_IR_TYPE_STRING: 字符串类型
 * - WOORT_IR_TYPE_DYNAMIC: 动态类型（GC 对象）
 *
 * 注意：
 * - IR 本身不进行类型检查，类型检查由 Woolang 前端完成
 * - 此类型标识主要用于生成正确的类型转换指令
 */
typedef enum woort_IRType
{
    WOORT_IR_TYPE_INTEGER  = 0,
    WOORT_IR_TYPE_REAL     = 1,
    WOORT_IR_TYPE_BOOLEAN  = 2,
    WOORT_IR_TYPE_STRING   = 3,
    WOORT_IR_TYPE_DYNAMIC  = 4,

} woort_IRType;

/*
 * woort_IRCallKind
 *
 * 函数调用类型，区分不同的函数调用方式。
 *
 * 调用类型：
 * - WOORT_IR_CALL_KIND_SCRIPT: 调用脚本函数（字节码函数）
 * - WOORT_IR_CALL_KIND_NATIVE_WITH_OPAQUE: 调用原生函数（使用 Opaque 调用约定）
 * - WOORT_IR_CALL_KIND_NATIVE_FP: 调用原生函数（使用函数指针调用约定）
 * - WOORT_IR_CALL_KIND_JIT: 调用 JIT 编译的函数
 *
 * 调用约定差异：
 * - SCRIPT: 标准 Woolang 调用约定
 * - NATIVE_WITH_OPAQUE: 传递 VM 上下文和参数数组
 * - NATIVE_FP: 直接使用 C 函数指针调用
 * - JIT: 与 SCRIPT 相同，但目标由 JIT 生成
 */
typedef enum woort_IRCallKind
{
    WOORT_IR_CALL_KIND_SCRIPT            = 0,
    WOORT_IR_CALL_KIND_NATIVE_WITH_OPAQUE = 1,
    WOORT_IR_CALL_KIND_NATIVE_FP          = 2,
    WOORT_IR_CALL_KIND_JIT                = 3,

} woort_IRCallKind;
