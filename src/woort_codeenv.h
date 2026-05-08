#pragma once

/*
woort_codeenv.h
*/

#include "woort.h"

#include "woort_diagnosis.h"
#include "woort_opcode_formal.h"
#include "woort_value.h"
#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_gc_units.h"
#include "woort_hashmap.h"
#include "woort_ir_srcloc.h"
#include "woort_threads.h"

#include <stdbool.h>

WOORT_NODISCARD bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);

struct woort_CodeEnv {
    woort_GCUnit m_gc_unit;

    bool m_hold;

    woort_Mutex* m_mutex;

    const woort_Bytecode* m_code_begin;
    const woort_Bytecode* m_code_end;

    woort_HashMap /* woort_Bytecode*, woort_Bytecode */
        m_trap_records;

    /* === 外部常量注册表 === */
    /*
     * 名称字符串 -> woort_IRConstantIndex 的映射表。
     * key 是 malloc 分配的字符串副本（由 register 拥有，destroy 时释放）。
     * value 是 woort_IRConstantIndex（按值存储）。
     */
    woort_HashMap /* char* -> woort_IRConstantIndex */
        m_extern_constants;

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

    /* === 函数边界表 === */
    /*
     * 字节码偏移 -> 函数名 的映射表。
     * 按 m_offset_begin 升序排列，使用二分查找查询。
     * 由 woort_CodeEnv_set_source_maps() 设置。
     * CodeEnv 拥有所有权（m_name 指针指向 m_srcloc_string_pool 中的字符串）。
     */
    woort_Vector /* woort_FunctionBoundary */ m_function_boundaries;

    /* === 外部库句柄跟踪 === */
    /*
     * 与 CodeEnv 关联的外部动态库句柄列表。
     * 当 CodeEnv 被 GC 销毁时，所有关联库会被解除引用。
     */
    woort_Vector /* woort_Dylib* */ m_extern_libs;

    size_t m_data_count;
    woort_Value m_data_begin[];
};
_Static_assert(offsetof(woort_CodeEnv, m_gc_unit) == 0, 
    "woort_GCUnit must be head of woort_CodeEnv.");

WOORT_NODISCARD bool woort_CodeEnv_create(
    const woort_Bytecode* bytecodes,
    size_t bytecodes_count,
    size_t constant_and_static_storage_count,
    woort_CodeEnv** out_code_env);

WOORT_NODISCARD bool woort_CodeEnv_find(
    const woort_Bytecode* addr, woort_CodeEnv** out_code_env);

void woort_CodeEnv_GC_mark_all_envs(void);

/*
 * 将编译器收集的源码映射数据转移到 CodeEnv。
 * CodeEnv 会复制所有映射条目和路径字符串，拥有完全的所有权。
 *
 * function_source_map: woort_Vector<woort_Function_SourceMap>
 *   每个条目包含 IR 函数指针（含名称、偏移、长度）和源码映射条目。
 */
void woort_CodeEnv_set_source_maps(
    woort_CodeEnv* env,
    const woort_Vector* function_source_map);

/*
 * 将外部库句柄关联到 CodeEnv。
 * 当 CodeEnv 被 GC 销毁时，关联的库将自动被 woort_dylib_unload(WOORT_DYLIB_UNREF) 解除引用。
 * lib 的引用计数会被增加。
 *
 * @return true on success, false on out-of-memory.
 */
WOORT_NODISCARD bool woort_CodeEnv_add_extern_lib(
    woort_CodeEnv* env,
    woort_Dylib* lib);
