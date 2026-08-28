#pragma once

/*
 * woort_debugger_session.h
 *
 * Programmatic debugger session: the frontend behind the public
 * woort_Debugger_* API declared in woort.h.  It attaches to the generic
 * debugger pipeline (woort_vm_debugger_api) and, instead of running an
 * interactive REPL, parks trapped VMs and lets an external host pull stop
 * events, inspect state and issue resume actions - see the contract in
 * woort.h.
 *
 * The session also owns the debugging machinery directly: the trap maps
 * below plus the per-VM stepping state (m_focusing_vms), implemented
 * alongside the rest of the session in woort_debugger_session.c.
 */

#include "woort.h"
#include "woort_threads.h"
#include "woort_hashmap.h"
#include "woort_vector.h"
#include "woort_vm.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum woort_DebuggerResumeAction
{
    _WOORT_DEBUGGER_RESUME_CONTINUE,
    _WOORT_DEBUGGER_RESUME_STEP_INSTRUCTION,
    _WOORT_DEBUGGER_RESUME_STEP_IN,
    _WOORT_DEBUGGER_RESUME_STEP_OVER,
    _WOORT_DEBUGGER_RESUME_STEP_OUT,

} woort_DebuggerResumeAction;

typedef struct woort_DebuggerBreakpointRecord
{
    woort_DebuggerBreakpointId m_id;

    bool m_is_function_bp;
    char m_file[WOORT_DEBUGGER_MAX_PATH];
    uint32_t m_line; /* 1-based request line (source breakpoints)       */
    char m_function_name[WOORT_DEBUGGER_MAX_NAME];

    bool m_resolved;
    uint32_t m_resolved_line; /* 1-based line the trap actually landed on */

    /* woort_Bytecode* entries the record placed into m_breakpoints. */
    woort_Vector m_applied_ips;

} woort_DebuggerBreakpointRecord;

typedef struct woort_DebuggerSession
{
    /* Refcounted map of ip -> stop count.  Any entry here causes a stop
       when hit, no matter which VM executes it; user breakpoints and the
       transient stepping traps share it. */
    woort_HashMap /* woort_Bytecode*, size_t */ m_breakpoints;

    /* m_debug_breakpoints 是所有无条件的断点，无论 VM 是否处于 m_focusing_vms */
    /* 命中断点都会使得 VM Trap. */
    woort_HashMap /* woort_Bytecode*, None */ m_debug_breakpoints;

    /* 单步（step/next/return）期间才持有条目，键为 VM，值为该 VM 的步进状态 */
    /* OPTIONAL */ woort_HashMap /* woort_VMRuntime*, per-VM step context */
        m_focusing_vms;

    bool m_attached; /* between attach and detach                     */
    bool m_detached; /* terminal: no further stops are delivered      */

    woort_Mutex* m_mx;
    woort_ConditionVariable* m_cv;

    /* woort_DebuggerBreakpointRecord */
    woort_Vector m_breakpoint_records;
    woort_DebuggerBreakpointId m_next_bp_id;

    /* Active stop; m_stop_vm is only read while m_mx is held. */
    bool m_stop_active;
    /* OPTIONAL */ woort_VMRuntime* m_stop_vm;
    woort_DebuggerStopReason m_stop_reason;
    bool m_action_ready;

    bool m_has_panic;
    woort_DebuggerPanicInfo m_panic;

    /* OPTIONAL */ woort_PanicHandlerFunction m_prev_panic_handler;

} woort_DebuggerSession;

/*
 * Lifecycle hooks invoked from the debugger pipeline bootup/shutdown.
 * Bootup prepares the attach/detach serialization lock; shutdown detaches
 * a still-attached session and releases that lock.
 */
WOORT_NODISCARD bool _woort_Debugger_session_bootup(void);
void _woort_Debugger_session_shutdown(void);

/*
 * In-tree escape hatch for the WAIPO CLI frontend: the VM of the current
 * stop, for commands that need runtime internals (raw bytecode offset,
 * const/static slot dump) which have no public API equivalent.  Only
 * meaningful while a stop is active.
 */
WOORT_NODISCARD bool _woort_Debugger_session_take_stopped_vm(
    /* OPTIONAL */ woort_VMRuntime** out_vm);
