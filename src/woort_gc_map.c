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
    gcmap->m_entries = NULL;
    gcmap->m_mask = 0;
    gcmap->m_size = 0;

    return gcmap;
}

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

void woort_GCMap_reserve(woort_GCMap* gcmap, size_t kv_count)
{
    // 计算合适的容量，确保是 2 的幂
    const size_t capacity = _woort_next_power_of_two(kv_count);

    // 如果已有足够的容量，无需重新分配
    if (gcmap->m_mask + 1 >= capacity)
        return;

    // 分配两个连续的数组：Next table 和 Buckets table
    // Next table: capacity * sizeof(woort_GCMap_Bucket_Index)
    // Buckets table: capacity * sizeof(woort_GCMap_Bucket)
    const size_t next_table_size = capacity * sizeof(woort_GCMap_Bucket_Index);
    const size_t buckets_table_size = capacity * sizeof(woort_GCMap_Bucket);
    const size_t total_size = next_table_size + buckets_table_size;

    woort_GCMap_Bucket_Index* const new_bucket_index =
        woort_GCUnit_alloc_attrib(A, total_size);
    woort_GCMap_Bucket* const new_bucket_entry =
        new_bucket_index + capacity;

    for (size_t i = 0; i < capacity; ++i)
    {
        new_bucket_index[i].m_next_index = UINT32_MAX;
        new_bucket_index[i].m_table_index = UINT32_MAX;
    }
    TODO;
    gcmap->m_entries = new_entries;
    gcmap->m_mask = capacity - 1;
}