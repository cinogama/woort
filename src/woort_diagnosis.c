#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_vm.h"
#include "woort_vm_debugger_api.h"
#include "woort_gc_string.h"
#include "woort_setting.h"
#include "woort_atomic.h"
#include "woort_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static woort_AtomicPtr /* OPTIONAL woort_PanicHandlerFunction */
_woort_panic_handler_callback = NULL;

WOORT_NODISCARD static bool _woort_diagnosis_str_equal(
    const char* a, const char* b)
{
    if (a == b)
        return true;
    if (a == NULL || b == NULL)
        return false;
    return strcmp(a, b) == 0;
}

WOORT_NODISCARD static bool _woort_diagnosis_trace_equal(
    const woort_VMRuntime_TraceCallstack* a,
    const woort_VMRuntime_TraceCallstack* b)
{
    if (!_woort_diagnosis_str_equal(a->m_function_name, b->m_function_name))
        return false;
    if (!_woort_diagnosis_str_equal(a->m_file_or_lib_name, b->m_file_or_lib_name))
        return false;
    if (a->m_has_location != b->m_has_location
        || a->m_location_begin[0] != b->m_location_begin[0]
        || a->m_location_begin[1] != b->m_location_begin[1]
        || a->m_location_end[0] != b->m_location_end[0]
        || a->m_location_end[1] != b->m_location_end[1])
        return false;
    return true;
}

static const char* const DEFAULT_PANIC_DESCRIBE_MESSAGE =
"Failed to get panic describe message.";

static void _woort_abort_vm_by_panic(
    woort_VMRuntime* panic_vm,
    /* OPTIONAL */ const char* msg)
{
    if (msg == NULL)
        msg = DEFAULT_PANIC_DESCRIBE_MESSAGE;

    const woort_GCString* panic_str =
        woort_GCString_make_string(msg, strlen(msg));

    panic_vm->m_sp->m_string = panic_str;
}

WOORT_NODISCARD bool woort_vpanic(
    woort_PanicReason reason,
    const char* msgfmt,
    va_list args)
{
    /* OPTIONAL */ char* panic_describe_message =
        woort_dupstr_with_format_v(msgfmt, args);

    /* OPTIONAL */ woort_PanicHandlerFunction handler =
        (woort_PanicHandlerFunction)woort_atomic_load_explicit(
            &_woort_panic_handler_callback,
            WOORT_ATOMIC_MEMORY_ORDER_ACQUIRE);

    bool vm_has_been_aborted = true;
    woort_VMRuntime* const panic_vm = WOORT_t_this_thread_vm;
    {
        if (panic_vm == NULL
            || woort_VMRuntime_request_set(panic_vm, WOORT_VMRUNTIME_CHECK_REQUEST_ABORT))
        {
            woort_PanicHandler_Action user_handler_action =
                WOORT_PANIC_HANDLER_ACTION_USE_DEFAULT_HANDLER;

            if (handler != NULL)
            {
                woort_VMRuntime* const last_vm = woort_VMRuntime_swap(NULL);
                user_handler_action = handler(panic_vm, reason, panic_describe_message);
                (void)woort_VMRuntime_swap(last_vm);
            }

            switch (user_handler_action)
            {
            case WOORT_PANIC_HANDLER_ACTION_ABORT:
                break;
            case WOORT_PANIC_HANDLER_ACTION_CONTINUE:
                vm_has_been_aborted = false;
                if (panic_vm != NULL)
                    (void)woort_VMRuntime_request_accept(
                        panic_vm, WOORT_VMRUNTIME_CHECK_REQUEST_ABORT);
                break;
            case WOORT_PANIC_HANDLER_ACTION_USE_DEFAULT_HANDLER:
                woort_log(
                    "WooRT Panic: Fatal runtime error(%X). "
                    "Program execution terminated:\n    ", reason);

                woort_log("%s\nTrace:\n", 
                    panic_describe_message != NULL 
                    ? panic_describe_message
                    : DEFAULT_PANIC_DESCRIBE_MESSAGE);

                if (panic_vm != NULL)
                {
                    woort_VMRuntime_TraceCallstack_Iter trace_iter;
                    woort_VMRuntime_TraceCallstack trace;
                    woort_VMRuntime_TraceCallstack prev_trace;
                    size_t repeat_count = 0;
                    bool has_prev = false;

                    woort_VMRuntime_trace_begin(panic_vm, &trace_iter);
                    while (woort_VMRuntime_trace_next(&trace_iter, &trace))
                    {
                        if (has_prev
                            && _woort_diagnosis_trace_equal(&prev_trace, &trace))
                        {
                            repeat_count++;
                        }
                        else
                        {
                            if (has_prev)
                            {
                                woort_VMRuntime_log_trace(&prev_trace);
                                if (repeat_count > 0)
                                    woort_log("    ... (repeated %zu time%s)\n",
                                        repeat_count, repeat_count > 1 ? "s" : "");
                            }
                            prev_trace = trace;
                            repeat_count = 0;
                            has_prev = true;
                        }
                    }

                    if (has_prev)
                    {
                        woort_VMRuntime_log_trace(&prev_trace);
                        if (repeat_count > 0)
                            woort_log("    ... (repeated %zu time%s)\n",
                                repeat_count, repeat_count > 1 ? "s" : "");
                    }
                }
                else
                    woort_log("    No vm running on this thread.\n");

                switch (_woort_setting_HALT_PANIC_VM_MODE)
                {
                case WOORT_HALT_PANIC_VM_MODE_VM_ITSELF:
                    if (panic_vm != NULL)
                        woort_log("Current VM will be aborted.\n");
                    break;
                case WOORT_HALT_PANIC_VM_MODE_PROCESS:
                    woort_log("Current process will be aborted.\n");
                    abort();
                case WOORT_HALT_PANIC_VM_MODE_SIGNAL:
                    woort_log("Current process will be aborted by SIGABRT.\n");
                    raise(SIGABRT);

                    /* Signal handled? */
                    abort();
                default:
                    woort_log("Unknown panic mode: %d.\n", (int)_woort_setting_HALT_PANIC_VM_MODE);
                    /* fallthrough */
                case WOORT_HALT_PANIC_VM_MODE_DONOT_HALT:
                    woort_log("1) Abort process.\n");
                    woort_log("2) Ignore.\n");
                    if (panic_vm != NULL)
                    {
                        woort_log("3) Terminate current vm.\n");
                        woort_log("4) Attach debuggee.\n");
                    }

                    woort_log("Please input your choice: ");

                    bool option_selected = false;
                    do
                    {
                    label_reenter_to_skip_white_space:
                        ;
                        woort_VMRuntime* const last_vm = woort_VMRuntime_swap(NULL);
                        int c = getchar();
                        (void)woort_VMRuntime_swap(last_vm);

                        switch (c)
                        {
                        case EOF:
                            // Failed to read from stdin.
                            woort_log("Failed to receive from STDIN.");
                            abort();
                        case '\n':
                        case '\r':
                        case ' ':
                            goto label_reenter_to_skip_white_space;
                        case '1':
                            option_selected = true;
                            abort();
                            break;
                        case '2':
                            option_selected = true;
                            vm_has_been_aborted = false;
                            if (panic_vm != NULL)
                            {
                                // Cancel WOORT_VMRUNTIME_CHECK_REQUEST_ABORT request to make vm continue.
                                (void)woort_VMRuntime_request_accept(
                                    panic_vm, WOORT_VMRUNTIME_CHECK_REQUEST_ABORT);
                            }
                            break;
                        case '3':
                            if (panic_vm != NULL)
                            {
                                option_selected = true;
                                break;
                            }
                            /* fallthrough */
                        case '4':
                            if (panic_vm != NULL)
                            {
                                option_selected = true;
                                vm_has_been_aborted = false;

                                // Cancel WOORT_VMRUNTIME_CHECK_REQUEST_ABORT request to make vm continue.
                                (void)woort_VMRuntime_request_accept(
                                    panic_vm, WOORT_VMRUNTIME_CHECK_REQUEST_ABORT);
                                (void)woort_VMRuntime_request_set(
                                    panic_vm, WOORT_VMRUNTIME_CHECK_REQUEST_DEBUG_CALLBACK);

                                (void)woort_WAIPO_Debugger_attach();
                                break;
                            }
                            /* fallthrough */
                        default:
                            woort_log("Invalid choice.");
                            break;
                        }

                    } while (!option_selected);
                    break;
                }

                break;
            default:
                WOORT_DEBUG("Unknown user panic hook callback action.");
                abort();
            }
        }
        /* else: This VM already aborted or no VM to abort, skip. */
    }

    if (vm_has_been_aborted && panic_vm)
        _woort_abort_vm_by_panic(panic_vm, panic_describe_message);

    if (panic_describe_message != NULL)
        free(panic_describe_message);

    return vm_has_been_aborted;
}

void woort_panic(
    woort_PanicReason reason,
    const char* msgfmt,
    ...)
{
    va_list args;
    va_start(args, msgfmt);

    (void)woort_vpanic(reason, msgfmt, args);

    va_end(args);
}

WOORT_NODISCARD /* OPTIONAL */ woort_PanicHandlerFunction woort_set_panic_callback(
    /* OPTIONAL */ woort_PanicHandlerFunction callback)
{
    return (woort_PanicHandlerFunction)
        woort_atomic_exchange_explicit(
            &_woort_panic_handler_callback,
            callback,
            WOORT_ATOMIC_MEMORY_ORDER_ACQ_REL);
}
