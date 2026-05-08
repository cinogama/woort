#include "woort_diagnosis.h"
#include "woort_log.h"
#include "woort_vm.h"

#include <stdarg.h>
#include <stdlib.h>

void woort_vpanic(
    woort_PanicReason reason,
    const char* msgfmt,
    va_list args)
{
    woort_VMRuntime* const panic_vm = woort_VMRuntime_swap(NULL);
    {
        if (woort_VMRuntime_request_set(panic_vm, WOORT_VMRUNTIME_CHECK_REQUEST_ABORT))
        {
            woort_log(
                "WooRT Panic: Fatal runtime error(%X). "
                "Program execution terminated:\n    ", reason);

            woort_vlog(msgfmt, args);
        }
        /* else: This VM already aborted, skip. */
    }
    (void)woort_VMRuntime_swap(panic_vm);
}

void woort_panic(
    woort_PanicReason reason,
    const char* msgfmt,
    ...)
{
    va_list args;
    va_start(args, msgfmt);

    woort_vpanic(reason, msgfmt, args);

    va_end(args);
}

WOORT_NODISCARD woort_VmCallStatus woort_ret_panic(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    woort_vpanic(WOORT_PANIC_ABORTED, fmt, args);

    va_end(args);

    return WOORT_VM_CALL_STATUS_RESYNC;
}

WOORT_NODISCARD woort_VmCallStatus woort_ret_yield(void)
{
    woort_VMRuntime* const vm = WOORT_t_this_thread_vm;
    if (vm != NULL)
    {
        (void)woort_VMRuntime_request_set(
            vm,
            WOORT_VMRUNTIME_CHECK_REQUEST_YIELD);
    }
    return WOORT_VM_CALL_STATUS_RESYNC;
}
