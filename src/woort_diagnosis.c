#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_vm.h"
#include "woort_gc_string.h"

#include <stdarg.h>
#include <stdlib.h>

WOORT_NODISCARD bool woort_vpanic(
    woort_PanicReason reason,
    const char* msgfmt,
    va_list args)
{
    bool vm_has_been_aborted = false;
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
                vm_has_been_aborted = true;

                va_list args_copy;
                va_copy(args_copy, args);
                const woort_GCString* panic_str =
                    woort_GCString_make_format_va(msgfmt, args_copy);
                va_end(args_copy);

                panic_vm->m_sp->m_string = panic_str;
            }

            woort_vlog(msgfmt, args);

            woort_log("\n");
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
