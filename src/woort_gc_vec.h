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
    /* OPTIONAL */ woort_DynBox*    
                    m_datas;

}woort_GCVec;

extern const woort_GCUnitProxy g_gcvec_unit_proxy;

woort_GCVec* woort_GCVec_new(size_t advise_reserving_sz);

void woort_GCVec_resize(woort_GCVec* vec, size_t size);
void woort_GCVec_push_back(woort_GCVec* vec, woort_DynBox boxed_value);
