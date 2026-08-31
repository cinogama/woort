#pragma once

/*
 * woort_vm_debugger_api.h
 */

#include "woort_vm.h"
#include "woort_diagnosis.h"
#include "woort_atomic.h"

#include <stdbool.h>

typedef void (*woort_VMRuntime_DebuggerCallback)(woort_VMRuntime*, void*, bool);
typedef void (*woort_VMRuntime_DebuggerContextDestroyCallback)(void*);

WOORT_NODISCARD bool woort_VMRuntime_Debugger_bootup(void);
void woort_VMRuntime_Debugger_shutdown(void);

WOORT_NODISCARD woort_DebuggerAttachResult woort_VMRuntime_Debugger_attach(
    woort_VMRuntime_DebuggerCallback callback, 
    void* context,
    /* OPTIONAL */ woort_VMRuntime_DebuggerContextDestroyCallback destroy_callback);

void woort_VMRuntime_Debugger_detach(void);

WOORT_NODISCARD bool woort_VMRuntime_Debugger_try_trap(bool trap_by_request);
WOORT_NODISCARD bool woort_VMRuntime_Debugger_handle_external_debug_break_race(woort_VMRuntime* vm);

typedef void(*woort_VMRuntime_Debugger_VerifyVmDoCallback)(woort_VMRuntime*, void*);

WOORT_NODISCARD bool woort_VMRuntime_Debugger_verify_vm_and_do_in_lock(
    woort_VMRuntime* vm_may_invalid,
    woort_VMRuntime_Debugger_VerifyVmDoCallback callback,
    void* userdata);
