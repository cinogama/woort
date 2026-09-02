#pragma once

/*
woort_waipo_debugger.h
*/

#include "woort_hashmap.h"
#include "woort_vm.h"
#include "woort_vector.h"
#include "woort_spin.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct woort_WAIPO_UserBreakpoint
{
    /* 创建时分配的稳定编号（1 起始），删除其他断点不会使其移位 */
    woort_WAIPO_Debugger_BreakpointId m_id;

    /* 一条源码行可能对应多条指令（如 for 头部），断点持有全部地址 */
    woort_Vector /* const woort_Bytecode* */ m_ips;

    /* 0 起始源码行号；SIZE_MAX 表示无行号信息（函数名断点） */
    size_t m_line;

    /* 断点名：源码行断点为文件路径，函数名断点为函数名 */
    char m_desc[256];
} woort_WAIPO_UserBreakpoint;

typedef struct woort_WAIPO_BreakpointCollection
{
    woort_HashMap /* woort_Bytecode*, size_t */ m_breakpoints;

    /* m_debug_breakpoints 是所有无条件的断点，无论 VM 是否处于 m_focusing_vms */
    /* 命中断点都会使得 VM Trap. 值为该地址被多少条用户断点持有（同一地址可被 */
    /* 多条断点共享，计数归零才摘除）；仅用户断点计入，步进断点不进此表。 */
    woort_HashMap /* woort_Bytecode*, size_t */ m_debug_breakpoints;

    /* m_user_breakpoints 跟踪用户通过 break 命令设置的断点，用于列表和删除 */
    woort_Vector /* woort_WAIPO_UserBreakpoint */ m_user_breakpoints;

    /* 下一个待分配的用户断点编号，只增不减；编号不复用，保证已删断点不使存留断点移位 */
    woort_WAIPO_Debugger_BreakpointId m_next_breakpoint_id;

    woort_RWSpinlock m_rwspin;

} woort_WAIPO_BreakpointCollection;

struct woort_WAIPO_Debugger
{
    woort_WAIPO_Debugger_TrapCallback m_trap_callback;

    woort_WAIPO_BreakpointCollection m_breakpoint_collection;
    /* OPTIONAL */ woort_HashMap /* woort_VMRuntime*, woort_WAIPO_VMLocalContext */
        m_focusing_vms;

    bool m_first_breakdown;
    woort_WAIPO_Debugger_FrameId m_current_frame_depth;
    /* OPTIONAL */ woort_VMRuntime* m_current_vm;

    /* Full command line (with args) of the last executed command, so an
     * empty input line can repeat it. Sized to hold any stdin line. */
    char m_last_command[4096];
};

/*
 * 查询 ip 是否为无条件中断的调试断点（无论 VM 是否处于 m_focusing_vms，命中即 Trap）。
 */
WOORT_NODISCARD bool _woort_WAIPO_BreakpointCollection_contains_debug_break_at(
    woort_WAIPO_BreakpointCollection* collection, const woort_Bytecode* ip);

/*
 * 收集 m_debug_breakpoints 中全部无条件断点的指令地址，追加到 modify_break_ips
 * （元素类型 const woort_Bytecode*，由调用方初始化/释放），结果不含重复地址。
 */
void _woort_WAIPO_BreakpointCollection_collect_debug_breakpoints(
    woort_WAIPO_BreakpointCollection* collection,
    woort_Vector /* const woort_Bytecode* */* modify_break_ips);

/*
 * 沿调用栈向内走到第 target_depth 层（0 为栈顶），成功时通过 out_trace
 * 返回该帧信息。
 */
WOORT_NODISCARD bool _woort_WAIPO_trace_to_depth(
    woort_VMRuntime* vm,
    woort_WAIPO_Debugger_FrameId target_depth,
    /* OPTIONAL */ woort_VMRuntime_TraceCallstack* out_trace);

WOORT_NODISCARD woort_WAIPO_TrapEndBehavior woort_WAIPO_Debugger_process_cmdline(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm);

void _woort_WAIPO_print_value(woort_DynBox boxed, bool is_fuzzy);

