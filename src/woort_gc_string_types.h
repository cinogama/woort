#pragma once

/*
woort_gc_string_types.h
*/

#include "woort_value_types.h"

#include <stddef.h>

struct woort_GCString
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */
    size_t          m_length;
    char            m_content[];
};
