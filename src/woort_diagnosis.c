#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_vm.h"
#include "woort_vm_debugger_api.h"
#include "woort_gc_string.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

WOORT_NODISCARD bool woort_vpanic(
    woort_PanicReason reason,
    const char* msgfmt,
    va_list args)
{
    bool vm_has_been_aborted = true;
    woort_VMRuntime* const panic_vm = WOORT_t_this_thread_vm;
    {
        if (panic_vm == NULL
            || woort_VMRuntime_request_set(panic_vm, WOORT_VMRUNTIME_CHECK_REQUEST_ABORT))
        {
            woort_log(
                "WooRT Panic: Fatal runtime error(%X). "
                "Program execution terminated:\n    ", reason);

            va_list args_copy;
            if (panic_vm != NULL)
                va_copy(args_copy, args);

            woort_vlog(msgfmt, args);

            woort_log("\nTrace:\n");

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

            woort_log("1) Abort whole program.\n");
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

                        const woort_GCString* panic_str =
                            woort_GCString_make_format_va(msgfmt, args_copy);
                        va_end(args_copy);

                        panic_vm->m_sp->m_string = panic_str;
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


        }
        /* else: This VM already aborted, skip. */
    }
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
