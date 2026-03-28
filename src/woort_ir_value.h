#pragma once

/*
 * woort_ir_value.h
 *
 * IR 虚拟寄存器（可变）定义。
 * 每个 woort_IRValue 代表一个无限虚拟寄存器，可被多次读写。
 * finish 阶段通过活跃性分析将虚拟寄存器映射到栈槽。
 */

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t woort_IRConstantIndex;
typedef uint32_t woort_IRStaticIndex;

/*
 * 虚拟寄存器来源类型
 */
typedef enum woort_IRValue_Source
{
    /* 普通虚拟寄存器（用户通过 new_vreg 创建） */
    WOORT_IRVALUE_SOURCE_VREG,

    /* 函数参数（预分配到 SB+3+idx） */
    WOORT_IRVALUE_SOURCE_ARGUMENT,

} woort_IRValue_Source;

#define WOORT_IRVALUE_STACK_NOT_ASSIGN INT32_MAX

/*
 * 虚拟寄存器
 *
 * 可变的虚拟寄存器，可在任意位置被写入和读取。
 * finish 阶段分配 m_assigned_stack_offset。
 */
typedef struct woort_IRValue
{
    woort_IRValue_Source m_source;

    /* 虚拟寄存器的唯一编号（在函数内唯一） */
    uint32_t m_id;

    union
    {
        /* WOORT_IRVALUE_SOURCE_ARGUMENT 专用 */
        uint32_t m_argument_idx;
    };

    /* finish 阶段填充的栈偏移 */
    int32_t m_assigned_stack_offset;

} woort_IRValue;

void woort_IRValue_init_vreg(woort_IRValue* v, uint32_t id);
void woort_IRValue_init_argument(woort_IRValue* v, uint32_t id, uint32_t argument_idx);
