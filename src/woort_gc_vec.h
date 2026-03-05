#pragma once

/*
woort_gc_vec.h
*/

#include <stddef.h>
#include <stdint.h>

#include "woort_gc_units.h"
#include "woort_value.h"

typedef struct woort_GCVec
{
    woort_GCUnit    m_gc_unit;
    /* =========================== */
    size_t          m_space;
    size_t          m_length;
    /* OPTIONAL */ woort_Value*    m_datas;

}woort_GCVec;

extern const woort_GCUnitProxy g_gcvec_unit_proxy;

woort_GCVec* woort_GCVec_make_vec(size_t advise_reserving_sz);
