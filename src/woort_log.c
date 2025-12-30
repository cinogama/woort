#include "woort_log.h"

#include <stdarg.h>
#include <stdio.h>

void woort_log(const char* format, ...)
{
    // Write to stderr.

    va_list args;
    va_start(args, format);

    vfprintf(stderr, format, args);

    va_end(args);
}