/*
 * woort_ir_srcloc.c
 *
 * IR 源码位置信息支持实现。
 * 包含：字符串池（intern 去重）、源码位置栈、映射表查询（二分查找）。
 */

#include "woort_ir_srcloc.h"

#include "woort_util.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

 /* ========================================================================
  * woort_SourceLocation
  * ======================================================================== */

WOORT_NODISCARD bool woort_SourceLocation_equal(
    const woort_SourceLocation* a,
    const woort_SourceLocation* b)
{
    /* m_filepath 使用指针比较（intern 语义保证同一内容同一指针） */
    return a->m_filepath == b->m_filepath
        && a->m_begin_line == b->m_begin_line
        && a->m_begin_column == b->m_begin_column
        && a->m_end_line == b->m_end_line
        && a->m_end_column == b->m_end_column;
}

/* ========================================================================
 * woort_StringPool
 * ======================================================================== */

void woort_StringPool_init(woort_StringPool* pool)
{
    woort_hashmap_init(
        &pool->m_map,
        sizeof(const char*),   /* key: const char* */
        sizeof(const char*),   /* value: const char* (same pointer) */
        &woort_util_cstr_hash,
        &woort_util_cstr_equal);
    woort_vector_init(&pool->m_strings, sizeof(char*));
}

void woort_StringPool_deinit(woort_StringPool* pool)
{
    /* 释放所有已分配的字符串 */
    for (size_t i = 0; i < pool->m_strings.m_size; ++i)
    {
        char** str_ptr = (char**)woort_vector_at(&pool->m_strings, i);
        free(*str_ptr);
    }
    woort_vector_deinit(&pool->m_strings);
    woort_hashmap_deinit(&pool->m_map);
}

WOORT_NODISCARD /* OPTIONAL */ const char* woort_StringPool_intern(
    woort_StringPool* pool, /* OPTIONAL */ const char* str)
{
    if (str == NULL)
        return NULL;

    /* 先尝试查找已有的 */
    void* value_addr;
    if (woort_hashmap_find(&pool->m_map, &str, &value_addr))
    {
        /* 已存在，返回已有的指针 */
        return *(const char**)value_addr;
    }

    /* 不存在，需要复制字符串并插入 */
    size_t len = strlen(str);
    char* dup = (char*)malloc(len + 1);
    if (dup == NULL)
        return NULL;
    memcpy(dup, str, len + 1);

    /* 将 dup 记录到 strings vector 以便在 deinit 时释放 */
    if (!woort_vector_push_back(&pool->m_strings, 1, &dup))
    {
        free(dup);
        return NULL;
    }

    /* 用 dup 作为 key 和 value 插入到 hashmap */
    woort_hashmap_Result ins_result = woort_hashmap_insert(
        &pool->m_map, &dup, &dup);

    if (ins_result != WOORT_HASHMAP_RESULT_OK)
    {
        /* 回滚：从 vector 中移除最后一个条目并释放 */
        pool->m_strings.m_size--;
        free(dup);
        return NULL;
    }

    return dup;
}

/* ========================================================================
 * woort_SourceLocationStack
 * ======================================================================== */

void woort_SourceLocationStack_init(woort_SourceLocationStack* stack)
{
    woort_vector_init(&stack->m_stack, sizeof(woort_SourceLocation));
}

void woort_SourceLocationStack_deinit(woort_SourceLocationStack* stack)
{
    woort_vector_deinit(&stack->m_stack);
}

WOORT_NODISCARD bool woort_SourceLocationStack_push(
    woort_SourceLocationStack* stack,
    const woort_SourceLocation* loc)
{
    return woort_vector_push_back(&stack->m_stack, 1, loc);
}

void woort_SourceLocationStack_pop(woort_SourceLocationStack* stack)
{
    assert(stack->m_stack.m_size > 0);
    stack->m_stack.m_size--;
}

WOORT_NODISCARD /* OPTIONAL */ const woort_SourceLocation*
woort_SourceLocationStack_top(const woort_SourceLocationStack* stack)
{
    if (stack->m_stack.m_size == 0)
        return NULL;

    return (const woort_SourceLocation*)woort_vector_at(
        (woort_Vector*)&stack->m_stack, stack->m_stack.m_size - 1);
}

WOORT_NODISCARD bool woort_SourceLocationStack_empty(
    const woort_SourceLocationStack* stack)
{
    return stack->m_stack.m_size == 0;
}

/* ========================================================================
 * woort_SourceMap 查询
 * ======================================================================== */

WOORT_NODISCARD bool woort_SourceMap_find_by_offset(
    const woort_SourceMap* map,
    uint32_t bytecode_offset,
    woort_SourceLocation* out_location)
{
    if (map == NULL || map->m_entries == NULL || map->m_entry_count == 0)
        return false;

    /*
     * 二分查找：找到 m_bytecode_offset <= bytecode_offset 的最大条目。
     * 即 upper_bound(bytecode_offset) - 1。
     */
    uint32_t lo = 0;
    uint32_t hi = map->m_entry_count;

    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        if (map->m_entries[mid].m_bytecode_offset <= bytecode_offset)
            lo = mid + 1;
        else
            hi = mid;
    }

    /* lo 现在是第一个 > bytecode_offset 的位置，lo - 1 是 <= 的最大位置 */
    if (lo == 0)
        return false; /* 所有条目的偏移都 > bytecode_offset */

    *out_location = map->m_entries[lo - 1].m_location;
    return true;
}

WOORT_NODISCARD bool woort_SourceMap_find_by_line(
    const woort_SourceMap* map,
    const char* filepath,
    uint32_t line,
    uint32_t* out_bytecode_offset)
{
    if (map == NULL || map->m_entries == NULL || map->m_entry_count == 0)
        return false;

    /*
     * 线性扫描，找到文件路径匹配且 m_begin_line >= line 的最小条目。
     * 如果没有 >= line 的，找 m_begin_line 最接近（<= line）的最大条目。
     */
    uint32_t best_ge_offset = 0;
    uint32_t best_ge_line = UINT32_MAX;
    bool found_ge = false;

    uint32_t best_le_offset = 0;
    uint32_t best_le_line = 0;
    bool found_le = false;

    for (uint32_t i = 0; i < map->m_entry_count; ++i)
    {
        const woort_SourceMap_Entry* entry = &map->m_entries[i];

        /* 使用指针比较（intern 语义） */
        if (entry->m_location.m_filepath != filepath)
            continue;

        if (entry->m_location.m_begin_line >= line)
        {
            if (!found_ge || entry->m_location.m_begin_line < best_ge_line)
            {
                best_ge_line = entry->m_location.m_begin_line;
                best_ge_offset = entry->m_bytecode_offset;
                found_ge = true;
            }
        }
        else
        {
            if (!found_le || entry->m_location.m_begin_line > best_le_line)
            {
                best_le_line = entry->m_location.m_begin_line;
                best_le_offset = entry->m_bytecode_offset;
                found_le = true;
            }
        }
    }

    if (found_ge)
    {
        *out_bytecode_offset = best_ge_offset;
        return true;
    }
    if (found_le)
    {
        *out_bytecode_offset = best_le_offset;
        return true;
    }

    return false;
}

/* 按字节码偏移升序回调所有覆盖指定行的条目 */
WOORT_NODISCARD static bool _woort_SourceMap_visit_covering(
    const woort_SourceMap* map,
    const char* filepath,
    uint32_t line,
    woort_SourceMap_OffsetCallback callback,
    void* user_data)
{
    bool visited = false;

    for (uint32_t i = 0; i < map->m_entry_count; ++i)
    {
        const woort_SourceMap_Entry* entry = &map->m_entries[i];

        /* 使用指针比较（intern 语义） */
        if (entry->m_location.m_filepath != filepath)
            continue;

        if (entry->m_location.m_begin_line == line)
            continue;

        visited = true;

        if (!callback(entry->m_bytecode_offset, user_data))
            break;
    }

    return visited;
}

WOORT_NODISCARD bool woort_SourceMap_foreach_by_line(
    const woort_SourceMap* map,
    const char* filepath,
    uint32_t line,
    woort_SourceMap_OffsetCallback callback,
    void* user_data)
{
    if (map == NULL || map->m_entries == NULL || map->m_entry_count == 0)
        return false;
    if (callback == NULL)
        return false;

    if (_woort_SourceMap_visit_covering(map, filepath, line, callback, user_data))
        return true;

    /*
     * 没有条目覆盖该行：回退到最近的有条目的行，
     * 优先取 >= line 的最小行，否则取 < line 的最大行。
     */
    uint32_t best_ge_line = UINT32_MAX;
    uint32_t best_le_line = 0;
    bool found_any = false;

    for (uint32_t i = 0; i < map->m_entry_count; ++i)
    {
        const woort_SourceMap_Entry* entry = &map->m_entries[i];

        /* 使用指针比较（intern 语义） */
        if (entry->m_location.m_filepath != filepath)
            continue;

        found_any = true;

        if (entry->m_location.m_begin_line >= line)
        {
            if (entry->m_location.m_begin_line < best_ge_line)
                best_ge_line = entry->m_location.m_begin_line;
        }
        else if (entry->m_location.m_begin_line > best_le_line)
        {
            best_le_line = entry->m_location.m_begin_line;
        }
    }

    if (best_ge_line != UINT32_MAX)
        line = best_ge_line;
    else if (found_any)
        line = best_le_line;
    else
        return false; /* 该文件无任何条目 */

    return _woort_SourceMap_visit_covering(map, filepath, line, callback, user_data);
}
