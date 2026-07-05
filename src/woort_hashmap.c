#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#include "woort_hashmap.h"
#include "woort_linklist.h"
#include "woort_log.h"

#define WOORT_HASHMAP_INITIAL_CAPACITY 16
#define WOORT_HASHMAP_KEY_VALUE_MAX_ALIGN 8
#define WOORT_HASHMAP_KEY_VALUE_MAX_ALIGN_MASK (WOORT_HASHMAP_KEY_VALUE_MAX_ALIGN - 1)

/* Key-Value pair structure stored in buckets */
typedef struct woort_HashMapEntry
{
    /* OPTIONAL */ struct woort_HashMapEntry* m_prev;
    /* OPTIONAL */ struct woort_HashMapEntry* m_next;

    /* Value data begin */
    void* m_value;

    /* Key and value instance storage here. */
    _Alignas(WOORT_HASHMAP_KEY_VALUE_MAX_ALIGN)
        char m_kv_storage[];

} woort_HashMapEntry;

void woort_hashmap_init(
    woort_HashMap* map,
    size_t key_size,
    size_t value_size,
    size_t(*hash_fn)(const void* key),
    bool (*equal_fn)(const void* key1, const void* key2))
{
    map->m_buckets = NULL;
    map->m_bucket_count = 0;
    map->m_size = 0;
    map->m_free_entries = NULL;

    assert(hash_fn != NULL && equal_fn != NULL);
    map->m_hash_fn = hash_fn;
    map->m_equal_fn = equal_fn;

    map->m_key_size = key_size;
    map->m_value_size = value_size;
}

void woort_hashmap_deinit(woort_HashMap* map)
{
    for (woort_HashMapEntry* free_entry = map->m_free_entries;
        free_entry != NULL;)
    {
        woort_HashMapEntry* const current_free_entry = free_entry;
        free_entry = free_entry->m_next;

        free(current_free_entry);
    }

    if (map->m_buckets != NULL)
    {
        for (size_t bucket_id = 0; bucket_id < map->m_bucket_count; ++bucket_id)
        {
            /* Free entries in each bucket. */
            for (woort_HashMapEntry* free_entry = map->m_buckets[bucket_id];
                free_entry != NULL;)
            {
                woort_HashMapEntry* const current_free_entry = free_entry;
                free_entry = free_entry->m_next;

                free(current_free_entry);
            }
        }
        free(map->m_buckets);
    }
}

WOORT_NODISCARD static /* OPTIONAL */
woort_HashMapEntry* _woort_hashmap_get_free_entry_and_reset_prev(woort_HashMap* map)
{
    woort_HashMapEntry* free_entry = map->m_free_entries;
    if (free_entry != NULL)
    {
        map->m_free_entries = free_entry->m_next;
    }
    else
    {
        /* No free entry in list, malloc a new one. */
        const size_t aligned_key_size =
            (map->m_key_size + WOORT_HASHMAP_KEY_VALUE_MAX_ALIGN_MASK)
            & ~WOORT_HASHMAP_KEY_VALUE_MAX_ALIGN_MASK;

        free_entry = malloc(
            sizeof(woort_HashMapEntry)
            + aligned_key_size
            + map->m_value_size);

        if (free_entry == NULL)
        {
            WOORT_DEBUG("Out of memory.");
            return NULL;
        }

        free_entry->m_value =
            free_entry->m_kv_storage + aligned_key_size;
    }

    free_entry->m_prev = NULL;
    return free_entry;
}

static void _woort_hashmap_drop_entry(
    woort_HashMap* map, woort_HashMapEntry* entry_to_drop)
{
    /* Only use next ptr for entries. */
    entry_to_drop->m_next = map->m_free_entries;
    map->m_free_entries = entry_to_drop;
}

WOORT_NODISCARD static bool _woort_hashmap_rehash_to_externed(woort_HashMap* map)
{
    const size_t new_bucket_count = map->m_bucket_count != 0
        ? map->m_bucket_count * 2
        : WOORT_HASHMAP_INITIAL_CAPACITY;

    woort_HashMapEntry** const new_bucket_list = (woort_HashMapEntry**)calloc(
        new_bucket_count,
        sizeof(woort_HashMapEntry*));

    if (new_bucket_list == NULL)
    {
        WOORT_DEBUG("Out of memory.");
        return false;
    }

    if (map->m_buckets != NULL)
    {
        const size_t new_hash_mask = new_bucket_count - 1;
        for (size_t bucket_id = 0; bucket_id < map->m_bucket_count; ++bucket_id)
        {
            /* Rehash entries in each bucket. */
            for (woort_HashMapEntry* entry = map->m_buckets[bucket_id];
                entry != NULL;)
            {
                woort_HashMapEntry* const current_entry = entry;
                entry = entry->m_next;

                const size_t new_bucket_id =
                    map->m_hash_fn(current_entry->m_kv_storage) & new_hash_mask;

                current_entry->m_prev = NULL;
                current_entry->m_next = new_bucket_list[new_bucket_id];

                if (current_entry->m_next != NULL)
                    current_entry->m_next->m_prev = current_entry;

                new_bucket_list[new_bucket_id] = current_entry;
            }
        }
        free(map->m_buckets);
    }

    map->m_buckets = new_bucket_list;
    map->m_bucket_count = new_bucket_count;

    return true;
}

WOORT_NODISCARD bool woort_hashmap_is_empty(woort_HashMap* map)
{
    return map->m_size == 0;
}

WOORT_NODISCARD woort_hashmap_Result woort_hashmap_insert(
    woort_HashMap* map,
    const void* key,
    /* OPTIONAL if value size is 0 */ const void* value)
{
    void* storage;
    const woort_hashmap_Result r = 
        woort_hashmap_get_or_emplace(map, key, &storage);

    if (r == WOORT_HASHMAP_RESULT_OK && map->m_value_size != 0)
        /* New kvpair inserted, copy value into storage. */
        memcpy(storage, value, map->m_value_size);

    return r;
}

WOORT_NODISCARD bool woort_hashmap_find(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr)
{
    if (map->m_bucket_count == 0)
        return false;

    const size_t hash_mask = map->m_bucket_count - 1;
    const size_t bucket_id =
        map->m_hash_fn(key) & hash_mask;

    /* Find if exist? */
    for (woort_HashMapEntry* entry = map->m_buckets[bucket_id];
        entry != NULL;
        entry = entry->m_next)
    {
        if (map->m_equal_fn(entry->m_kv_storage, key))
        {
            /* Exist! */
            *out_value_addr = entry->m_value;
            return true;
        }
    }
    return false;
}

WOORT_NODISCARD void* woort_hashmap_at(
    woort_HashMap* map,
    const void* key)
{
    void* val_storage;
    if (!woort_hashmap_find(map, key, &val_storage))
        abort();

    return val_storage;
}

WOORT_NODISCARD woort_hashmap_Result woort_hashmap_get_or_emplace(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr)
{
    if (map->m_size * 4 >= map->m_bucket_count * 3)
    {
        if (!_woort_hashmap_rehash_to_externed(map))
            return WOORT_HASHMAP_RESULT_OUT_OF_MEMORY;
    }

    const size_t hash_mask = map->m_bucket_count - 1;
    const size_t bucket_id =
        map->m_hash_fn(key) & hash_mask;

    for (woort_HashMapEntry* entry = map->m_buckets[bucket_id];
        entry != NULL;
        entry = entry->m_next)
    {
        if (map->m_equal_fn(entry->m_kv_storage, key))
        {
            *out_value_addr = entry->m_value;
            return WOORT_HASHMAP_RESULT_ALREADY_EXIST;
        }
    }

    woort_HashMapEntry* const new_entry =
        _woort_hashmap_get_free_entry_and_reset_prev(map);

    if (new_entry == NULL)
        return WOORT_HASHMAP_RESULT_OUT_OF_MEMORY;

    memcpy(new_entry->m_kv_storage, key, map->m_key_size);
    *out_value_addr = new_entry->m_value;

    new_entry->m_next = map->m_buckets[bucket_id];
    if (new_entry->m_next != NULL)
        new_entry->m_next->m_prev = new_entry;
    map->m_buckets[bucket_id] = new_entry;

    ++map->m_size;

    return WOORT_HASHMAP_RESULT_OK;
}

WOORT_NODISCARD bool woort_hashmap_contains(
    woort_HashMap* map,
    const void* key)
{
    void* _value;

    (void)_value;
    return woort_hashmap_find(map, key, &_value);
}

WOORT_NODISCARD bool woort_hashmap_remove(
    woort_HashMap* map,
    const void* key)
{
    if (map->m_bucket_count == 0)
        return false;

    const size_t hash_mask = map->m_bucket_count - 1;
    const size_t bucket_id =
        map->m_hash_fn(key) & hash_mask;

    /* Find if exist? */
    for (woort_HashMapEntry* entry = map->m_buckets[bucket_id];
        entry != NULL;
        entry = entry->m_next)
    {
        if (map->m_equal_fn(entry->m_kv_storage, key))
        {
            /* Exist! */
            if (entry->m_prev == NULL)
                /* First node in bucket. */
                map->m_buckets[bucket_id] = entry->m_next;
            else
                entry->m_prev->m_next = entry->m_next;

            if (entry->m_next != NULL)
                entry->m_next->m_prev = entry->m_prev;

            _woort_hashmap_drop_entry(map, entry);

            --map->m_size;

            return true;
        }
    }
    return false;
}

void woort_hashmap_clear(woort_HashMap* map)
{
    for (size_t bucket_id = 0; bucket_id < map->m_bucket_count; ++bucket_id)
    {
        for (woort_HashMapEntry* entry = map->m_buckets[bucket_id];
            entry != NULL;)
        {
            woort_HashMapEntry* const current_entry = entry;
            entry = entry->m_next;

            _woort_hashmap_drop_entry(map, current_entry);
        }
        map->m_buckets[bucket_id] = NULL;
    }
    map->m_size = 0;
}

WOORT_NODISCARD bool woort_hashmap_foreach(
    woort_HashMap* map,
    woort_HashMapForEachCallback callback,
    void* user_data)
{
    for (size_t bucket_id = 0; bucket_id < map->m_bucket_count; ++bucket_id)
    {
        /* Iterate entries in each bucket. */
        for (woort_HashMapEntry* entry = map->m_buckets[bucket_id];
            entry != NULL;
            entry = entry->m_next)
        {
            if (!callback(entry->m_kv_storage, entry->m_value, user_data))
                /* Break. */
                return false;
        }
    }
    return true;
}