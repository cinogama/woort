#pragma once

/*
woort_gc_string.h
*/

#include <stddef.h>
#include <stdint.h>

#include "woort_gc_units.h"
#include "woort_value.h"

struct woort_GCString
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */
    size_t          m_length;
    char            m_content[];
};

extern const woort_GCUnitProxy g_gcstring_unit_proxy;

WOORT_NODISCARD const woort_GCString* woort_GCString_make_string(const char* str, size_t len);
WOORT_NODISCARD const woort_GCString* woort_GCString_add_string(const woort_GCString* a, const woort_GCString* b);

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
