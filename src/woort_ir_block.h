#pragma once

/*
 * woort_ir_block.h
 *
 * 新 IR 接口：
 *   - woort_IRLabel: 跳转目标
 *   - woort_IRBlock: 内部概念，finish() 时从 Label/跳转自动切分
 *   - woort_IR_*: 指令发射函数（在 IRFunction 上操作）
 *
 * 用户通过 woort_IR_* 函数向 IRFunction 追加指令。
 * 控制流通过 Label + 显式 JMP/JCC 表达。
 */

#include "woort.h"

#include "woort_ir_value.h"
#include "woort_ir_op.h"
#include "woort_vector.h"
#include "woort_bitset.h"
#include "woort_diagnosis.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Label：跳转目标
 *
 * 由 woort_IRFunction_new_label 创建。
 * 用 woort_IR_bind 绑定到当前指令位置。
 * 在 finish 阶段解析为字节码地址。
 */
struct woort_IRLabel
{
    uint32_t m_id;

    /* finish 阶段填充 */
    bool m_bound;           /* 是否已绑定 */
    uint32_t m_bind_index;  /* 在指令列表中的位置（绑定的 IROp 索引） */
    uint32_t m_block_index; /* 所属 block 编号 */
};

/*
 * 基本块（内部数据结构）
 *
 * 在 finish() 时由框架根据 Label 和跳转指令自动切分。
 * 用户不直接创建或操作 Block。
 */
typedef struct woort_IRBlock
{
    /* 在指令列表中的范围 [m_begin, m_end) */
    uint32_t m_begin;
    uint32_t m_end;

    /* CFG 边 */
    woort_Vector /* uint32_t (block index) */ m_successors;
    woort_Vector /* uint32_t (block index) */ m_predecessors;

    /* 活跃性分析 */
    woort_Bitset m_use;
    woort_Bitset m_def;
    woort_Bitset m_live_in;
    woort_Bitset m_live_out;

    /* Dominator 分析 */
    int32_t m_idom;     /* immediate dominator block index, -1 if none */
    uint32_t m_dom_depth;

    /* 循环信息 */
    bool m_is_in_loop;
    int32_t m_loop_header;  /* loop header block index, -1 if not in loop */

    /* 常量加载放置 */
    woort_Vector /* _woort_ConstLoadInfo */ m_const_loads;

    /* 字节码发射结果 */
    woort_Vector /* woort_Bytecode */ m_bytecodes;

} woort_IRBlock;

void _woort_IRBlock_init(woort_IRBlock* block);
void _woort_IRBlock_deinit(woort_IRBlock* block);
