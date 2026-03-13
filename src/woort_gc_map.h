#pragma once

/*
woort_gc_map.h
*/

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "woort_gc_units.h"
#include "woort_value.h"

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

extern const woort_GCUnitProxy g_gcmap_unit_proxy;

WOORT_NODISCARD woort_GCMap* woort_GCMap_new(void);

void woort_GCMap_reserve(woort_GCMap* gcmap, size_t kv_count);
void woort_GCMap_set_or_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val);
WOORT_NODISCARD bool woort_GCMap_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val);
WOORT_NODISCARD bool woort_GCMap_erase(woort_GCMap* gcmap, woort_DynBox key);
WOORT_NODISCARD bool woort_GCMap_get(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox* out_val);
WOORT_NODISCARD bool woort_GCMap_set(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val);

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_int(
    woort_GCMap* gcmap, woort_Int key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_real(
    woort_GCMap* gcmap, woort_Real key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_bool(
    woort_GCMap* gcmap, bool key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_dynbox(
    woort_GCMap* gcmap, woort_DynBox key);
 
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_int(
    woort_GCMap* gcmap, woort_Int key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_real(
    woort_GCMap* gcmap, woort_Real key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_bool(
    woort_GCMap* gcmap, bool key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_dynbox(
    woort_GCMap* gcmap, woort_DynBox key);
