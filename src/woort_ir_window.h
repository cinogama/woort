#pragma once

/*
 * woort_ir_window.h
 *
 * 槽位窗口（V2）：一组由同一条消费指令（CALL*2 / MK*2）读取的操作数槽位。
 *
 * 槽位是普通的 woort_IRValue（来源 WOORT_IRVALUE_SOURCE_SLOT），可以作任何指令的
 * 目标，实参表达式因此能把结果直接写进槽位，不再经过 PUSHCHK。
 *
 * finish 阶段把窗口放进函数入口预留的操作数区：
 *   - 位置 0 紧贴预留后的 sp，位置 p 位于 sp[1+p]；
 *   - 窗口占连续的 count 个位置，消费时 sp 指向窗口最低位置的正下方，
 *     MK*NP / CALL 读到的 sp[1..count] 就是窗口；
 *   - 窗口之下不能有存活的数据，所以窗口落在祖先窗口已写槽位的下方；
 *     嵌套窗口因此可以复用外层尚未写入的位置。窗口不在位置 0 时消费前发射
 *     SPSET 校正 sp（见 woort_ir_function.c 的 Phase 3c）。
 */

#include "woort.h"

#include "woort_ir_value.h"
#include "woort_ir_op.h"

#include <stdint.h>
#include <stdbool.h>

struct woort_IRWindow
{
    woort_IRFunction* m_function;
    uint32_t m_id;             /* 在函数窗口表 m_ir_windows 中的下标 */
    uint32_t m_count;

    /* 创建时的指令数：数据流执行到该下标的指令之前，窗口打开 */
    uint32_t m_create_index;

    /* 发射期：消费指令是否已发射 */
    bool m_consumed;

    /* ---- 以下由 finish 阶段填充 ---- */

    /* Phase 1c：打开时刻的祖先窗口（最外层在前）以及其中已写的槽位，
       拼接记录在函数的 m_window_ancestors / m_window_live 中 */
    uint32_t m_ancestor_begin;
    uint32_t m_ancestor_count;
    uint32_t m_live_begin;
    uint32_t m_live_count;

    /* 消费指令在指令列表中的下标 */
    uint32_t m_consumer_index;

    /* Phase 3c：布局。m_base 是窗口最低的位置 */
    int32_t m_base;
    bool m_push_lowered;       /* 回退为压栈降级（槽位参与线性扫描） */

    woort_IRValue* m_slots[];
};

/* 创建窗口及其 count 个槽位；失败返回 NULL */
WOORT_NODISCARD /* OPTIONAL */ woort_IRWindow* _woort_IRWindow_create(
    woort_IRFunction* f, uint32_t count);
void _woort_IRWindow_destroy(woort_IRWindow* w);

/*
 * 消费指令发射前的检查：窗口属于本函数且尚未被消费。
 * 通过时登记为已消费并记下消费指令的下标。
 */
WOORT_NODISCARD bool _woort_IRWindow_consume(
    woort_IRFunction* f, woort_IRWindow* w);
