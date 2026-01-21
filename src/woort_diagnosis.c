#include "woort_diagnosis.h"
#include "woort_log.h"

#include <stdarg.h>
#include <stdlib.h>

void woort_panic(
    woort_PanicReason reason,
    const char* msgfmt,
    ...)
{
    woort_log(
        "WooRT Panic: Fatal runtime error(%X). "
        "Program execution terminated:\n    ", reason);

    va_list args;
    va_start(args, msgfmt);

    woort_vlog(msgfmt, args);

    va_end(args);

    // TODO;
    abort();
}
