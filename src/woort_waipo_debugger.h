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

} woort_WAIPO_Debugger;

typedef enum woort_WAIPO_CommandResult
{
    WOORT_WAIPO_CMD_NEED_NEXT,
    WOORT_WAIPO_CMD_CONTINUE,
} woort_WAIPO_CommandResult;

void _woort_WAIPO_Debugger_out_of_focus(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm);

void woort_WAIPO_Debugger_process(
    woort_WAIPO_Debugger* debugger_instance, woort_VMRuntime* vm);
