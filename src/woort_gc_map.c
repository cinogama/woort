#include <string.h>

#include "woomem.h"
#include "woort_gc.h"
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

        const size_t entry_idx =
            woort_DynBox_hash(this_bucket->m_key) & gcmap->m_mask;

        // 头插法：将当前 bucket 插入到链表头部
        const uint32_t head_idx = gcmap->m_entries[entry_idx];
        this_bucket->m_next = head_idx;
        this_bucket->m_prev = NULL_BUCKET_INDEX;

        if (head_idx != NULL_BUCKET_INDEX)
            gcmap->m_buckets[head_idx].m_prev = (uint32_t)i;

        gcmap->m_entries[entry_idx] = (uint32_t)i;
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

    gcmap->m_entries = (uint32_t*)(gcmap->m_buckets + capacity);
    gcmap->m_mask = capacity - 1;

    _woort_GCMap_rehash(gcmap);
}

woort_GCMap_Bucket* _woort_GCMap_get_writable_bucket_for_key(
    woort_GCMap* gcmap, woort_DynBox key)
{
    // 确保有足够的容量
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    // 计算哈希和入口索引
    const size_t hash = woort_DynBox_hash(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    // 在链中查找已存在的 key
    uint32_t idx = gcmap->m_entries[entry_idx];
    while (idx != NULL_BUCKET_INDEX)
    {
        woort_GCMap_Bucket* bucket = &gcmap->m_buckets[idx];
        if (woort_DynBox_equal(bucket->m_key, key))
        {
            // 混合写屏障(1), 标记被覆盖单元
            woort_GC_barrier_mark_dynbox(bucket->m_val);
            return bucket;  // 找到已存在的 key，返回该 bucket
        }

        idx = bucket->m_next;
    }

    // 未找到，创建新的 bucket
    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* new_bucket = &gcmap->m_buckets[new_idx];
    new_bucket->m_key = key;
    new_bucket->m_next = NULL_BUCKET_INDEX;
    new_bucket->m_prev = NULL_BUCKET_INDEX;

    // 写屏障：确保增量 GC 正确追踪新写入的 key
    woort_GC_barrier_mark_dynbox(key);

    // 将新 bucket 链接到链表头部
    const uint32_t head_idx = gcmap->m_entries[entry_idx];
    if (head_idx == NULL_BUCKET_INDEX)
    {
        // 链表为空，新 bucket 成为头
        gcmap->m_entries[entry_idx] = new_idx;
    }
    else
    {
        // 链接新 bucket 到头部
        new_bucket->m_next = head_idx;
        gcmap->m_buckets[head_idx].m_prev = new_idx;
        gcmap->m_entries[entry_idx] = new_idx;
    }

    ++gcmap->m_size;
    return new_bucket;
}

void woort_GCMap_set(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
    woort_GCMap_Bucket* bucket = _woort_GCMap_get_writable_bucket_for_key(gcmap, key);

    // 混合写屏障(2), 标记插入单元
    woort_GC_barrier_mark_dynbox(val);
    bucket->m_val = val;
}
