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

extern const woort_GCUnitProxy WOORT_GCMAP_UNIT_PROXY;

WOORT_NODISCARD woort_GCMap* woort_GCMap_new(void);

void woort_GCMap_reserve(woort_GCMap* gcmap, size_t kv_count);
void woort_GCMap_set_or_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val);
WOORT_NODISCARD bool woort_GCMap_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val);
void woort_GCMap_clear(woort_GCMap* gcmap);
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
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_string(
    woort_GCMap* gcmap, const char* key, size_t len);
 
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_int(
    woort_GCMap* gcmap, woort_Int key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_real(
    woort_GCMap* gcmap, woort_Real key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_bool(
    woort_GCMap* gcmap, bool key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_dynbox(
    woort_GCMap* gcmap, woort_DynBox key);
WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_string(
    woort_GCMap* gcmap, const char* key, size_t len);

WOORT_NODISCARD bool woort_GCMap_get_key_value_by_index(
    const woort_GCMap* gcmap,
    size_t index,
    /* OPTIONAL */ woort_DynBox* out_key,
    /* OPTIONAL */ woort_DynBox* out_val);

/*
Emplace API: 将新键值对直接写入 bucket（先入桶再入链）。

  woort_GCMap_Bucket* bucket = woort_GCMap_emplace_prepare(gcmap);
  bucket->m_key = ...;   // 直接写入
  bucket->m_val = ...;   // 直接写入
  woort_GCMap_emplace_commit(gcmap);  // 入桶
*/
WOORT_NODISCARD woort_GCMap_Bucket* woort_GCMap_emplace_prepare(woort_GCMap* gcmap);
void woort_GCMap_emplace_commit(woort_GCMap* gcmap);
