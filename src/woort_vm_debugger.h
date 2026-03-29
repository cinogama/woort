#pragma once

/*
woort_vm_debugger.h
*/

#include "woort_vm.h"
#include "woort_diagnosis.h"
#include "woort_atomic.h"

#include <stdbool.h>

typedef void (*woort_VMRuntime_DebuggerCallback)(woort_VMRuntime*, void*);
typedef void (*woort_VMRuntime_DebuggerContextDestroyCallback)(void*);

typedef struct woort_VMRuntime_Debugger
{
    woort_VMRuntime_DebuggerCallback m_break_callback;
    /* OPTIONAL */ woort_VMRuntime_DebuggerContextDestroyCallback m_context_destroy_callback;
    void* m_debugger_context;

    woort_AtomicUInt32 m_ref_count;

} woort_VMRuntime_Debugger;

void woort_VMRuntime_Debugger_bootup(void);
void woort_VMRuntime_Debugger_shutdown(void);

WOORT_NODISCARD bool woort_VMRuntime_Debugger_attach(
    woort_VMRuntime_DebuggerCallback callback, 
    void* context,
    /* OPTIONAL */ woort_VMRuntime_DebuggerContextDestroyCallback destroy_callback);

void woort_VMRuntime_Debugger_disattach(void);

WOORT_NODISCARD bool woort_VMRuntime_Debugger_try_invoke(woort_VMRuntime* vm);
