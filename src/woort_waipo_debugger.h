#pragma once

/*
woort_waipo_debugger_cmd.h
*/

#include "woort_hashmap.h"
#include "woort_vm.h"

#include <stdbool.h>

typedef struct woort_WAIPO_BreakpointCollection
{
    woort_HashMap /* woort_Bytecode*, size_t */ m_breakpoints;

    /* m_debug_breakpoints 是所有无条件的断点，无论 VM 是否处于 m_focusing_vms */
    /* 命中断点都会使得 VM Trap. */
    woort_HashMap /* woort_Bytecode*, None */ m_debug_breakpoints;

} woort_WAIPO_BreakpointCollection;

typedef struct woort_WAIPO_Debugger
{
    woort_WAIPO_BreakpointCollection m_breakpoint_collection;
    /* OPTIONAL */ woort_HashMap /* woort_VMRuntime*, woort_WAIPO_VMLocalContext */
        m_focusing_vms;

    bool m_first_breakdown;
    char m_last_command[256];

    size_t m_current_frame_depth;

} woort_WAIPO_Debugger;

typedef enum woort_WAIPO_CommandResult
{
    WOORT_WAIPO_CMD_NEED_NEXT,
    WOORT_WAIPO_CMD_CONTINUE,
} woort_WAIPO_CommandResult;

void _woort_WAIPO_Debugger_out_of_focus(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm);

bool _woort_WAIPO_Debugger_focus_on(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm);

bool _woort_WAIPO_Debugger_set_step_break(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm,
    const woort_Bytecode* ip);

bool _woort_WAIPO_Debugger_set_step_source_break(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm,
    const woort_Bytecode* ip);

bool _woort_WAIPO_Debugger_set_next_source_break(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm,
    const woort_Bytecode* ip);

void woort_WAIPO_Debugger_process(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm);

/*
 * 根据当前指令和 VM 状态计算下一条指令的地址（用于单步执行）。
 * 考虑跳转、调用、返回等所有控制流转移情况。
 * 返回 false 表示无法确定下一条指令（如从 native 函数返回）。
 */
WOORT_NODISCARD bool _woort_WAIPO_get_next_ip(
    const woort_Bytecode*   ip,
    woort_CodeEnv*          cenv,
    const woort_Value*      sb,
    /* OPTIONAL */ const woort_Bytecode**  out_next_ip);
