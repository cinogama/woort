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

extern const woort_GCUnitProxy WOORT_GCVEC_UNIT_PROXY;

woort_GCVec* woort_GCVec_new(void);

void woort_GCVec_resize(woort_GCVec* vec, size_t size);
void woort_GCVec_push_back(woort_GCVec* vec, woort_DynBox boxed_value);

woort_DynBox woort_GCVec_get(const woort_GCVec* vec, size_t index);
void woort_GCVec_set(woort_GCVec* vec, size_t index, woort_DynBox boxed_value);
void woort_GCVec_pop_back(woort_GCVec* vec);
void woort_GCVec_insert(woort_GCVec* vec, size_t index, woort_DynBox boxed_value);
void woort_GCVec_erase(woort_GCVec* vec, size_t index);
void woort_GCVec_clear(woort_GCVec* vec);
