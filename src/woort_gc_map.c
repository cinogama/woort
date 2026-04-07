#include <string.h>

#include "woomem.h"
#include "woort_gc.h"
#include "woort_gc_map.h"
#include "woort_gc_string.h"
#include "woort_util.h"

const woort_GCUnitProxy WOORT_GCMAP_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD woort_GCMap* woort_GCMap_new(void)
{
    woort_GCMap* const gcmap = woort_GCUnit_alloc_attrib(
        A, sizeof(woort_GCMap));

    gcmap->m_gc_unit.m_proxy = &WOORT_GCMAP_UNIT_PROXY;
    gcmap->m_entries = NULL;
    gcmap->m_buckets = NULL;
    gcmap->m_mask = 0;
    gcmap->m_size = 0;

    return gcmap;
}

#define NULL_BUCKET_INDEX UINT32_MAX

/* 计算大于等于 n 的最小 2 的幂 */
static size_t _woort_next_power_of_two(size_t n)
{
    if (n == 0)
        return 8;

    size_t power = 8;
    while (power < n)
        power <<= 1;

    return power;
}

/* 查找 key 对应的 bucket 索引，未找到返回 NULL_BUCKET_INDEX */
static uint32_t _woort_GCMap_find_bucket(woort_GCMap* gcmap, woort_DynBox key)
{
    if (gcmap->m_size == 0)
        return NULL_BUCKET_INDEX;

    const size_t hash = woort_DynBox_hash(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    uint32_t idx = gcmap->m_entries[entry_idx];
    while (idx != NULL_BUCKET_INDEX)
    {
        woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];
        if (woort_DynBox_equal(bucket->m_key, key))
            return idx;
        idx = bucket->m_next;
    }

    return NULL_BUCKET_INDEX;
}

void _woort_GCMap_rehash(woort_GCMap* gcmap)
{
    for (size_t i = 0; i <= gcmap->m_mask; ++i)
        gcmap->m_entries[i] = NULL_BUCKET_INDEX;

    /* GCMap 总是使用buckets的前N项来储存 */
    for (size_t i = 0; i < gcmap->m_size; ++i)
    {
        woort_GCMap_Bucket* const this_bucket = &gcmap->m_buckets[i];

        const size_t entry_idx =
            woort_DynBox_hash(this_bucket->m_key) & gcmap->m_mask;

        /* 头插法：将当前 bucket 插入到链表头部 */
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
    /* 计算合适的容量，确保是 2 的幂 */
    const size_t capacity = _woort_next_power_of_two(kv_count);

    /* 如果已有足够的容量，无需重新分配 */
    if (gcmap->m_mask + 1 >= capacity)
        return;

    /* 重新分配 buckets */
    const size_t realloc_size =
        capacity * (sizeof(woort_GCMap_Bucket) + sizeof(uint32_t));

    woort_GC_mixed_write_barrier_gcaddr(
        &gcmap->m_buckets,
        gcmap->m_buckets == NULL
            ? woort_GCUnit_alloc_attrib(A, realloc_size)
            : woomem_realloc(gcmap->m_buckets, realloc_size));

    gcmap->m_entries = (uint32_t*)(gcmap->m_buckets + capacity);
    gcmap->m_mask = capacity - 1;

    _woort_GCMap_rehash(gcmap);
}

woort_GCMap_Bucket* _woort_GCMap_get_writable_bucket_for_key(
    woort_GCMap* gcmap, woort_DynBox key)
{
    /* 查找已存在的 key */
    uint32_t idx = _woort_GCMap_find_bucket(gcmap, key);
    if (idx != NULL_BUCKET_INDEX)
        return &gcmap->m_buckets[idx];

    /* 未找到，创建新的 bucket */
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    const size_t hash = woort_DynBox_hash(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* const new_bucket = &gcmap->m_buckets[new_idx];
    woort_GC_mixed_write_barrier_dynbox(&new_bucket->m_key, key);
    new_bucket->m_next = NULL_BUCKET_INDEX;
    new_bucket->m_prev = NULL_BUCKET_INDEX;

    /* 将新 bucket 链接到链表头部 */
    const uint32_t head_idx = gcmap->m_entries[entry_idx];
    if (head_idx == NULL_BUCKET_INDEX)
    {
        gcmap->m_entries[entry_idx] = new_idx;
    }
    else
    {
        new_bucket->m_next = head_idx;
        gcmap->m_buckets[head_idx].m_prev = new_idx;
        gcmap->m_entries[entry_idx] = new_idx;
    }

    ++gcmap->m_size;
    return new_bucket;
}

void woort_GCMap_set_or_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
    woort_GCMap_Bucket* const bucket =
        _woort_GCMap_get_writable_bucket_for_key(gcmap, key);

    woort_GC_mixed_write_barrier_dynbox(&bucket->m_val, val);
}

WOORT_NODISCARD bool woort_GCMap_erase(woort_GCMap* gcmap, woort_DynBox key)
{
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key);
    if (idx == NULL_BUCKET_INDEX)
        return false;

    woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];

    /* 删除屏障：标记被删除的 key 和 val */
    woort_GC_delete_barrier_dynbox(bucket->m_key);
    woort_GC_delete_barrier_dynbox(bucket->m_val);

    /* 计算 entry_idx（erase 需要它来更新链表头） */
    const size_t entry_idx = woort_DynBox_hash(key) & gcmap->m_mask;

    /* 从链表中移除该 bucket */
    const uint32_t prev_idx = bucket->m_prev;
    const uint32_t next_idx = bucket->m_next;

    if (prev_idx != NULL_BUCKET_INDEX)
        gcmap->m_buckets[prev_idx].m_next = next_idx;
    else
        gcmap->m_entries[entry_idx] = next_idx;

    if (next_idx != NULL_BUCKET_INDEX)
        gcmap->m_buckets[next_idx].m_prev = prev_idx;

    /* 如果不是最后一个 bucket，将最后一个 bucket 移动到被删除的位置 */
    const uint32_t last_idx = (uint32_t)(gcmap->m_size - 1);
    if (idx != last_idx)
    {
        woort_GCMap_Bucket* const last_bucket = &gcmap->m_buckets[last_idx];
        const uint32_t last_prev = last_bucket->m_prev;
        const uint32_t last_next = last_bucket->m_next;

        /* 通过写屏障复制 key 和 val */
        woort_GC_mixed_write_barrier_dynbox(&bucket->m_key, last_bucket->m_key);
        woort_GC_mixed_write_barrier_dynbox(&bucket->m_val, last_bucket->m_val);

        /* 更新最后一个 bucket 的邻居指针 */
        if (last_prev != NULL_BUCKET_INDEX)
            gcmap->m_buckets[last_prev].m_next = idx;
        else
        {
            const size_t last_entry_idx = woort_DynBox_hash(last_bucket->m_key) & gcmap->m_mask;
            gcmap->m_entries[last_entry_idx] = idx;
        }

        if (last_next != NULL_BUCKET_INDEX)
            gcmap->m_buckets[last_next].m_prev = idx;
    }

    --gcmap->m_size;
    return true;
}

WOORT_NODISCARD bool woort_GCMap_get(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox* out_val)
{
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key);
    if (idx == NULL_BUCKET_INDEX)
        return false;

    if (out_val != NULL)
        *out_val = gcmap->m_buckets[idx].m_val;
    return true;
}

WOORT_NODISCARD bool woort_GCMap_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
    /* 检查 key 是否已存在 */
    if (_woort_GCMap_find_bucket(gcmap, key) != NULL_BUCKET_INDEX)
        return false;

    woort_GCMap_Bucket* const bucket =
        _woort_GCMap_get_writable_bucket_for_key(gcmap, key);
    woort_GC_mixed_write_barrier_dynbox(&bucket->m_val, val);
    return true;
}

WOORT_NODISCARD bool woort_GCMap_set(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key);
    if (idx == NULL_BUCKET_INDEX)
        return false;

    woort_GC_mixed_write_barrier_dynbox(&gcmap->m_buckets[idx].m_val, val);
    return true;
}

////////////////////////////////////////////////////////////////////////
// 类型特化的查找函数（返回指针）：用于原地修改
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_int(
    woort_GCMap* gcmap, woort_Int key)
{
    if (gcmap->m_size == 0)
        return NULL;

    const size_t hash = _woort_hash_int(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    uint32_t idx = gcmap->m_entries[entry_idx];
    while (idx != NULL_BUCKET_INDEX)
    {
        woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];
        if (woort_DynBox_equal_int(bucket->m_key, key))
            return &bucket->m_val;
        idx = bucket->m_next;
    }

    return NULL;
}

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_real(
    woort_GCMap* gcmap, woort_Real key)
{
    if (gcmap->m_size == 0)
        return NULL;

    const size_t hash = _woort_hash_real(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    uint32_t idx = gcmap->m_entries[entry_idx];
    while (idx != NULL_BUCKET_INDEX)
    {
        woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];
        if (woort_DynBox_equal_real(bucket->m_key, key))
            return &bucket->m_val;
        idx = bucket->m_next;
    }

    return NULL;
}

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_bool(
    woort_GCMap* gcmap, bool key)
{
    if (gcmap->m_size == 0)
        return NULL;

    const size_t hash = key ? 1 : 0;
    const size_t entry_idx = hash & gcmap->m_mask;

    uint32_t idx = gcmap->m_entries[entry_idx];
    while (idx != NULL_BUCKET_INDEX)
    {
        woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];
        if (woort_DynBox_equal_bool(bucket->m_key, key))
            return &bucket->m_val;
        idx = bucket->m_next;
    }

    return NULL;
}

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_dynbox(
    woort_GCMap* gcmap, woort_DynBox key)
{
    if (gcmap->m_size == 0)
        return NULL;

    const size_t hash = woort_DynBox_hash(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    uint32_t idx = gcmap->m_entries[entry_idx];
    while (idx != NULL_BUCKET_INDEX)
    {
        woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];
        if (woort_DynBox_equal(bucket->m_key, key))
            return &bucket->m_val;
        idx = bucket->m_next;
    }

    return NULL;
}

////////////////////////////////////////////////////////////////////////
// 类型特化的查找或创建函数（返回指针）：用于原地修改，不存在则创建
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_int(
    woort_GCMap* gcmap, woort_Int key)
{
    /* 先尝试查找已存在的 key */
    woort_DynBox* const existing = woort_GCMap_get_bucket_val_by_int(gcmap, key);
    if (existing != NULL)
        return existing;

    /* 未找到，创建新的 bucket */
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    const size_t hash = _woort_hash_int(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* const new_bucket = &gcmap->m_buckets[new_idx];
    woort_DynBox_box_int_with_barrier(&new_bucket->m_key, key);
    new_bucket->m_next = NULL_BUCKET_INDEX;
    new_bucket->m_prev = NULL_BUCKET_INDEX;

    /* 将新 bucket 链接到链表头部 */
    const uint32_t head_idx = gcmap->m_entries[entry_idx];
    if (head_idx == NULL_BUCKET_INDEX)
    {
        gcmap->m_entries[entry_idx] = new_idx;
    }
    else
    {
        new_bucket->m_next = head_idx;
        gcmap->m_buckets[head_idx].m_prev = new_idx;
        gcmap->m_entries[entry_idx] = new_idx;
    }

    ++gcmap->m_size;
    return &new_bucket->m_val;
}

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_real(
    woort_GCMap* gcmap, woort_Real key)
{
    /* 先尝试查找已存在的 key */
    woort_DynBox* const existing = woort_GCMap_get_bucket_val_by_real(gcmap, key);
    if (existing != NULL)
        return existing;

    /* 未找到，创建新的 bucket */
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    const size_t hash = _woort_hash_real(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* const new_bucket = &gcmap->m_buckets[new_idx];
    woort_DynBox_box_real_with_barrier(&new_bucket->m_key, key);
    new_bucket->m_next = NULL_BUCKET_INDEX;
    new_bucket->m_prev = NULL_BUCKET_INDEX;

    /* 将新 bucket 链接到链表头部 */
    const uint32_t head_idx = gcmap->m_entries[entry_idx];
    if (head_idx == NULL_BUCKET_INDEX)
    {
        gcmap->m_entries[entry_idx] = new_idx;
    }
    else
    {
        new_bucket->m_next = head_idx;
        gcmap->m_buckets[head_idx].m_prev = new_idx;
        gcmap->m_entries[entry_idx] = new_idx;
    }

    ++gcmap->m_size;
    return &new_bucket->m_val;
}

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_bool(
    woort_GCMap* gcmap, bool key)
{
    /* 先尝试查找已存在的 key */
    woort_DynBox* const existing = woort_GCMap_get_bucket_val_by_bool(gcmap, key);
    if (existing != NULL)
        return existing;

    /* 未找到，创建新的 bucket */
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    const size_t hash = key ? 1 : 0;
    const size_t entry_idx = hash & gcmap->m_mask;

    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* const new_bucket = &gcmap->m_buckets[new_idx];
    woort_DynBox_box_bool_with_barrier(&new_bucket->m_key, key);
    new_bucket->m_next = NULL_BUCKET_INDEX;
    new_bucket->m_prev = NULL_BUCKET_INDEX;

    /* 将新 bucket 链接到链表头部 */
    const uint32_t head_idx = gcmap->m_entries[entry_idx];
    if (head_idx == NULL_BUCKET_INDEX)
    {
        gcmap->m_entries[entry_idx] = new_idx;
    }
    else
    {
        new_bucket->m_next = head_idx;
        gcmap->m_buckets[head_idx].m_prev = new_idx;
        gcmap->m_entries[entry_idx] = new_idx;
    }

    ++gcmap->m_size;
    return &new_bucket->m_val;
}

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_dynbox(
    woort_GCMap* gcmap, woort_DynBox key)
{
    /* 先尝试查找已存在的 key */
    woort_DynBox* const existing = woort_GCMap_get_bucket_val_by_dynbox(gcmap, key);
    if (existing != NULL)
        return existing;

    /* 未找到，创建新的 bucket */
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    const size_t hash = woort_DynBox_hash(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* const new_bucket = &gcmap->m_buckets[new_idx];
    woort_GC_mixed_write_barrier_dynbox(&new_bucket->m_key, key);
    new_bucket->m_next = NULL_BUCKET_INDEX;
    new_bucket->m_prev = NULL_BUCKET_INDEX;

    /* 将新 bucket 链接到链表头部 */
    const uint32_t head_idx = gcmap->m_entries[entry_idx];
    if (head_idx == NULL_BUCKET_INDEX)
    {
        gcmap->m_entries[entry_idx] = new_idx;
    }
    else
    {
        new_bucket->m_next = head_idx;
        gcmap->m_buckets[head_idx].m_prev = new_idx;
        gcmap->m_entries[entry_idx] = new_idx;
    }

    ++gcmap->m_size;
    return &new_bucket->m_val;
}

////////////////////////////////////////////////////////////////////////
// 类型特化的查找函数：string
////////////////////////////////////////////////////////////////////////

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_bucket_val_by_string(
    woort_GCMap* gcmap, const char* key, size_t len)
{
    if (gcmap->m_size == 0)
        return NULL;

    const size_t hash = woort_hash_string(key, len);
    const size_t entry_idx = hash & gcmap->m_mask;

    uint32_t idx = gcmap->m_entries[entry_idx];
    while (idx != NULL_BUCKET_INDEX)
    {
        woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];
        if (woort_DynBox_equal_string(bucket->m_key, key, len))
            return &bucket->m_val;
        idx = bucket->m_next;
    }

    return NULL;
}

WOORT_NODISCARD /* OPTIONAL */ woort_DynBox* woort_GCMap_get_or_create_bucket_val_by_string(
    woort_GCMap* gcmap, const char* key, size_t len)
{
    woort_DynBox* const existing = woort_GCMap_get_bucket_val_by_string(gcmap, key, len);
    if (existing != NULL)
        return existing;

    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    const size_t hash = woort_hash_string(key, len);
    const size_t entry_idx = hash & gcmap->m_mask;

    const woort_GCString* const str = woort_GCString_make_string(key, len);

    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* const new_bucket = &gcmap->m_buckets[new_idx];
    {
        woort_DynBox boxed;
        boxed.m_boxed_gc_unit = (woort_GCUnit*)str;
        woort_GC_mixed_write_barrier_dynbox(&new_bucket->m_key, boxed);
    }
    new_bucket->m_next = NULL_BUCKET_INDEX;
    new_bucket->m_prev = NULL_BUCKET_INDEX;

    const uint32_t head_idx = gcmap->m_entries[entry_idx];
    if (head_idx == NULL_BUCKET_INDEX)
    {
        gcmap->m_entries[entry_idx] = new_idx;
    }
    else
    {
        new_bucket->m_next = head_idx;
        gcmap->m_buckets[head_idx].m_prev = new_idx;
        gcmap->m_entries[entry_idx] = new_idx;
    }

    ++gcmap->m_size;
    return &new_bucket->m_val;
}
