#include "woort_vm_debugger_api.h"
#include "woort_atomic.h"
#include "woort_spin.h"
#include "woort_gc.h"
#include "woort_jit.h"
#include "woort_threads.h"
#include "woort_serialize.h"
#include "woort_util.h"

#include <stdlib.h>
#include <string.h>
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
static woort_Mutex* g_debugger_external_race_mx;

WOORT_NODISCARD bool woort_VMRuntime_Debugger_bootup(void)
{
    if (!woort_mutex_create(&g_debugger_execute_mx))
        return false;

    if (!woort_mutex_create(&g_debugger_external_race_mx))
    {
        woort_mutex_destroy(g_debugger_execute_mx);
        return false;
    }

    g_debugger = NULL;
    woort_rwspinlock_init(&g_debugger_rwspin);
    return true;
}

void woort_VMRuntime_Debugger_shutdown(void)
{
    woort_VMRuntime_Debugger_detach();
    woort_rwspinlock_deinit(&g_debugger_rwspin);
    woort_mutex_destroy(g_debugger_execute_mx);
    woort_mutex_destroy(g_debugger_external_race_mx);

    g_debugger_execute_mx = NULL;
    g_debugger_external_race_mx = NULL;
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

    if (result == WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS)
        woort_JIT_unjit_all_codeenv();

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

static bool _woort_VMRuntime_Debugger_reset_vm_external_debug_break_request(
    woort_VMRuntime* vm, void* user_data)
{
    (void)user_data;
    (void)woort_VMRuntime_request_accept(
        vm,
        WOORT_VMRUNTIME_CHECK_REQUEST_EXTERNAL_DEBUG_BREAK);

    return true;
}


typedef struct _woort_VMRuntime_Debugger_QueryVMsContext
{
    /* OPTIONAL */ woort_VMRuntime** m_out_vms;
    size_t m_buffer_count;
    size_t m_vm_count;
} _woort_VMRuntime_Debugger_QueryVMsContext;

static bool _woort_VMRuntime_Debugger_query_vms_callback(
    woort_VMRuntime* vm, void* user_data)
{
    _woort_VMRuntime_Debugger_QueryVMsContext* const ctx =
        (_woort_VMRuntime_Debugger_QueryVMsContext*)user_data;

    if (ctx->m_vm_count < ctx->m_buffer_count)
        ctx->m_out_vms[ctx->m_vm_count] = vm;

    ++ctx->m_vm_count;
    return true;
}

WOORT_NODISCARD size_t woort_VMRuntime_Debugger_query_vms(
    /* OPTIONAL */ woort_VMRuntime** out_vms,
    size_t vms_buffer_count)
{
    assert(out_vms != NULL || vms_buffer_count == 0);

    _woort_VMRuntime_Debugger_QueryVMsContext ctx = {
        .m_out_vms = out_vms,
        .m_buffer_count = vms_buffer_count,
        .m_vm_count = 0,
    };

    woort_GC_foreach_root_vm(&_woort_VMRuntime_Debugger_query_vms_callback, &ctx);

    return ctx.m_vm_count;
}

void woort_VMRuntime_Debugger_try_breakdown_any_vm(void)
{
    woort_GC_raise_debug_request_in_next_round();
    woort_mem_trigger_gc(true);
}

typedef struct woort_VMRuntime_Debugger_VerifyVmCheckContext
{
    woort_VMRuntime* m_vm_may_invalid;
    woort_VMRuntime_Debugger_VerifyVmDoCallback m_check_do_callback;
    void* m_userdata;

    bool m_done;

}woort_VMRuntime_Debugger_VerifyVmCheckContext;

static bool _woort_VMRuntime_Debugger_verify_vm_and_do_in_lock(
    woort_VMRuntime* vm, void* user_data)
{
    woort_VMRuntime_Debugger_VerifyVmCheckContext* const ctx = user_data;
    if (vm == ctx->m_vm_may_invalid)
    {
        ctx->m_done = true;
        ctx->m_check_do_callback(vm, ctx->m_userdata);
        return false;
    }
    return true;
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_verify_vm_and_do_in_lock(
    woort_VMRuntime* vm_may_invalid,
    woort_VMRuntime_Debugger_VerifyVmDoCallback callback,
    void* userdata)
{
    woort_VMRuntime_Debugger_VerifyVmCheckContext ctx = {
        .m_vm_may_invalid = vm_may_invalid,
        .m_check_do_callback = callback,
        .m_userdata = userdata,

        .m_done = false,
    };
    woort_GC_foreach_root_vm(&_woort_VMRuntime_Debugger_verify_vm_and_do_in_lock, &ctx);
    return ctx.m_done;
}

static void _woort_VMRuntime_Debugger_breakdown_vm(woort_VMRuntime* vm, void* userdata)
{
    (void)userdata;
    (void)woort_VMRuntime_request_set(vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_BREAK);
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_try_breakdown_vm(woort_VMRuntime* vm_may_invalid)
{
    return woort_VMRuntime_Debugger_verify_vm_and_do_in_lock(
        vm_may_invalid,
        &_woort_VMRuntime_Debugger_breakdown_vm,
        NULL);
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_handle_external_debug_break_race(
    woort_VMRuntime* vm)
{
    bool race_succeed = false;
    woort_mutex_lock(g_debugger_external_race_mx);
    {
        if (woort_VMRuntime_request_accept(
            vm, WOORT_VMRUNTIME_CHECK_REQUEST_EXTERNAL_DEBUG_BREAK))
        {
            /* Success, clean other VM's request. */
            woort_GC_foreach_root_vm(
                _woort_VMRuntime_Debugger_reset_vm_external_debug_break_request, NULL);

            /* Check whether a debugger is already in operation. */
            if (woort_mutex_trylock(g_debugger_execute_mx))
            {
                /*
                NOTE: The approach of using the lock status of g_debugger_execute_mx
                    to infer whether the debugger is active is not particularly precise;
                    nevertheless, external debugging requests do not demand stringent
                    conditions.
                */
                woort_mutex_unlock(g_debugger_execute_mx);
                race_succeed = true;
            }
        }
        else
        {
            /*  Another VM has reached, failed */
            (void)woort_VMRuntime_request_accept(
                vm, WOORT_VMRUNTIME_CHECK_REQUEST_EXTERNAL_DEBUG_BREAK);
        }
    }
    woort_mutex_unlock(g_debugger_external_race_mx);
    return race_succeed;
}

static bool _woort_woort_VMRuntime_Debugger_terminate_vm_callback(
    woort_VMRuntime* vm, void* user_data)
{
    (void)user_data;
    (void)woort_VMRuntime_request_set(
        vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE);
    return true;
}

void woort_VMRuntime_Debugger_terminate_all_vm(void)
{
    woort_GC_foreach_root_vm(
        &_woort_woort_VMRuntime_Debugger_terminate_vm_callback, NULL);
}

static void _woort_VMRuntime_Debugger_terminate_vm(woort_VMRuntime* vm, void* userdata)
{
    (void)userdata;
    (void)woort_VMRuntime_request_set(vm, WOORT_VMRUNTIME_CHECK_REQUEST_TERMINATE);
}

WOORT_NODISCARD bool woort_VMRuntime_Debugger_try_terminate_vm(
    woort_VMRuntime* vm_may_invalid)
{
    return woort_VMRuntime_Debugger_verify_vm_and_do_in_lock(
        vm_may_invalid,
        &_woort_VMRuntime_Debugger_terminate_vm,
        NULL);
}

WOORT_NODISCARD size_t woort_VMRuntime_Debugger_serialize_value(
    woort_Value* value,
    char* out_value_content,
    size_t value_content_buf_len)
{
    assert(value != NULL);
    assert(out_value_content != NULL || value_content_buf_len == 0);

    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    /*
    NOTE: Fuzzy mode matches the debugger `print` command: the value usually
        comes straight from a stack frame or static storage
        (VariableInfo.m_value) and may hold raw bits that are not a
        well-formed DynBox.
    */
    const bool ok = _woort_serialize_dynbox_to_buf_for_debug(
        value->m_dynamic, &buf, &visited_set, 0, true);

    const size_t content_len = ok ? buf.m_size : 0;

    if (ok && value_content_buf_len != 0)
    {
        memcpy(
            out_value_content,
            buf.m_data,
            content_len < value_content_buf_len ? content_len : value_content_buf_len);

        if (content_len < value_content_buf_len)
            out_value_content[content_len] = '\0';
    }

    woort_vector_deinit(&buf);
    woort_hashmap_deinit(&visited_set);

    return content_len;
}
