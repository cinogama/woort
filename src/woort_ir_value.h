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

    /*
     * 常量直连优化 (Phase 2b 填充)
     *
     * 如果此 vreg 仅由一条 LOAD_CONST 定义且仅被一条支持常量直连的指令
     * （PUSHCHK / RET）使用，则标记为 true。
     * 发射层将直接发出 PUSHCCHK / RETVC，跳过 LOAD 和栈槽分配。
     */
    bool m_is_const_direct;
    woort_IRConstantIndex m_direct_const_index;

} woort_IRValue;

void woort_IRValue_init_vreg(woort_IRValue* v, uint32_t id);
void woort_IRValue_init_argument(woort_IRValue* v, uint32_t id, uint32_t argument_idx);
