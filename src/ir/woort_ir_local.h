#pragma once

/*
 * woort_ir_local.h
 */

#include "woort_ir_value.h"
#include <stdint.h>

typedef struct woort_IRFunction woort_IRFunction;

typedef struct woort_IRLocal
{
    uint32_t            m_id;
    woort_IRFunction*   m_function;
    woort_IRValue**     m_current_vals;
    uint32_t            m_current_vals_capacity;

} woort_IRLocal;