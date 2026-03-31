#pragma once

/*
woort_codeenv.h
*/

#include "woort_diagnosis.h"
#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_gc_units.h"
#include "woort_hashmap.h"
#include "woort_ir_srcloc.h"

#include <stdbool.h>

WOORT_NODISCARD bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);

typedef struct woort_CodeEnv {
    woort_GCUnit m_gc_unit;

    bool m_hold;

    const woort_Bytecode* m_code_begin;
    const woort_Bytecode* m_code_end;

    woort_HashMap /* woort_Bytecode*, woort_Bytecode */
        m_trap_records;

    /* === 源码映射 === */

    /*
     * 合并的源码映射表，覆盖所有函数。
     * 由 woort_CodeEnv_set_source_maps() 设置。
     * CodeEnv 拥有映射数据的所有权（在 GC destroy 时释放）。
     * 如果无源码信息，m_source_map.m_entries 为 NULL 且 m_entry_count 为 0。
     */
    woort_SourceMap m_source_map;

    /*
     * 源码映射中路径字符串的存储池。
     * CodeEnv 拥有所有权，GC destroy 时释放。
     */
    woort_StringPool m_srcloc_string_pool;

    woort_Value m_data_begin[];

} woort_CodeEnv;
_Static_assert(offsetof(woort_CodeEnv, m_gc_unit) == 0, 
    "woort_GCUnit must be head of woort_CodeEnv.");

WOORT_NODISCARD bool woort_CodeEnv_create(
    const woort_Bytecode* bytecodes,
    size_t bytecodes_count,
    size_t constant_and_static_storage_count,
    woort_CodeEnv** out_code_env);

void woort_CodeEnv_drop(
    woort_CodeEnv* code_env);

WOORT_NODISCARD bool woort_CodeEnv_find(
    const woort_Bytecode* addr, woort_CodeEnv** out_code_env);

void woort_CodeEnv_GC_mark_all_envs(void);

WOORT_NODISCARD bool woort_CodeEnv_set_trap(woort_Bytecode* code);

/*
 * 将编译器收集的源码映射数据转移到 CodeEnv。
 * CodeEnv 会复制所有映射条目和路径字符串，拥有完全的所有权。
 *
 * per_func_entries: 每个函数对应一个 woort_Vector<woort_SourceMap_Entry>
 * func_count: 函数数量
 */
void woort_CodeEnv_set_source_maps(
    woort_CodeEnv* env,
    const woort_Vector* per_func_entries,
    uint32_t func_count);

/*
 * 根据字节码偏移查找最匹配的源码位置。
 * bytecode_offset 是相对于 m_code_begin 的偏移。
 * 返回 true 表示找到了匹配的源码位置。
 */
WOORT_NODISCARD bool woort_CodeEnv_find_srcloc_by_offset(
    const woort_CodeEnv* env,
    uint32_t bytecode_offset,
    woort_SourceLocation* out_location);

/*
 * 根据源码位置（文件路径 + 行号）查找最匹配的字节码偏移。
 * filepath 可以是任意字符串指针（内部使用 strcmp 比较）。
 * 返回 true 表示找到了匹配条目。
 */
WOORT_NODISCARD bool woort_CodeEnv_find_offset_by_srcloc(
    const woort_CodeEnv* env,
    const char* filepath,
    uint32_t line,
    uint32_t* out_bytecode_offset);
