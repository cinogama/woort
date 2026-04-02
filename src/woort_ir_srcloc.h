#pragma once

/*
 * woort_ir_srcloc.h
 *
 * IR 源码位置信息支持。
 *
 * 包含：
 *   - woort_SourceLocation: 源码位置（文件路径 + 起止行列号）
 *   - woort_StringPool: 字符串池（intern 语义，去重，统一管理生命周期）
 *   - woort_SourceLocationStack: 源码位置栈（编译期 push/pop 控制当前 IR 的源码信息）
 *   - woort_SourceMap: 字节码偏移 <-> 源码位置 映射表（边界记录 + 二分查找）
 */

#include "woort.h"

#include "woort_diagnosis.h"
#include "woort_vector.h"
#include "woort_hashmap.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 无源码位置的哨兵值 */
#define WOORT_SRCLOC_INVALID_INDEX UINT32_MAX

/* 比较两个 SourceLocation 是否相同（m_filepath 使用指针比较） */
WOORT_NODISCARD bool woort_SourceLocation_equal(
    const woort_SourceLocation* a,
    const woort_SourceLocation* b);

/* ========== 字符串池 ========== */

/*
 * 字符串池（intern 语义）：
 * 相同内容的字符串只存储一份，intern 返回相同的指针。
 * 所有字符串由池管理生命周期，在 deinit 时统一释放。
 */
typedef struct woort_StringPool
{
    woort_HashMap /* const char* -> const char* */ m_map;
    woort_Vector /* char* */ m_strings;

} woort_StringPool;

void woort_StringPool_init(woort_StringPool* pool);
void woort_StringPool_deinit(woort_StringPool* pool);

/*
 * intern 一个字符串到池中。
 * 如果池中已有相同内容的字符串，返回已有的指针。
 * 否则复制并存储，返回新指针。
 * 返回 NULL 表示 OOM。
 */
WOORT_NODISCARD /* OPTIONAL */ const char* woort_StringPool_intern(
    woort_StringPool* pool, const char* str);

/* ========== 源码位置栈 ========== */

/*
 * 编译期源码位置栈。
 * 调用者通过 push/pop 控制当前 IR 指令关联的源码位置。
 * 栈为空时表示无源码信息。
 */
typedef struct woort_SourceLocationStack
{
    woort_Vector /* woort_SourceLocation */ m_stack;

} woort_SourceLocationStack;

void woort_SourceLocationStack_init(woort_SourceLocationStack* stack);
void woort_SourceLocationStack_deinit(woort_SourceLocationStack* stack);

/* 推入一个源码位置 */
WOORT_NODISCARD bool woort_SourceLocationStack_push(
    woort_SourceLocationStack* stack,
    const woort_SourceLocation* loc);

/* 弹出栈顶的源码位置 */
void woort_SourceLocationStack_pop(woort_SourceLocationStack* stack);

/* 获取栈顶的源码位置。栈为空时返回 NULL。 */
WOORT_NODISCARD /* OPTIONAL */ const woort_SourceLocation*
    woort_SourceLocationStack_top(const woort_SourceLocationStack* stack);

/* 栈是否为空 */
WOORT_NODISCARD bool woort_SourceLocationStack_empty(
    const woort_SourceLocationStack* stack);

/* ========== 源码映射表 ========== */

/*
 * 映射表条目：字节码偏移 -> 源码位置。
 * 按 m_bytecode_offset 升序排列。
 */
typedef struct woort_SourceMap_Entry
{
    uint32_t m_bytecode_offset;     /* 相对于 CodeEnv.m_code_begin 的偏移 */
    woort_SourceLocation m_location;

} woort_SourceMap_Entry;

/*
 * 源码映射表。
 * 包含一组按字节码偏移升序排列的条目。
 * 查询时使用二分查找。
 *
 * 映射表拥有自身的 m_entries 数组的所有权。
 * 映射表中的 m_filepath 指针指向 CodeEnv 持有的字符串副本。
 */
typedef struct woort_SourceMap
{
    /* OPTIONAL */ woort_SourceMap_Entry* m_entries;
    uint32_t m_entry_count;

} woort_SourceMap;

/*
 * 根据字节码偏移查找最匹配的源码位置。
 * 在映射表中二分查找 <= bytecode_offset 的最大条目。
 * 返回 true 表示找到了匹配的源码位置。
 */
WOORT_NODISCARD bool woort_SourceMap_find_by_offset(
    const woort_SourceMap* map,
    uint32_t bytecode_offset,
    woort_SourceLocation* out_location);

/*
 * 根据源码位置（文件路径 + 行号）查找最匹配的字节码偏移。
 * 在映射表中线性扫描，找到文件路径匹配且行号 >= line 的最小条目。
 * filepath 必须是 intern 过的指针（使用指针比较）。
 * 返回 true 表示找到了匹配条目。
 */
WOORT_NODISCARD bool woort_SourceMap_find_by_line(
    const woort_SourceMap* map,
    const char* filepath,
    uint32_t line,
    uint32_t* out_bytecode_offset);
