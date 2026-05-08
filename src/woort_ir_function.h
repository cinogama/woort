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
#include "woort_ir_srcloc.h"
#include "woort_linklist.h"
#include "woort_vector.h"
#include "woort_diagnosis.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct woort_IRFunction
{
    uint32_t m_param_count;
    uint32_t m_captured_count;

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

    /* 函数名称（编译期设置，finish 阶段转移到 CodeEnv 的字符串池） */
    /* OPTIONAL */ const char* m_name;

    /* finish 阶段：函数字节码的起始偏移 (在 compiler 的总代码中) */
    size_t m_code_offset;
    size_t m_code_length;

    /* === 源码位置支持 === */

    /* 源码位置栈（编译期 push/pop 控制当前 IR 的源码信息） */
    woort_SourceLocationStack m_srcloc_stack;

    /* 去重的源码位置池，IR 指令的 m_srcloc_index 索引此数组 */
    woort_Vector /* woort_SourceLocation */ m_source_locations;
};

void woort_IRFunction_init(
    woort_IRFunction* f, uint32_t param_count, uint32_t captured_count);
void woort_IRFunction_deinit(woort_IRFunction* f);

/* finish 阶段内部函数 */
WOORT_NODISCARD bool _woort_IRFunction_analyze_and_allocate(
    woort_IRFunction* f, size_t* out_stack_space);

/*
 * 获取当前栈顶源码位置对应的 m_source_locations 索引。
 * 如果栈为空，返回 WOORT_SRCLOC_INVALID_INDEX。
 * 内部使用：会在 m_source_locations 中去重查找或新增。
 */
WOORT_NODISCARD uint32_t _woort_IRFunction_current_srcloc_index(
    woort_IRFunction* f);
