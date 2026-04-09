#include "woort_diagnosis.h"
#include "woort_log.h"

#include <stdarg.h>
#include <stdlib.h>

void woort_vpanic(
    woort_PanicReason reason,
    const char* msgfmt,
    va_list args)
{
    woort_log(
        "WooRT Panic: Fatal runtime error(%X). "
        "Program execution terminated:\n    ", reason);

    woort_vlog(msgfmt, args);

    /* TODO */
    abort();
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
