#pragma once

/*
woort_diagnosis.h
*/

#include "woort.h"

#include <stdarg.h>
#include <stdbool.h>

WOORT_NODISCARD bool woort_raise_panic_v(
    woort_PanicReason reason, 
    const char* funcname,
    const char* location,
    int line, 
    const char* msgfmt, 
    va_list va_list);
