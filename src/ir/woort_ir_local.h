#pragma once

/*
 * woort_ir_local.h
 */

#include "woort_ir_value.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct woort_IRFunction woort_IRFunction;

typedef struct woort_IRLocalValueEntry
{
    uint32_t            m_block_id;
    woort_IRValue*      m_value;
    bool                m_is_set;

} woort_IRLocalValueEntry;

typedef struct woort_IRLocal
{
    uint32_t                    m_id;
    woort_IRFunction*           m_function;
    woort_IRLocalValueEntry*    m_values;
    uint32_t                    m_values_count;
    uint32_t                    m_values_capacity;

} woort_IRLocal;