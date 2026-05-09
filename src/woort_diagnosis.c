#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_vm.h"
#include "woort_gc_string.h"

#include <stdarg.h>
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
    if (a->m_is_fuzzy != b->m_is_fuzzy
        || a->m_has_location != b->m_has_location
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

            if (panic_vm != NULL)
            {
                va_list args_copy;
                va_copy(args_copy, args);
                const woort_GCString* panic_str =
                    woort_GCString_make_format_va(msgfmt, args_copy);
                va_end(args_copy);

                panic_vm->m_sp->m_string = panic_str;
            }

            woort_vlog(msgfmt, args);

            woort_log("\nTrace:\n");

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
