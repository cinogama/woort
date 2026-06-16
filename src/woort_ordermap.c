#include "woort_ordermap.h"
#include "woort_log.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#define WOORT_ORDERMAP_KEY_VALUE_MAX_ALIGN 8
#define WOORT_ORDERMAP_KEY_VALUE_MAX_ALIGN_MASK (WOORT_ORDERMAP_KEY_VALUE_MAX_ALIGN - 1)

typedef enum woort_OrderMapColor
{
    WOORT_ORDERMAP_BLACK,
    WOORT_ORDERMAP_RED,
} woort_OrderMapColor;

typedef struct woort_OrderMapNode
{
    woort_OrderMapColor     m_color;
    struct woort_OrderMapNode* m_parent;
    struct woort_OrderMapNode* m_left;
    struct woort_OrderMapNode* m_right;

    /* 值数据起始地址 */
    void* m_value;

    /* 键和值实例存储在此处 */
    _Alignas(WOORT_ORDERMAP_KEY_VALUE_MAX_ALIGN)
        char m_kv_storage[];

} woort_OrderMapNode;

struct woort_OrderMap
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
};

/*
===========================================
内部辅助函数
===========================================
*/

/* 获取节点中键的地址 */
static inline void* _woort_ordermap_node_key(woort_OrderMapNode* node)
{
    return node->m_kv_storage;
}

/* 从空闲链表中获取一个节点，若无则 malloc */
WOORT_NODISCARD /* OPTIONAL */
static woort_OrderMapNode* _woort_ordermap_alloc_node(woort_OrderMap* map)
{
    woort_OrderMapNode* node = map->m_free_nodes;
    if (node != NULL)
    {
        map->m_free_nodes = node->m_right;
    }
    else
    {
        const size_t aligned_key_size =
            (map->m_key_size + WOORT_ORDERMAP_KEY_VALUE_MAX_ALIGN_MASK)
            & ~WOORT_ORDERMAP_KEY_VALUE_MAX_ALIGN_MASK;

        node = (woort_OrderMapNode*)malloc(
            sizeof(woort_OrderMapNode)
            + aligned_key_size
            + map->m_value_size);

        if (node == NULL)
        {
            WOORT_DEBUG("Out of memory.");
            return NULL;
        }

        node->m_value =
            node->m_kv_storage + aligned_key_size;
    }

    return node;
}

/* 将节点放回空闲链表（复用 m_right 作为链表指针） */
static void _woort_ordermap_free_node(
    woort_OrderMap* map, woort_OrderMapNode* node)
{
    node->m_right = map->m_free_nodes;
    map->m_free_nodes = node;
}

/*
----------------------------------------------------------------------
红黑树基本操作
----------------------------------------------------------------------
*/

/* 左旋：将 x 向左旋转，使其右孩子 y 成为新的子树根 */
static void _woort_ordermap_left_rotate(
    woort_OrderMap* map, woort_OrderMapNode* x)
{
    woort_OrderMapNode* const y = x->m_right;
    x->m_right = y->m_left;

    if (y->m_left != map->m_nil)
        y->m_left->m_parent = x;

    y->m_parent = x->m_parent;

    if (x->m_parent == map->m_nil)
        map->m_root = y;
    else if (x == x->m_parent->m_left)
        x->m_parent->m_left = y;
    else
        x->m_parent->m_right = y;

    y->m_left = x;
    x->m_parent = y;
}

/* 右旋：将 x 向右旋转，使其左孩子 y 成为新的子树根 */
static void _woort_ordermap_right_rotate(
    woort_OrderMap* map, woort_OrderMapNode* x)
{
    woort_OrderMapNode* const y = x->m_left;
    x->m_left = y->m_right;

    if (y->m_right != map->m_nil)
        y->m_right->m_parent = x;

    y->m_parent = x->m_parent;

    if (x->m_parent == map->m_nil)
        map->m_root = y;
    else if (x == x->m_parent->m_right)
        x->m_parent->m_right = y;
    else
        x->m_parent->m_left = y;

    y->m_right = x;
    x->m_parent = y;
}

/* 插入修复：恢复红黑树性质 */
static void _woort_ordermap_insert_fixup(
    woort_OrderMap* map, woort_OrderMapNode* z)
{
    while (z->m_parent->m_color == WOORT_ORDERMAP_RED)
    {
        if (z->m_parent == z->m_parent->m_parent->m_left)
        {
            woort_OrderMapNode* const y = z->m_parent->m_parent->m_right;

            if (y->m_color == WOORT_ORDERMAP_RED)
            {
                /* Case 1: 叔节点为红色 */
                z->m_parent->m_color = WOORT_ORDERMAP_BLACK;
                y->m_color = WOORT_ORDERMAP_BLACK;
                z->m_parent->m_parent->m_color = WOORT_ORDERMAP_RED;
                z = z->m_parent->m_parent;
            }
            else
            {
                if (z == z->m_parent->m_right)
                {
                    /* Case 2: 叔节点为黑色且 z 是右孩子 */
                    z = z->m_parent;
                    _woort_ordermap_left_rotate(map, z);
                }
                /* Case 3: 叔节点为黑色且 z 是左孩子 */
                z->m_parent->m_color = WOORT_ORDERMAP_BLACK;
                z->m_parent->m_parent->m_color = WOORT_ORDERMAP_RED;
                _woort_ordermap_right_rotate(map, z->m_parent->m_parent);
            }
        }
        else
        {
            /* 对称情况：z 的父节点是祖父的右孩子 */
            woort_OrderMapNode* const y = z->m_parent->m_parent->m_left;

            if (y->m_color == WOORT_ORDERMAP_RED)
            {
                z->m_parent->m_color = WOORT_ORDERMAP_BLACK;
                y->m_color = WOORT_ORDERMAP_BLACK;
                z->m_parent->m_parent->m_color = WOORT_ORDERMAP_RED;
                z = z->m_parent->m_parent;
            }
            else
            {
                if (z == z->m_parent->m_left)
                {
                    z = z->m_parent;
                    _woort_ordermap_right_rotate(map, z);
                }
                z->m_parent->m_color = WOORT_ORDERMAP_BLACK;
                z->m_parent->m_parent->m_color = WOORT_ORDERMAP_RED;
                _woort_ordermap_left_rotate(map, z->m_parent->m_parent);
            }
        }
    }

    map->m_root->m_color = WOORT_ORDERMAP_BLACK;
}

/* 查找键对应的节点。找到返回节点指针，否则返回 NULL。 */
WOORT_NODISCARD /* OPTIONAL */
static woort_OrderMapNode* _woort_ordermap_find_node(
    woort_OrderMap* map, const void* key)
{
    woort_OrderMapNode* x = map->m_root;

    while (x != map->m_nil)
    {
        const int cmp = map->m_compare_fn(key, _woort_ordermap_node_key(x));
        if (cmp == 0)
            return x;
        else if (cmp < 0)
            x = x->m_left;
        else
            x = x->m_right;
    }

    return NULL;
}

/* 查找第一个键 >= key 的节点。没找到返回 NULL。 */
WOORT_NODISCARD /* OPTIONAL */
static woort_OrderMapNode* _woort_ordermap_lower_bound_node(
    woort_OrderMap* map, const void* key)
{
    woort_OrderMapNode* x = map->m_root;
    woort_OrderMapNode* result = NULL;

    while (x != map->m_nil)
    {
        const int cmp = map->m_compare_fn(key, _woort_ordermap_node_key(x));
        if (cmp <= 0)
        {
            result = x;
            x = x->m_left;
        }
        else
            x = x->m_right;
    }

    return result;
}

/* 查找第一个键 > key 的节点。没找到返回 NULL。 */
WOORT_NODISCARD /* OPTIONAL */
static woort_OrderMapNode* _woort_ordermap_upper_bound_node(
    woort_OrderMap* map, const void* key)
{
    woort_OrderMapNode* x = map->m_root;
    woort_OrderMapNode* result = NULL;

    while (x != map->m_nil)
    {
        const int cmp = map->m_compare_fn(key, _woort_ordermap_node_key(x));
        if (cmp < 0)
        {
            result = x;
            x = x->m_left;
        }
        else
            x = x->m_right;
    }

    return result;
}

/* 查找最后一个键 <= key 的节点。没找到返回 NULL。 */
WOORT_NODISCARD /* OPTIONAL */
static woort_OrderMapNode* _woort_ordermap_find_le_node(
    woort_OrderMap* map, const void* key)
{
    woort_OrderMapNode* x = map->m_root;
    woort_OrderMapNode* result = NULL;

    while (x != map->m_nil)
    {
        const int cmp = map->m_compare_fn(key, _woort_ordermap_node_key(x));
        if (cmp == 0)
            return x;
        else if (cmp < 0)
            x = x->m_left;
        else
        {
            result = x;
            x = x->m_right;
        }
    }

    return result;
}

/* 获取子树中的最小节点 */
WOORT_NODISCARD
static woort_OrderMapNode* _woort_ordermap_subtree_min(
    woort_OrderMap* map, woort_OrderMapNode* x)
{
    while (x->m_left != map->m_nil)
        x = x->m_left;
    return x;
}

/* 红黑树移植：用 v 替换 u 的位置 */
static void _woort_ordermap_transplant(
    woort_OrderMap* map,
    woort_OrderMapNode* u,
    woort_OrderMapNode* v)
{
    if (u->m_parent == map->m_nil)
        map->m_root = v;
    else if (u == u->m_parent->m_left)
        u->m_parent->m_left = v;
    else
        u->m_parent->m_right = v;

    v->m_parent = u->m_parent;
}

/* 删除修复：恢复红黑树性质 */
static void _woort_ordermap_delete_fixup(
    woort_OrderMap* map, woort_OrderMapNode* x)
{
    while (x != map->m_root
           && x->m_color == WOORT_ORDERMAP_BLACK)
    {
        if (x == x->m_parent->m_left)
        {
            woort_OrderMapNode* w = x->m_parent->m_right;

            if (w->m_color == WOORT_ORDERMAP_RED)
            {
                /* Case 1 */
                w->m_color = WOORT_ORDERMAP_BLACK;
                x->m_parent->m_color = WOORT_ORDERMAP_RED;
                _woort_ordermap_left_rotate(map, x->m_parent);
                w = x->m_parent->m_right;
            }

            if (w->m_left->m_color == WOORT_ORDERMAP_BLACK
                && w->m_right->m_color == WOORT_ORDERMAP_BLACK)
            {
                /* Case 2 */
                w->m_color = WOORT_ORDERMAP_RED;
                x = x->m_parent;
            }
            else
            {
                if (w->m_right->m_color == WOORT_ORDERMAP_BLACK)
                {
                    /* Case 3 */
                    w->m_left->m_color = WOORT_ORDERMAP_BLACK;
                    w->m_color = WOORT_ORDERMAP_RED;
                    _woort_ordermap_right_rotate(map, w);
                    w = x->m_parent->m_right;
                }
                /* Case 4 */
                w->m_color = x->m_parent->m_color;
                x->m_parent->m_color = WOORT_ORDERMAP_BLACK;
                w->m_right->m_color = WOORT_ORDERMAP_BLACK;
                _woort_ordermap_left_rotate(map, x->m_parent);
                x = map->m_root;
            }
        }
        else
        {
            /* 对称情况 */
            woort_OrderMapNode* w = x->m_parent->m_left;

            if (w->m_color == WOORT_ORDERMAP_RED)
            {
                w->m_color = WOORT_ORDERMAP_BLACK;
                x->m_parent->m_color = WOORT_ORDERMAP_RED;
                _woort_ordermap_right_rotate(map, x->m_parent);
                w = x->m_parent->m_left;
            }

            if (w->m_right->m_color == WOORT_ORDERMAP_BLACK
                && w->m_left->m_color == WOORT_ORDERMAP_BLACK)
            {
                w->m_color = WOORT_ORDERMAP_RED;
                x = x->m_parent;
            }
            else
            {
                if (w->m_left->m_color == WOORT_ORDERMAP_BLACK)
                {
                    w->m_right->m_color = WOORT_ORDERMAP_BLACK;
                    w->m_color = WOORT_ORDERMAP_RED;
                    _woort_ordermap_left_rotate(map, w);
                    w = x->m_parent->m_left;
                }
                w->m_color = x->m_parent->m_color;
                x->m_parent->m_color = WOORT_ORDERMAP_BLACK;
                w->m_left->m_color = WOORT_ORDERMAP_BLACK;
                _woort_ordermap_right_rotate(map, x->m_parent);
                x = map->m_root;
            }
        }
    }

    x->m_color = WOORT_ORDERMAP_BLACK;
}

/* 递归释放子树中的所有节点 */
static void _woort_ordermap_free_subtree(
    woort_OrderMap* map, woort_OrderMapNode* node)
{
    if (node == map->m_nil)
        return;

    _woort_ordermap_free_subtree(map, node->m_left);
    _woort_ordermap_free_subtree(map, node->m_right);

    free(node);
}

/* 递归将子树中的节点移入空闲链表 */
static void _woort_ordermap_clear_subtree(
    woort_OrderMap* map, woort_OrderMapNode* node)
{
    if (node == map->m_nil)
        return;

    _woort_ordermap_clear_subtree(map, node->m_left);
    _woort_ordermap_clear_subtree(map, node->m_right);

    _woort_ordermap_free_node(map, node);
}

/* 中序遍历 */
static bool _woort_ordermap_inorder_foreach(
    woort_OrderMap* map,
    woort_OrderMapNode* node,
    woort_OrderMapForEachCallback callback,
    void* user_data)
{
    if (node == map->m_nil)
        return true;

    if (!_woort_ordermap_inorder_foreach(map, node->m_left, callback, user_data))
        return false;

    if (!callback(_woort_ordermap_node_key(node), node->m_value, user_data))
        return false;

    if (!_woort_ordermap_inorder_foreach(map, node->m_right, callback, user_data))
        return false;

    return true;
}

/* 预先对字符串或自定义类型应用比较函数。
   找到已存在节点时返回该节点（此时 *out_last_cmp 为 0）；
   未找到时返回 NULL，*out_parent 为待插入位置的父节点，
   *out_last_cmp 为最后一次与 *out_parent 比较的结果（< 0 挂左，> 0 挂右）。 */
WOORT_NODISCARD /* OPTIONAL */
static inline woort_OrderMapNode* _woort_ordermap_find_insert_pos(
    woort_OrderMap* map,
    const void* key,
    woort_OrderMapNode** out_parent,
    int* out_last_cmp)
{
    woort_OrderMapNode* y = map->m_nil;
    woort_OrderMapNode* x = map->m_root;
    int last_cmp = 0;

    while (x != map->m_nil)
    {
        y = x;
        last_cmp = map->m_compare_fn(key, _woort_ordermap_node_key(x));
        if (last_cmp == 0)
        {
            *out_parent = x;
            return x; /* 找到了，已存在 */
        }
        else if (last_cmp < 0)
            x = x->m_left;
        else
            x = x->m_right;
    }

    *out_parent = y;
    *out_last_cmp = last_cmp;
    return NULL;
}

static void _woort_ordermap_init(
    woort_OrderMap* map,
    size_t key_size,
    size_t value_size,
    int (*compare_fn)(const void* key1, const void* key2))
{
    assert(compare_fn != NULL);

    map->m_nil = (woort_OrderMapNode*)malloc(sizeof(woort_OrderMapNode));
    assert(map->m_nil != NULL);

    map->m_nil->m_color = WOORT_ORDERMAP_BLACK;
    map->m_nil->m_parent = map->m_nil;
    map->m_nil->m_left = map->m_nil;
    map->m_nil->m_right = map->m_nil;
    map->m_nil->m_value = NULL;

    map->m_root = map->m_nil;
    map->m_size = 0;
    map->m_free_nodes = NULL;

    map->m_compare_fn = compare_fn;
    map->m_key_size = key_size;
    map->m_value_size = value_size;
}

static void _woort_ordermap_deinit(woort_OrderMap* map)
{
    /* 释放空闲链表中的所有节点 */
    {
        woort_OrderMapNode* node = map->m_free_nodes;
        while (node != NULL)
        {
            woort_OrderMapNode* const next = node->m_right;
            free(node);
            node = next;
        }
        map->m_free_nodes = NULL;
    }

    /* 释放树中的所有节点 */
    if (map->m_nil != NULL)
    {
        _woort_ordermap_free_subtree(map, map->m_root);
        free(map->m_nil);
        map->m_nil = NULL;
        map->m_root = NULL;
    }
}

/*
===========================================
公共 API
===========================================
*/

WOORT_NODISCARD bool woort_ordermap_create(
    size_t key_size,
    size_t value_size,
    int (*compare_fn)(const void* key1, const void* key2),
    woort_OrderMap** out_map)
{
    woort_OrderMap* const map = (woort_OrderMap*)malloc(sizeof(woort_OrderMap));
    if (map == NULL)
    {
        WOORT_DEBUG("Out of memory.");
        return false;
    }

    _woort_ordermap_init(map, key_size, value_size, compare_fn);

    *out_map = map;
    return true;
}

void woort_ordermap_destroy(/* OPTIONAL */ woort_OrderMap* map)
{
    if (map == NULL)
        return;

    _woort_ordermap_deinit(map);
    free(map);
}

WOORT_NODISCARD woort_ordermap_Result woort_ordermap_get_or_emplace(
    woort_OrderMap* map,
    const void* key,
    void** out_value_addr)
{
    woort_OrderMapNode* parent;
    int last_cmp;
    woort_OrderMapNode* existing = _woort_ordermap_find_insert_pos(map, key, &parent, &last_cmp);

    if (existing != NULL)
    {
        *out_value_addr = existing->m_value;
        return WOORT_ORDERMAP_RESULT_ALREADY_EXIST;
    }

    woort_OrderMapNode* const new_node = _woort_ordermap_alloc_node(map);
    if (new_node == NULL)
        return WOORT_ORDERMAP_RESULT_OUT_OF_MEMORY;

    /* 拷贝键到节点存储区 */
    memcpy(_woort_ordermap_node_key(new_node), key, map->m_key_size);
    *out_value_addr = new_node->m_value;

    /* 设置节点基本属性 */
    new_node->m_parent = parent;
    new_node->m_left = map->m_nil;
    new_node->m_right = map->m_nil;
    new_node->m_color = WOORT_ORDERMAP_RED;

    /* 连接到树中 */
    if (parent == map->m_nil)
        map->m_root = new_node;
    else if (last_cmp < 0)
        parent->m_left = new_node;
    else
        parent->m_right = new_node;

    /* 修复红黑树 */
    _woort_ordermap_insert_fixup(map, new_node);

    ++map->m_size;
    return WOORT_ORDERMAP_RESULT_OK;
}

WOORT_NODISCARD woort_ordermap_Result woort_ordermap_insert(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL if value size is 0 */ const void* value)
{
    assert(value != NULL || map->m_value_size == 0);

    void* storage;
    const woort_ordermap_Result r =
        woort_ordermap_get_or_emplace(map, key, &storage);

    if (r == WOORT_ORDERMAP_RESULT_OK && map->m_value_size != 0)
        memcpy(storage, value, map->m_value_size);

    return r;
}

WOORT_NODISCARD bool woort_ordermap_find(
    woort_OrderMap* map,
    const void* key,
    void** out_value_addr)
{
    woort_OrderMapNode* const node = _woort_ordermap_find_node(map, key);
    if (node == NULL)
        return false;

    *out_value_addr = node->m_value;
    return true;
}

WOORT_NODISCARD bool woort_ordermap_contains(
    woort_OrderMap* map,
    const void* key)
{
    return _woort_ordermap_find_node(map, key) != NULL;
}

WOORT_NODISCARD bool woort_ordermap_remove(
    woort_OrderMap* map,
    const void* key)
{
    woort_OrderMapNode* const z = _woort_ordermap_find_node(map, key);
    if (z == NULL)
        return false;

    woort_OrderMapNode* y = z;
    woort_OrderMapColor y_original_color = y->m_color;
    woort_OrderMapNode* x;

    if (z->m_left == map->m_nil)
    {
        /* z 没有左孩子 */
        x = z->m_right;
        _woort_ordermap_transplant(map, z, z->m_right);
    }
    else if (z->m_right == map->m_nil)
    {
        /* z 没有右孩子 */
        x = z->m_left;
        _woort_ordermap_transplant(map, z, z->m_left);
    }
    else
    {
        /* z 有两个孩子：找到后继节点（右子树中的最小节点） */
        y = _woort_ordermap_subtree_min(map, z->m_right);
        y_original_color = y->m_color;
        x = y->m_right;

        if (y->m_parent == z)
        {
            x->m_parent = y;
        }
        else
        {
            _woort_ordermap_transplant(map, y, y->m_right);
            y->m_right = z->m_right;
            y->m_right->m_parent = y;
        }

        _woort_ordermap_transplant(map, z, y);
        y->m_left = z->m_left;
        y->m_left->m_parent = y;
        y->m_color = z->m_color;
    }

    if (y_original_color == WOORT_ORDERMAP_BLACK)
        _woort_ordermap_delete_fixup(map, x);

    _woort_ordermap_free_node(map, z);

    --map->m_size;
    return true;
}

void woort_ordermap_clear(woort_OrderMap* map)
{
    _woort_ordermap_clear_subtree(map, map->m_root);
    map->m_root = map->m_nil;
    map->m_size = 0;
}

WOORT_NODISCARD bool woort_ordermap_foreach(
    woort_OrderMap* map,
    woort_OrderMapForEachCallback callback,
    void* user_data)
{
    return _woort_ordermap_inorder_foreach(
        map, map->m_root, callback, user_data);
}

WOORT_NODISCARD bool woort_ordermap_is_empty(woort_OrderMap* map)
{
    return map->m_size == 0;
}

WOORT_NODISCARD bool woort_ordermap_min(
    woort_OrderMap* map,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr)
{
    if (map->m_root == map->m_nil)
        return false;

    woort_OrderMapNode* const node =
        _woort_ordermap_subtree_min(map, map->m_root);

    if (out_key != NULL)
        memcpy(out_key, _woort_ordermap_node_key(node), map->m_key_size);

    *out_value_addr = node->m_value;
    return true;
}

WOORT_NODISCARD bool woort_ordermap_max(
    woort_OrderMap* map,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr)
{
    if (map->m_root == map->m_nil)
        return false;

    woort_OrderMapNode* x = map->m_root;
    while (x->m_right != map->m_nil)
        x = x->m_right;

    if (out_key != NULL)
        memcpy(out_key, _woort_ordermap_node_key(x), map->m_key_size);

    *out_value_addr = x->m_value;
    return true;
}

WOORT_NODISCARD bool woort_ordermap_lower_bound(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr)
{
    woort_OrderMapNode* const node = _woort_ordermap_lower_bound_node(map, key);
    if (node == NULL)
        return false;

    if (out_key != NULL)
        memcpy(out_key, _woort_ordermap_node_key(node), map->m_key_size);

    *out_value_addr = node->m_value;
    return true;
}

WOORT_NODISCARD bool woort_ordermap_upper_bound(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr)
{
    woort_OrderMapNode* const node = _woort_ordermap_upper_bound_node(map, key);
    if (node == NULL)
        return false;

    if (out_key != NULL)
        memcpy(out_key, _woort_ordermap_node_key(node), map->m_key_size);

    *out_value_addr = node->m_value;
    return true;
}

WOORT_NODISCARD bool woort_ordermap_find_le(
    woort_OrderMap* map,
    const void* key,
    /* OPTIONAL */ void* out_key,
    void** out_value_addr)
{
    woort_OrderMapNode* const node = _woort_ordermap_find_le_node(map, key);
    if (node == NULL)
        return false;

    if (out_key != NULL)
        memcpy(out_key, _woort_ordermap_node_key(node), map->m_key_size);

    *out_value_addr = node->m_value;
    return true;
}
