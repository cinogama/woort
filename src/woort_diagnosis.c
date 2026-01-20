#include "woort_diagnosis.h"
#include "woort_log.h"

#include <stdarg.h>

void woort_panic(
    woort_PanicReason reason,
    const char* msgfmt,
    ...)
{
    va_list args;
    va_start(args, msgfmt);

    woort_vlog(msgfmt, args);

    va_end(args);

    abort();
}
