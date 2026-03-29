#pragma once

/*
 * woort_ir_function.h
 *
 * IR 函数：持有虚拟寄存器、Label、指令列表。
 * finish 阶段：Label->Block 切分、活跃性分析、栈槽分配、常量加载放置。
 */

#include "woort_ir_block.h"
#include "woort_ir_value.h"
#include "woort_ir_op.h"
#include "woort_linklist.h"
#include "woort_vector.h"
#include "woort_diagnosis.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct woort_IRFunction
{
    uint32_t m_param_count;

    /* 虚拟寄存器池（LinkList 持有 woort_IRValue） */
    woort_LinkList /* woort_IRValue */ m_ir_values;
    uint32_t m_next_vreg_id;

    /* Label 池（LinkList 持有 woort_IRLabel） */
    woort_LinkList /* woort_IRLabel */ m_ir_labels;
    uint32_t m_next_label_id;

    /* 线性指令列表 */
    woort_Vector /* woort_IROp */ m_instructions;

    /* finish 阶段生成的基本块 */
    woort_Vector /* woort_IRBlock */ m_blocks;

    /* finish 阶段：函数字节码的起始偏移 (在 compiler 的总代码中) */
    size_t m_code_offset;
    size_t m_code_length;
};

void woort_IRFunction_init(woort_IRFunction* f, uint32_t param_count);
void woort_IRFunction_deinit(woort_IRFunction* f);

/* 创建新的虚拟寄存器 */
WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_new_vreg(woort_IRFunction* f);

/* 获取函数参数的虚拟寄存器（预分配到 SB+3+idx） */
WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(woort_IRFunction* f, uint32_t param_idx);

/* 创建新的 Label */
WOORT_NODISCARD /* OPTIONAL */ woort_IRLabel* woort_IRFunction_new_label(woort_IRFunction* f);

/* 获取一个代表常量 G[idx] 的值。
 * 同一 const_index 多次调用返回相同的 IRValue*（天然去重）。
 * 返回的 IRValue* 的 m_source 为 WOORT_IRVALUE_SOURCE_CONST。
 * 返回 NULL 表示 OOM。 */
WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRFunction_load_const(
    woort_IRFunction* f, woort_IRConstantIndex idx);

/* finish 阶段内部函数 */
WOORT_NODISCARD bool _woort_IRFunction_analyze_and_allocate(
    woort_IRFunction* f, size_t* out_stack_space);
