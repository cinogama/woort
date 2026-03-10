#include <string.h>

#include "woomem.h"
#include "woort_gc_map.h"

const woort_GCUnitProxy g_gcmap_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD woort_GCMap* woort_GCMap_new(void)
{
    woort_GCMap* const gcmap = woort_GCUnit_alloc_attrib(
        A, sizeof(woort_GCMap));

    gcmap->m_gc_unit.m_proxy = &g_gcmap_unit_proxy;
    gcmap->m_buckets = NULL;
    gcmap->m_mask = 0;
    gcmap->m_size = 0;

    return gcmap;
}

#define NULL_BUCKET_INDEX UINT32_MAX

// 计算大于等于 n 的最小 2 的幂
static size_t _woort_next_power_of_two(size_t n)
{
    if (n == 0)
        return 8;

    size_t power = 8;
    while (power < n)
        power <<= 1;

    return power;
}

void _woort_GCMap_rehash(woort_GCMap* gcmap)
{
    for (size_t i = 0; i <= gcmap->m_mask; ++i)
        gcmap->m_entries[i] = NULL_BUCKET_INDEX;

    // GCMap 总是使用buckets的前N项来储存
    for (size_t i = 0; i < gcmap->m_size; ++i)
    {
        woort_GCMap_Bucket* const this_bucket = &gcmap->m_buckets[i];
        this_bucket->m_next = NULL_BUCKET_INDEX;

        const size_t entry_idx = 
            woort_DynBox_hash(this_bucket->m_key) & gcmap->m_mask;

        uint32_t idx = gcmap->m_entries[entry_idx];

        if (idx == NULL_BUCKET_INDEX)
            gcmap->m_entries[entry_idx] = i;
        else
        {
            woort_GCMap_Bucket* prev_bucket = &gcmap->m_buckets[idx];
            while (prev_bucket->m_next != NULL_BUCKET_INDEX)
            {
                idx = prev_bucket->m_next;
                prev_bucket = &gcmap->m_buckets[idx];
            }

            prev_bucket->m_next = i;
        }
        gcmap->m_buckets[i].m_prev = idx;
    }
}

void woort_GCMap_reserve(woort_GCMap* gcmap, size_t kv_count)
{
    // 计算合适的容量，确保是 2 的幂
    const size_t capacity = _woort_next_power_of_two(kv_count);

    // 如果已有足够的容量，无需重新分配
    if (gcmap->m_mask + 1 >= capacity)
        return;

    // 重新分配 buckets
    const size_t realloc_size =
        capacity * (sizeof(woort_GCMap_Bucket) + sizeof(uint32_t));

    gcmap->m_buckets = gcmap->m_buckets == NULL 
        ? woort_GCUnit_alloc_attrib(A, realloc_size)
        : woomem_realloc(gcmap->m_buckets, realloc_size);

    gcmap->m_entries = gcmap->m_buckets + capacity;
    gcmap->m_mask = capacity - 1;

    _woort_GCMap_rehash(gcmap);
}

woort_GCMap_Bucket* _woort_GCMap_get_writable_bucket_for_key(
    woort_GCMap* gcmap, woort_DynBox key)
{

}

void woort_GCMap_set(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
}