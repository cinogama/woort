#include "woort_vm_debugger_api.h"
#include "woort_atomic.h"
#include "woort_spin.h"
#include "woort_gc.h"
#include "woort_jit.h"

#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

typedef struct woort_VMRuntime_Debugger
{
    woort_VMRuntime_DebuggerCallback m_break_callback;
    /* OPTIONAL */ woort_VMRuntime_DebuggerContextDestroyCallback m_context_destroy_callback;
    /* OPTIONAL */ void* m_debugger_context;

    woort_AtomicUInt32 m_ref_count;

} woort_VMRuntime_Debugger;

static /* OPTIONAL */ woort_VMRuntime_Debugger* g_debugger;
static woort_RWSpinlock g_debugger_rwspin;
static woort_Mutex* g_debugger_execute_mx;

WOORT_NODISCARD bool woort_VMRuntime_Debugger_bootup(void)
{
    if (!woort_mutex_create(&g_debugger_execute_mx))
        return false;

    g_debugger = NULL;
    woort_rwspinlock_init(&g_debugger_rwspin);
    return true;
}

void woort_VMRuntime_Debugger_shutdown(void)
{
    woort_VMRuntime_Debugger_detach();
    woort_rwspinlock_deinit(&g_debugger_rwspin);
    woort_mutex_destroy(g_debugger_execute_mx);

    g_debugger_execute_mx = NULL;
}

static void _woort_VMRuntime_Debugger_release_impl(woort_VMRuntime_Debugger* debugger)
{
    if (debugger->m_context_destroy_callback != NULL)
        debugger->m_context_destroy_callback(debugger->m_debugger_context);

    free(debugger);
}

static void _woort_VMRuntime_Debugger_disref(woort_VMRuntime_Debugger* debugger)
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

WOORT_NODISCARD woort_DebuggerAttachResult woort_VMRuntime_Debugger_attach(
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

        return WOORT_DEBUGGER_ATTACH_RESULT_FAILED;
    }

    new_debugger->m_break_callback = callback;
    new_debugger->m_debugger_context = context;
    new_debugger->m_context_destroy_callback = destroy_callback;
    woort_atomic_init(&new_debugger->m_ref_count, 1);

    woort_DebuggerAttachResult result;
    woort_rwspinlock_write_lock(&g_debugger_rwspin);
    {
        if (g_debugger != NULL)
        {
            _woort_VMRuntime_Debugger_release_impl(new_debugger);
            result = WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED;
        }
        else
        {
            g_debugger = new_debugger;
            result = WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS;
        }

    }
    woort_rwspinlock_write_unlock(&g_debugger_rwspin);

    //if (result == WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS)
    //    woort_JIT_unjit_all_codeenv();

    return result;
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_try_trap(bool trap_by_request)
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
        woort_VMRuntime* const running_vm = woort_VMRuntime_swap(NULL);
        assert(running_vm != NULL);
        {
            woort_mutex_lock(g_debugger_execute_mx);
            {
                current_debugger->m_break_callback(
                    running_vm,
                    current_debugger->m_debugger_context,
                    trap_by_request);
            }
            woort_mutex_unlock(g_debugger_execute_mx);
        }
        (void)woort_VMRuntime_swap(running_vm);

        _woort_VMRuntime_Debugger_disref(current_debugger);

        return true;
    }
    return false;
}

void woort_VMRuntime_Debugger_breakdown_all_vm(void)
{
    _woort_GC_debug_callback_all_vm();
}
