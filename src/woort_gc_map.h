#pragma once

/*
woort_gc_map.h
*/

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "woort_gc_units.h"
#include "woort_value.h"

typedef size_t woort_GCMap_Bucket_Index;
typedef struct woort_GCMap_Bucket
{
    woort_DynBox m_key;
    woort_DynBox m_val;

} woort_GCMap_Bucket;

_Static_assert(
    _Alignof(woort_GCMap_Bucket) == _Alignof(woort_GCMap_Bucket_Index),
    "As we are allocating one contiguous space for both woort_GCMap_Bucket "
    "and woort_GCMap_Bucket_Index, they must share the same alignment requirements.");

/*
    Next table: 
        [woort_GCMap_Bucket_Index 0 1 2....]
    m_entries-> point to:
    Buckets table:  
        [woort_GCMap_Bucket 0 1 2 ...]
*/

struct woort_GCMap
{
    woort_GCUnit        m_gc_unit;
    /* =========================== */
    /* OPTIONAL */ woort_GCMap_Bucket* m_entries;
    size_t              m_mask;
    size_t              m_size;
};

extern const woort_GCUnitProxy g_gcmap_unit_proxy;

WOORT_NODISCARD woort_GCMap* woort_GCMap_new(void);
void woort_GCMap_reserve(woort_GCMap* gcmap, size_t kv_count);
