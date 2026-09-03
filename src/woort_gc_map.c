#include "woort.h"

#include "woort_gc.h"
#include "woort_gc_map.h"
#include "woort_gc_string.h"
#include "woort_util.h"

#include "woort_mem.h"

#include <string.h>

const woort_GCUnitProxy WOORT_GCMAP_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD woort_GCMap* woort_GCMap_new(void)
{
    woort_GCMap* const gcmap = woort_GCUnit_alloc_delay_init(
        sizeof(woort_GCMap));

    gcmap->m_gc_unit.m_proxy = &WOORT_GCMAP_UNIT_PROXY;
    gcmap->m_entries = NULL;
    gcmap->m_buckets = NULL;
    gcmap->m_mask = 0;
    gcmap->m_size = 0;

    woort_GCUnit_init_delay_alloc(A, gcmap);

    return gcmap;
}

#define NULL_BUCKET_INDEX UINT32_MAX

/* 计算大于等于 n 的最小 2 的幂（下限为 8） */
static size_t _woort_next_power_of_two(size_t n)
{
    if (n <= 8)
        return 8;

    /* 位运算：将最高位以下的位全部置 1，再加 1 得到下一个 2 的幂 */
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if SIZE_MAX >> 32 != 0
    n |= n >> 32;
#endif
    return n + 1;
}

/* 查找 key 对应的 bucket 索引，未找到返回 NULL_BUCKET_INDEX。
   若 out_entry_idx 非 NULL，写入 key 对应的 entry 槽位索引。 */
static uint32_t _woort_GCMap_find_bucket(
    woort_GCMap* gcmap,
    woort_DynBox key,
    /* OPTIONAL */ size_t* out_entry_idx)
{
    if (gcmap->m_size == 0)
        return NULL_BUCKET_INDEX;

    const size_t hash = woort_DynBox_hash(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    if (out_entry_idx != NULL)
        *out_entry_idx = entry_idx;

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

/* 将 bucket_idx 处的桶头插到 entry_idx 对应冲突链的头部。
   调用前无需初始化 bucket 的 m_next/m_prev，本函数会完整设置。 */
static void _woort_GCMap_link_bucket_to_head(
    woort_GCMap* gcmap, uint32_t bucket_idx, size_t entry_idx)
{
    woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[bucket_idx];
    const uint32_t head_idx = gcmap->m_entries[entry_idx];

    bucket->m_next = head_idx;
    bucket->m_prev = NULL_BUCKET_INDEX;

    if (head_idx != NULL_BUCKET_INDEX)
        gcmap->m_buckets[head_idx].m_prev = bucket_idx;

    gcmap->m_entries[entry_idx] = bucket_idx;
}

static void _woort_GCMap_rehash(woort_GCMap* gcmap)
{
    /* NULL_BUCKET_INDEX == 0xFFFFFFFF，可用 memset 按字节填充 0xFF */
    memset(gcmap->m_entries, 0xFF, (gcmap->m_mask + 1) * sizeof(uint32_t));

    /* GCMap 总是使用 buckets 的前 N 项来储存 */
    for (size_t i = 0; i < gcmap->m_size; ++i)
    {
        const size_t entry_idx =
            woort_DynBox_hash(gcmap->m_buckets[i].m_key) & gcmap->m_mask;
        _woort_GCMap_link_bucket_to_head(gcmap, (uint32_t)i, entry_idx);
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

    void* new_buckets;
    if (gcmap->m_buckets == NULL)
    {
        new_buckets = woort_GCUnit_alloc_delay_init(realloc_size);
        woort_GCUnit_init_delay_alloc(A, new_buckets);
    }
    else
        new_buckets = woort_GCUnit_realloc(gcmap->m_buckets, realloc_size);

    woort_GC_mixed_write_barrier_gcunit(
        (void**)&gcmap->m_buckets, new_buckets);

    gcmap->m_entries = (uint32_t*)(gcmap->m_buckets + capacity);
    gcmap->m_mask = capacity - 1;

    _woort_GCMap_rehash(gcmap);
}

/* 假定 key 不存在于 map 中，创建新 bucket（dynbox key + init 写屏障）
   并链接到冲突链头部。调用者负责后续写入 m_val。 */
static woort_GCMap_Bucket* _woort_GCMap_create_bucket(
    woort_GCMap* gcmap, woort_DynBox key)
{
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    const size_t hash = woort_DynBox_hash(key);
    const size_t entry_idx = hash & gcmap->m_mask;

    const uint32_t new_idx = (uint32_t)gcmap->m_size;
    woort_GCMap_Bucket* const new_bucket = &gcmap->m_buckets[new_idx];

    /* 初始化 m_val 为 nil，避免后续 mixed 写屏障标记脏的旧值 */
    new_bucket->m_val.m_boxed = 0;
    woort_GC_init_write_barrier_dynbox(&new_bucket->m_key, key);

    _woort_GCMap_link_bucket_to_head(gcmap, new_idx, entry_idx);

    ++gcmap->m_size;
    return new_bucket;
}

static woort_GCMap_Bucket* _woort_GCMap_get_writable_bucket_for_key(
    woort_GCMap* gcmap, woort_DynBox key)
{
    /* 查找已存在的 key */
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key, NULL);
    if (idx != NULL_BUCKET_INDEX)
        return &gcmap->m_buckets[idx];

    /* 未找到，创建新的 bucket */
    return _woort_GCMap_create_bucket(gcmap, key);
}

void woort_GCMap_set_or_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
    woort_GCMap_Bucket* const bucket =
        _woort_GCMap_get_writable_bucket_for_key(gcmap, key);

    woort_GC_mixed_write_barrier_dynbox(&bucket->m_val, val);
}

void woort_GCMap_clear(woort_GCMap* gcmap)
{
    if (gcmap->m_size == 0)
        return;

    /* 删除屏障：标记所有被清除的 key 和 val */
    for (size_t i = 0; i < gcmap->m_size; ++i)
    {
        woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[i];
        woort_GC_delete_barrier_dynbox(bucket->m_key);
        woort_GC_delete_barrier_dynbox(bucket->m_val);

        bucket->m_key.m_boxed = 0;
        bucket->m_val.m_boxed = 0;
    }

    /* 重置所有 entry 槽位 */
    memset(gcmap->m_entries, 0xFF, (gcmap->m_mask + 1) * sizeof(uint32_t));

    gcmap->m_size = 0;
}

WOORT_NODISCARD bool woort_GCMap_erase(woort_GCMap* gcmap, woort_DynBox key)
{
    /* find_bucket 同时返回 entry_idx，避免二次哈希 */
    size_t entry_idx;
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key, &entry_idx);
    if (idx == NULL_BUCKET_INDEX)
        return false;

    woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[idx];

    /* 删除屏障：标记被删除的 key 和 val */
    woort_GC_delete_barrier_dynbox(bucket->m_key);
    woort_GC_delete_barrier_dynbox(bucket->m_val);

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

        /* 在 key 被清零前计算其所属的 entry 槽位 */
        const size_t last_entry_idx =
            woort_DynBox_hash(last_bucket->m_key) & gcmap->m_mask;

        /* 通过写屏障复制 key 和 val */
        woort_GC_mixed_write_barrier_dynbox(&bucket->m_key, last_bucket->m_key);
        woort_GC_mixed_write_barrier_dynbox(&bucket->m_val, last_bucket->m_val);

        last_bucket->m_key.m_boxed = 0;
        last_bucket->m_val.m_boxed = 0;

        /* 继承 last_bucket 在链表中的位置：否则 bucket[idx] 会保留被删除
           节点的旧 m_next/m_prev，导致碰撞链断裂（丢元素）甚至成环
           （_woort_GCMap_find_bucket 死循环）。 */
        bucket->m_prev = last_prev;
        bucket->m_next = last_next;

        /* 更新最后一个 bucket 的邻居指针 */
        if (last_prev != NULL_BUCKET_INDEX)
            gcmap->m_buckets[last_prev].m_next = idx;
        else
            gcmap->m_entries[last_entry_idx] = idx;

        if (last_next != NULL_BUCKET_INDEX)
            gcmap->m_buckets[last_next].m_prev = idx;
    }

    --gcmap->m_size;
    return true;
}

WOORT_NODISCARD bool woort_GCMap_get(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox* out_val)
{
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key, NULL);
    if (idx == NULL_BUCKET_INDEX)
        return false;

    assert(out_val != NULL);
    *out_val = gcmap->m_buckets[idx].m_val;

    return true;
}

WOORT_NODISCARD bool woort_GCMap_contains(woort_GCMap* gcmap, woort_DynBox key)
{
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key, NULL);
    return idx != NULL_BUCKET_INDEX;
}

WOORT_NODISCARD bool woort_GCMap_insert(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
    /* 检查 key 是否已存在 */
    if (_woort_GCMap_find_bucket(gcmap, key, NULL) != NULL_BUCKET_INDEX)
        return false;

    /* key 不存在，直接创建（避免 _woort_GCMap_get_writable_bucket_for_key
       内部的二次查找） */
    woort_GCMap_Bucket* const bucket = _woort_GCMap_create_bucket(gcmap, key);
    woort_GC_init_write_barrier_dynbox(&bucket->m_val, val);
    return true;
}

WOORT_NODISCARD bool woort_GCMap_set(woort_GCMap* gcmap, woort_DynBox key, woort_DynBox val)
{
    const uint32_t idx = _woort_GCMap_find_bucket(gcmap, key, NULL);
    if (idx == NULL_BUCKET_INDEX)
        return false;

    woort_GC_mixed_write_barrier_dynbox(&gcmap->m_buckets[idx].m_val, val);
    return true;
}

/* ======================================================================
 * 类型特化的查找函数（返回指针）：用于原地修改
 * ====================================================================== */

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

/* ======================================================================
 * 类型特化的查找或创建函数（返回指针）：用于原地修改，不存在则创建
 * ====================================================================== */

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

    /* 初始化 m_key/m_val 为 nil，避免 mixed 写屏障标记脏的旧值 */
    new_bucket->m_key.m_boxed = 0;
    new_bucket->m_val.m_boxed = 0;
    woort_DynBox_box_int_with_barrier(&new_bucket->m_key, key);

    _woort_GCMap_link_bucket_to_head(gcmap, new_idx, entry_idx);

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

    /* 初始化 m_key/m_val 为 nil，避免 mixed 写屏障标记脏的旧值 */
    new_bucket->m_key.m_boxed = 0;
    new_bucket->m_val.m_boxed = 0;
    woort_DynBox_box_real_with_barrier(&new_bucket->m_key, key);

    _woort_GCMap_link_bucket_to_head(gcmap, new_idx, entry_idx);

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

    /* 初始化 m_key/m_val 为 nil，避免 mixed 写屏障标记脏的旧值 */
    new_bucket->m_key.m_boxed = 0;
    new_bucket->m_val.m_boxed = 0;
    woort_DynBox_box_bool_with_barrier(&new_bucket->m_key, key);

    _woort_GCMap_link_bucket_to_head(gcmap, new_idx, entry_idx);

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

    /* 未找到，直接创建（复用 dynbox 创建路径） */
    woort_GCMap_Bucket* const new_bucket = _woort_GCMap_create_bucket(gcmap, key);
    return &new_bucket->m_val;
}

/* ======================================================================
 * 类型特化的查找函数：string
 * ====================================================================== */

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

    /* 初始化 m_val 为 nil，避免后续 mixed 写屏障标记脏的旧值 */
    new_bucket->m_val.m_boxed = 0;
    {
        woort_DynBox boxed;
        boxed.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)str);
        woort_GC_init_write_barrier_dynbox(&new_bucket->m_key, boxed);
    }

    _woort_GCMap_link_bucket_to_head(gcmap, new_idx, entry_idx);

    ++gcmap->m_size;
    return &new_bucket->m_val;
}

WOORT_NODISCARD bool woort_GCMap_get_key_value_by_index(
    const woort_GCMap* gcmap,
    size_t index,
    /* OPTIONAL */ woort_DynBox* out_key,
    /* OPTIONAL */ woort_DynBox* out_val)
{
    if (index >= gcmap->m_size)
        return false;

    const woort_GCMap_Bucket* const bucket = &gcmap->m_buckets[index];
    if (out_key != NULL)
        *out_key = bucket->m_key;
    if (out_val != NULL)
        *out_val = bucket->m_val;
    return true;
}

WOORT_NODISCARD woort_GCMap_Bucket* woort_GCMap_emplace_prepare(woort_GCMap* gcmap)
{
    if (gcmap->m_size >= gcmap->m_mask)
        woort_GCMap_reserve(gcmap, gcmap->m_size + 1);

    return &gcmap->m_buckets[gcmap->m_size];
}

void woort_GCMap_emplace_commit(woort_GCMap* gcmap)
{
    const uint32_t idx = (uint32_t)gcmap->m_size;
    const size_t entry_idx =
        woort_DynBox_hash(gcmap->m_buckets[idx].m_key) & gcmap->m_mask;

    _woort_GCMap_link_bucket_to_head(gcmap, idx, entry_idx);

    ++gcmap->m_size;
}

void woort_GCMap_copy(woort_GCMap* dst, const woort_GCMap* src)
{
    if (dst == src)
        return;

    woort_GCMap_clear(dst);
    woort_GCMap_reserve(dst, src->m_size);

    /* dst 已清空，所有 key 必定不存在，直接创建以跳过查找 */
    for (size_t i = 0; i < src->m_size; ++i)
    {
        woort_GCMap_Bucket* const bucket =
            _woort_GCMap_create_bucket(dst, src->m_buckets[i].m_key);
        woort_GC_mixed_write_barrier_dynbox(
            &bucket->m_val, src->m_buckets[i].m_val);
    }
}

void woort_GCMap_swap(woort_GCMap* a, woort_GCMap* b)
{
    size_t tmp_mask = a->m_mask;
    size_t tmp_size = a->m_size;
    uint32_t* tmp_entries = a->m_entries;
    woort_GCMap_Bucket* tmp_buckets = a->m_buckets;

    a->m_mask = b->m_mask;
    a->m_size = b->m_size;
    a->m_entries = b->m_entries;
    /* mixed 写屏障会同时标记 src（hard）和旧值（fuzzy），
       无需额外的 delete_barrier_gcunit(a->m_buckets) */
    woort_GC_mixed_write_barrier_gcunit((void**)&a->m_buckets, b->m_buckets);

    b->m_mask = tmp_mask;
    b->m_size = tmp_size;
    b->m_entries = tmp_entries;
    woort_GC_mixed_write_barrier_gcunit((void**)&b->m_buckets, tmp_buckets);
}
