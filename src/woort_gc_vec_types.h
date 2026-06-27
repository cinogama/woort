#pragma once

/*
woort_gc_vec_types.h
*/

#include "woort_value_types.h"

#include <stddef.h>

typedef struct woort_GCVec
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */
    size_t          m_space;
    size_t          m_length;
    /* OPTIONAL */ woort_DynBox*
                    m_datas;

}woort_GCVec;
