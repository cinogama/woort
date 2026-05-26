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

WOORT_NODISCARD void _woort_GCVec_extern(woort_GCVec* vec, size_t size);
void woort_GCVec_resize_without_init(woort_GCVec* vec, size_t size);
void woort_GCVec_resize_with(woort_GCVec* vec, size_t size, woort_DynBox init_val);
WOORT_NODISCARD bool woort_GCVec_shrink(woort_GCVec* vec, size_t new_size);
void woort_GCVec_push_back(woort_GCVec* vec, woort_DynBox boxed_value);
WOORT_NODISCARD woort_DynBox* woort_GCVec_emplace_back(woort_GCVec* vec, size_t count);

WOORT_NODISCARD bool woort_GCVec_get(const woort_GCVec* vec, size_t index, woort_DynBox* out_boxval);
WOORT_NODISCARD bool woort_GCVec_set(woort_GCVec* vec, size_t index, woort_DynBox boxed_value);
WOORT_NODISCARD bool woort_GCVec_pop_back(woort_GCVec* vec);
WOORT_NODISCARD bool woort_GCVec_insert(woort_GCVec* vec, size_t index, woort_DynBox boxed_value);
WOORT_NODISCARD bool woort_GCVec_erase(woort_GCVec* vec, size_t index);
void woort_GCVec_clear(woort_GCVec* vec);

void woort_GCVec_copy(woort_GCVec* dst, const woort_GCVec* src);
void woort_GCVec_swap(woort_GCVec* a, woort_GCVec* b);
