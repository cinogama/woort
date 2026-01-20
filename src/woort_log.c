#include "woort_log.h"

#include <stdarg.h>
#include <stdio.h>

void woort_vlog(const char* format, va_list va)
{
    // Write to stderr.
    vfprintf(stderr, format, va);
}

void woort_log(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    woort_vlog(format, args);

    va_end(args);
}