#pragma once

/*
woort_hashmap.h
*/

#include "woort_diagnosis.h"
#include "woort_linklist.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct woort_HashMap
{
    /* Bucket array (each bucket is a woort_Vector of entries) */
    /* OPTIONAL after init. */ struct woort_HashMapEntry** m_buckets;
    size_t              m_bucket_count;
    size_t              m_size;
    /* OPTIONAL */ struct woort_HashMapEntry* m_free_entries;

    /* User-defined functions */
    size_t  (*m_hash_fn)(const void* key);
    bool    (*m_equal_fn)(const void* key1, const void* key2);

    /* Key and value sizes */
    size_t         m_key_size;
    size_t         m_value_size;

} woort_HashMap;

void woort_hashmap_init(
    woort_HashMap* map,
    size_t key_size,
    size_t value_size,
    size_t(*hash_fn)(const void* key),
    bool (*equal_fn)(const void* key1, const void* key2));
void woort_hashmap_deinit(woort_HashMap* map);

typedef enum woort_hashmap_Result
{
    WOORT_HASHMAP_RESULT_OK,

    WOORT_HASHMAP_RESULT_ALREADY_EXIST,
    WOORT_HASHMAP_RESULT_OUT_OF_MEMORY,

}woort_hashmap_Result;

/*
Maybe:
    WOORT_HASHMAP_RESULT_OK (Emplace and get storage address)
    WOORT_HASHMAP_RESULT_ALREADY_EXIST (Get storage address)
    WOORT_HASHMAP_RESULT_OUT_OF_MEMORY (Do nothing)
*/
WOORT_NODISCARD woort_hashmap_Result woort_hashmap_get_or_emplace(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);

/*
Maybe:
    WOORT_HASHMAP_RESULT_OK (Key value pair has been inserted)
    WOORT_HASHMAP_RESULT_ALREADY_EXIST (Do nothing)
    WOORT_HASHMAP_RESULT_OUT_OF_MEMORY (Do nothing)
*/
WOORT_NODISCARD woort_hashmap_Result woort_hashmap_insert(
    woort_HashMap* map,
    const void* key,
    const void* value);

WOORT_NODISCARD bool woort_hashmap_find(
    woort_HashMap* map,
    const void* key,
    void** out_value_addr);

WOORT_NODISCARD bool woort_hashmap_contains(
    woort_HashMap* map,
    const void* key);

WOORT_NODISCARD bool woort_hashmap_remove(
    woort_HashMap* map,
    const void* key);

void woort_hashmap_clear(woort_HashMap* map);

typedef bool /* false if break loop. */ (*woort_HashMapForEachCallback)(
    const void* key,
    void* value,
    void* user_data);

WOORT_NODISCARD bool /* foreach complete */ woort_hashmap_foreach(
    woort_HashMap* map,
    woort_HashMapForEachCallback callback,
    void* user_data);
