#include "woort_vm_debugger.h"
#include "woort_atomic.h"
#include "woort_spin.h"

#include <stdlib.h>
#include <assert.h>

typedef struct woort_VMRuntime_Debugger
{
    woort_VMRuntime_DebuggerCallback m_break_callback;
    /* OPTIONAL */ woort_VMRuntime_DebuggerContextDestroyCallback m_context_destroy_callback;
    /* OPTIONAL */ void* m_debugger_context;

    woort_AtomicUInt32 m_ref_count;

} woort_VMRuntime_Debugger;

static /* OPTIONAL */ woort_VMRuntime_Debugger* g_debugger;
static woort_RWSpinlock g_debugger_rwspin;

void woort_VMRuntime_Debugger_bootup(void)
{
    g_debugger = NULL;
    woort_rwspinlock_init(&g_debugger_rwspin);
}

void woort_VMRuntime_Debugger_shutdown(void)
{
    woort_VMRuntime_Debugger_detach();
    woort_rwspinlock_deinit(&g_debugger_rwspin);
}

void _woort_VMRuntime_Debugger_release_impl(woort_VMRuntime_Debugger* debugger)
{
    if (debugger->m_context_destroy_callback != NULL)
        debugger->m_context_destroy_callback(debugger->m_debugger_context);

    free(debugger);
}

void _woort_VMRuntime_Debugger_disref(woort_VMRuntime_Debugger* debugger)
{
    if (woort_atomic_fetch_sub_explicit(
        &debugger->m_ref_count,
        1,
        WOORT_ATOMIC_MEMORY_ORDER_RELAXED) == 1)
    {
        _woort_VMRuntime_Debugger_release_impl(debugger);
    }
}

void woort_VMRuntime_Debugger_detach(void)
{
    woort_VMRuntime_Debugger* origin_debugger;
    woort_rwspinlock_write_lock(&g_debugger_rwspin);
    {
        origin_debugger = g_debugger;
        g_debugger = NULL;
    }
    woort_rwspinlock_write_unlock(&g_debugger_rwspin);

    if (origin_debugger != NULL)
    {
        _woort_VMRuntime_Debugger_disref(origin_debugger);
    }
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_attach(
    woort_VMRuntime_DebuggerCallback callback,
    void* context,
    /* OPTIONAL */ woort_VMRuntime_DebuggerContextDestroyCallback destroy_callback)
{
    assert(callback != NULL);

    woort_VMRuntime_Debugger* new_debugger = malloc(sizeof(woort_VMRuntime_Debugger));
    if (new_debugger == NULL)
    {
        if (destroy_callback != NULL)
            destroy_callback(context);

        return false;
    }

    new_debugger->m_break_callback = callback;
    new_debugger->m_debugger_context = context;
    new_debugger->m_context_destroy_callback = destroy_callback;
    woort_atomic_init(&new_debugger->m_ref_count, 1);

    woort_rwspinlock_write_lock(&g_debugger_rwspin);
    {
        if (g_debugger != NULL)
            _woort_VMRuntime_Debugger_release_impl(new_debugger);
        else
            g_debugger = new_debugger;

    }
    woort_rwspinlock_write_unlock(&g_debugger_rwspin);

    return g_debugger == new_debugger;
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_try_trap(void)
{
    woort_VMRuntime_Debugger* current_debugger;
    woort_rwspinlock_read_lock(&g_debugger_rwspin);
    {
        current_debugger = g_debugger;

        if (current_debugger != NULL)
        {
            (void)woort_atomic_fetch_add_explicit(
                &current_debugger->m_ref_count,
                1,
                WOORT_ATOMIC_MEMORY_ORDER_RELAXED);
        }
    }
    woort_rwspinlock_read_unlock(&g_debugger_rwspin);

    if (current_debugger != NULL)
    {
        woort_VMRuntime* const running_vm = woort_VMRuntime_swap_running_vm(NULL);
        assert(running_vm != NULL);
        {
            current_debugger->m_break_callback(
                running_vm,
                current_debugger->m_debugger_context);
        }
        (void)woort_VMRuntime_swap_running_vm(running_vm);

        _woort_VMRuntime_Debugger_disref(current_debugger);

        return true;
    }
    return false;
}

