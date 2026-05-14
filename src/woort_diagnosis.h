#pragma once

/*
woort_diagnosis.h
*/

#include "woort.h"

#include <stdarg.h>
#include <stdbool.h>

typedef enum woort_PanicReason
{
    WOORT_PANIC_BAD_BYTE_CODE = 0xD001,
    WOORT_PANIC_STACK_OVERFLOW = 0xD002,
    WOORT_PANIC_CODE_ENV_NOT_FOUND = 0xD003,
    WOORT_PANIC_BAD_CALLSTACK = 0xD004,
    WOORT_PANIC_BAD_TYPE = 0xD005,
    WOORT_PANIC_BAD_VM_REQUEST = 0xD006,
    WOORT_PANIC_ABORTED = 0xD007,
    WOORT_PANIC_INDEX_OUT_OF_RANGE = 0xD008,
    WOORT_PANIC_USER = 0xD009,
    WOORT_PANIC_INTEGER_DIV_FAIL = 0xD00A,
    WOORT_PANIC_OUT_OF_MEMORY = 0xD00B,
} woort_PanicReason;

WOORT_NODISCARD bool woort_vpanic(
    woort_PanicReason reason, const char* msgfmt, va_list va_list);

void woort_panic(
    woort_PanicReason reason, const char* msgfmt, ...);
