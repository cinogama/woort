#pragma once

/*
woort_gc_string.h
*/

#include "woort_gc_string_types.h"

#include "woort_gc_units.h"
#include "woort_value.h"

#include <stddef.h>
#include <stdarg.h>

extern const woort_GCUnitProxy WOORT_GCSTRING_UNIT_PROXY;

WOORT_NODISCARD const woort_GCString* woort_GCString_make_string_for_env_constant(
    woort_CodeEnv* cenv, const char* str, size_t len);
WOORT_NODISCARD const woort_GCString* woort_GCString_make_string(
    const char* str, size_t len);
WOORT_NODISCARD const woort_GCString* woort_GCString_make_format_va(
    const char* fmt, va_list args);
WOORT_NODISCARD const woort_GCString* woort_GCString_add_string(
    const woort_GCString* a, const woort_GCString* b);

/**
 * Compare two GC strings.
 * @return <0 if a < b, 0 if a == b, >0 if a > b
 */
WOORT_NODISCARD int woort_GCString_compare(const woort_GCString* a, const woort_GCString* b);

WOORT_NODISCARD size_t woort_GCString_hash(const woort_GCString* str);

/**
 * Convert an integer to a GC string.
 */
WOORT_NODISCARD const woort_GCString* woort_GCString_from_integer(woort_Int value);

/**
 * Convert a real (double) to a GC string.
 */
WOORT_NODISCARD const woort_GCString* woort_GCString_from_real(woort_Real value);

/**
 * Convert a GC string to an integer.
 * @param str The GC string to convert.
 * @return converted integer.
 */
WOORT_NODISCARD woort_Int woort_GCString_to_integer(const woort_GCString* str);

/**
 * Convert a GC string to a real (double).
 * @param str The GC string to convert.
 * @return converted real
 */
WOORT_NODISCARD woort_Real woort_GCString_to_real(const woort_GCString* str);
