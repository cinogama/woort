#pragma once

/*
woort_diagnosis.h
*/

#include "woort.h"

#include <stdarg.h>
#include <stdbool.h>

WOORT_NODISCARD bool woort_raise_panic_v(
    woort_PanicReason reason, 
    const char* location,
    const char* funcname,
    int line, 
    const char* msgfmt, 
    va_list va_list);
