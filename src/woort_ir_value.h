#pragma once

/*
 * woort_ir_value.h
 *
 * IR 虚拟寄存器定义。
 * 三种来源：普通 vreg（可变）、函数参数（预分配栈偏移）、常量（绑定 G[idx]）。
 */

#include "woort.h"

#include <stdint.h>
#include <stdbool.h>

/*
 * 虚拟寄存器来源类型
 */
typedef enum woort_IRValue_Source
{
    /* 普通虚拟寄存器（用户通过 new_vreg 创建） */
    WOORT_IRVALUE_SOURCE_VREG,

    /* 函数参数（预分配到 SB+3+idx） */
    WOORT_IRVALUE_SOURCE_ARGUMENT,

    /* 常量值（绑定到 G[const_idx]，由 load_const 创建） */
    WOORT_IRVALUE_SOURCE_CONST,

} woort_IRValue_Source;

#define WOORT_IRVALUE_STACK_NOT_ASSIGN INT32_MAX

/*
 * 虚拟寄存器
 */
struct woort_IRValue
{
    woort_IRValue_Source m_source;

    /* 虚拟寄存器的唯一编号（在函数内唯一） */
    uint32_t m_id;

    union
    {
        /* WOORT_IRVALUE_SOURCE_CONST 专用 */
        woort_IRConstantIndex m_const_idx;
    };

    /* finish 阶段填充的栈偏移 */
    int32_t m_assigned_stack_offset;

    /*
     * 常量直连优化 (Phase 3b 填充，仅 SOURCE_CONST 有效)
     *
     * 如果此 CONST vreg 仅被一条支持常量直连的指令（PUSHCHK / RET）使用，
     * 标记为 true。发射层将直接发出 PUSHCCHK / RETVC，跳过 LOAD 和栈槽分配。
     */
    bool m_is_const_direct;

};

void woort_IRValue_init_vreg(woort_IRValue* v, uint32_t id);
void woort_IRValue_init_argument(woort_IRValue* v, uint32_t id, uint32_t argument_idx);
void woort_IRValue_init_captured(woort_IRValue* v, uint32_t id, uint32_t captured_idx);
void woort_IRValue_init_const(woort_IRValue* v, uint32_t id, woort_IRConstantIndex const_idx);
