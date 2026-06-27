#pragma once

/*
woort_gc_map_types.h
*/

#include "woort_value_types.h"

#include <stddef.h>
#include <stdint.h>

typedef struct woort_GCMap_Bucket
{
    uint32_t m_next;
    uint32_t m_prev;

    woort_DynBox m_key;
    woort_DynBox m_val;

} woort_GCMap_Bucket;

struct woort_GCMap
{
    woort_GCUnit        m_gc_unit;
    /* =========================== */
    size_t              m_mask;
    size_t              m_size;
    /* OPTIONAL */ uint32_t* m_entries;
    /* OPTIONAL */ woort_GCMap_Bucket* m_buckets;
};
