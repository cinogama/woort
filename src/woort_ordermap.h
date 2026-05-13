#pragma once

/*
woort_ordermap.h
有序红黑树 Map 容器（非 GC 对象，类似 woort_HashMap）
*/

#include "woort_diagnosis.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct woort_OrderMap woort_OrderMap;

WOORT_NODISCARD bool woort_ordermap_create(
    size_t key_size,
    size_t value_size,
    int (*compare_fn)(const void* key1, const void* key2),
    woort_OrderMap** out_map);

void woort_ordermap_destroy(/* OPTIONAL */ woort_OrderMap* map);

typedef enum woort_ordermap_Result
{
    WOORT_ORDERMAP_RESULT_OK,

    WOORT_ORDERMAP_RESULT_ALREADY_EXIST,
    WOORT_ORDERMAP_RESULT_OUT_OF_MEMORY,

} woort_ordermap_Result;

/*
可能返回：
    WOORT_ORDERMAP_RESULT_OK（新建节点，返回存储地址）
    WOORT_ORDERMAP_RESULT_ALREADY_EXIST（键已存在，返回已有存储地址）
    WOORT_ORDERMAP_RESULT_OUT_OF_MEMORY（分配失败）
*/
WOORT_NODISCARD woort_ordermap_Result woort_ordermap_get_or_emplace(
    woort_OrderMap* map,
    const void* key,
    void** out_value_addr);

/*
可能返回：
    WOORT_ORDERMAP_RESULT_OK（键值对已插入）
    WOORT_ORDERMAP_RESULT_ALREADY_EXIST（不做任何事）
    WOORT_ORDERMAP_RESULT_OUT_OF_MEMORY（不做任何事）
*/
WOORT_NODISCARD woort_ordermap_Result woort_ordermap_insert(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL if value size is 0 */ const void* value);

WOORT_NODISCARD bool woort_ordermap_find(
    woort_OrderMap* map,
    const void* key,
    void** out_value_addr);

WOORT_NODISCARD bool woort_ordermap_contains(
    woort_OrderMap* map,
    const void* key);

WOORT_NODISCARD bool woort_ordermap_remove(
    woort_OrderMap* map,
    const void* key);

void woort_ordermap_clear(woort_OrderMap* map);

typedef bool /* false if break loop. */ (*woort_OrderMapForEachCallback)(
    const void* key,
    void* value,
    void* user_data);

/* 按键的升序遍历所有键值对 */
WOORT_NODISCARD bool /* foreach complete */ woort_ordermap_foreach(
    woort_OrderMap* map,
    woort_OrderMapForEachCallback callback,
    void* user_data);

WOORT_NODISCARD bool woort_ordermap_is_empty(woort_OrderMap* map);

/* 获取最小键及其值。如果 map 为空，返回 false。 */
WOORT_NODISCARD bool woort_ordermap_min(
    woort_OrderMap* map,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr);

/* 获取最大键及其值。如果 map 为空，返回 false。 */
WOORT_NODISCARD bool woort_ordermap_max(
    woort_OrderMap* map,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr);

/* 二分查找：第一个键 >= key 的节点。未找到返回 false。 */
WOORT_NODISCARD bool woort_ordermap_lower_bound(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr);

/* 二分查找：第一个键 > key 的节点。未找到返回 false。 */
WOORT_NODISCARD bool woort_ordermap_upper_bound(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr);

/* 二分查找：最后一个键 <= key 的节点。未找到返回 false。 */
WOORT_NODISCARD bool woort_ordermap_find_le(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr);
