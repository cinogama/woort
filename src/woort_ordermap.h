#pragma once

/*
woort_ordermap.h
有序红黑树 Map 容器（非 GC 对象，类似 woort_HashMap）
*/

#include "woort_diagnosis.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct woort_OrderMap
{
    /* NIL 哨兵节点，始终黑色。初始化后非 NULL。 */
    /* OPTIONAL after init. */ struct woort_OrderMapNode* m_nil;
    /* 树根节点。空树时指向 m_nil。 */
    /* OPTIONAL after init. */ struct woort_OrderMapNode* m_root;
    size_t              m_size;
    /* 空闲节点链表，用于复用被删除的节点。 */
    /* OPTIONAL */ struct woort_OrderMapNode* m_free_nodes;

    /* 用户定义的键比较函数。
       返回值 < 0 表示 key1 < key2，
       返回值 = 0 表示 key1 == key2，
       返回值 > 0 表示 key1 > key2。 */
    int     (*m_compare_fn)(const void* key1, const void* key2);

    /* 键和值的大小 */
    size_t         m_key_size;
    size_t         m_value_size;

} woort_OrderMap;

void woort_ordermap_init(
    woort_OrderMap* map,
    size_t key_size,
    size_t value_size,
    int (*compare_fn)(const void* key1, const void* key2));
void woort_ordermap_deinit(woort_OrderMap* map);

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
