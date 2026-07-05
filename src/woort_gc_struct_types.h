#pragma once

/*
woort_gc_struct_types.h
*/

#include "woort_value_types.h"

#include <stddef.h>

struct woort_GCStruct
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */

    size_t          m_size;
    woort_Value     m_datas[];

};
