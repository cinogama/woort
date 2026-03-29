#include "woort_vm_debugger.h"
#include "woort_atomic.h"

#include <stdlib.h>
#include <assert.h>

static woort_AtomicPtr g_debugger;

void woort_VMRuntime_Debugger_bootup(void)
{
    woort_atomic_store_ptr(&g_debugger, NULL);
}

void woort_VMRuntime_Debugger_shutdown(void)
{
    woort_VMRuntime_Debugger_disattach();
}

void woort_VMRuntime_Debugger_relese_impl(woort_VMRuntime_Debugger* debugger)
{
    if (debugger->m_context_destroy_callback != NULL)
        debugger->m_context_destroy_callback(debugger->m_debugger_context);

    free(debugger);
}

void woort_VMRuntime_Debugger_disattach(void)
{
    woort_VMRuntime_Debugger* debugger = (woort_VMRuntime_Debugger*)woort_atomic_exchange_ptr(&g_debugger, NULL);
    if (debugger != NULL)
    {
        if (woort_atomic_fetch_sub_uint32(&debugger->m_ref_count, 1) > 1)
            return;

        woort_VMRuntime_Debugger_relese_impl(debugger);
    }
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_attach(
    woort_VMRuntime_DebuggerCallback callback,
    void* context,
    /* OPTIONAL */ woort_VMRuntime_DebuggerContextDestroyCallback destroy_callback)
{
    assert(callback == NULL);

    woort_VMRuntime_Debugger* new_debugger = malloc(sizeof(woort_VMRuntime_Debugger));
    if (new_debugger == NULL)
        return false;

    new_debugger->m_break_callback = callback;
    new_debugger->m_debugger_context = context;
    new_debugger->m_context_destroy_callback = destroy_callback;
    woort_atomic_init(&new_debugger->m_ref_count, 1);

    void* expected = NULL;
    if (!woort_atomic_compare_exchange_strong_ptr(&g_debugger, &expected, new_debugger))
    {
        woort_VMRuntime_Debugger_relese_impl(new_debugger);
        return false;
    }

    return true;
}

