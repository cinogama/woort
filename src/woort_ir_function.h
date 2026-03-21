#pragma once

/*
 * woort_ir_function.h
 */

#include "woort_ir_types.h"
#include "woort_diagnosis.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * IRFunction - IR 函数
 *
 * 包含多个基本块和函数参数。
 */

/*
 * 获取函数的入口基本块
 *
 * func: 函数指针
 * 返回: 入口基本块指针
 */
WOORT_NODISCARD woort_IRBlock* woort_IRFunction_get_entry_block(woort_IRFunction* func);

/*
 * 获取函数参数
 *
 * func: 函数指针
 * index: 参数索引
 * 返回: 参数值指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRFunction_get_param(woort_IRFunction* func, uint32_t index);

/*
 * 添加新的基本块
 *
 * func: 函数指针
 * out_block: 输出基本块指针
 * 返回: 成功返回 true
 */
WOORT_NODISCARD bool woort_IRFunction_add_block(
    woort_IRFunction* func,
    woort_IRBlock** out_block);

/*
 * 创建 PHI 节点
 *
 * PHI 节点可以在 block 代码生成完成后继续填充 incoming
 *
 * func: 函数指针
 * block: PHI 所在的基本块
 * 返回: PHI 节点指针
 */
WOORT_NODISCARD woort_IRPHI* woort_IRFunction_create_phi(woort_IRFunction* func, woort_IRBlock* block);

/*
 * 添加 PHI 的输入来源
 *
 * phi: PHI 节点指针
 * from_block: 来源基本块
 * value: 来源值
 */
void woort_IRPHI_add_incoming(woort_IRPHI* phi, woort_IRBlock* from_block, const woort_IRValue* value);

/*
 * 将 PHI 转换为 Value 以便使用
 *
 * phi: PHI 节点指针
 * 返回: Value 指针
 */
WOORT_NODISCARD const woort_IRValue* woort_IRPHI_as_value(woort_IRPHI* phi);
