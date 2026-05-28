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

/**
 * @brief Function boundary descriptor for mapping bytecode offsets to function names.
 *
 * Entries are sorted by m_offset_begin in ascending order.
 * A bytecode offset belongs to a function if offset >= m_offset_begin
 * and offset < m_offset_begin + m_code_length.
 */
typedef struct woort_FunctionBoundary
{
    uint32_t m_offset_begin;           /**< @brief Bytecode offset where the function starts. */
    uint32_t m_code_length;            /**< @brief Length of the function's bytecode in words. */
    /* OPTIONAL */ const char* m_name; /**< @brief Function name (may be NULL for anonymous). */

} woort_FunctionBoundary;

/**
 * @brief Debug information for a local variable, mapping name to stack offset.
 *
 * Resolved during woort_IRCompiler_finish() from the compile-time
 * woort_IRFunction_record_local_var() records.
 */
typedef struct woort_LocalVarDebugInfo
{
    /* OPTIONAL */ const char* m_name; /**< @brief Variable name (interned into CodeEnv string pool). */
    uint32_t m_function_offset;        /**< @brief Bytecode offset of the containing function. */
    int32_t m_stack_offset;            /**< @brief Stack offset of the variable within its function frame. */

} woort_LocalVarDebugInfo;

/**
 * @brief Debug information for a static variable, mapping name to static index.
 *
 * Resolved during woort_IRCompiler_finish() from the compile-time
 * woort_IRCompiler_record_static_var() records.
 */
typedef struct woort_StaticVarDebugInfo
{
    /* OPTIONAL */ const char* m_name; /**< @brief Variable name (interned into CodeEnv string pool). */
    woort_IRStaticIndex m_static_idx;  /**< @brief Index into the static data area. */

} woort_StaticVarDebugInfo;

/* ========================================================================
 * 常量池类型记录 —— 用于二进制序列化/反序列化
 * ======================================================================== */

/*
 * 常量池条目的类型标签。
 * 保存/恢复二进制时需要知道每个常量槽存的是什么类型的值，
 * 因为 woort_Value 是一个无标签联合体。
 */
typedef enum woort_ConstRecordType
{
    WOORT_CONST_TYPE_NIL = 0,             /* 未初始化（零值） */
    WOORT_CONST_TYPE_INT,                 /* m_integer */
    WOORT_CONST_TYPE_REAL,                /* m_real */
    WOORT_CONST_TYPE_STRING,              /* m_string */
    WOORT_CONST_TYPE_SCRIPT_FUNC,         /* m_script_function */
    WOORT_CONST_TYPE_EXTERN_FUNC,         /* m_native_function */
    WOORT_CONST_TYPE_SCRIPT_CLOSURE,      /* m_closure (指向脚本函数) */
    WOORT_CONST_TYPE_EXTERN_CLOSURE,      /* m_closure (指向原生函数) */
    WOORT_CONST_TYPE_BOX_INT,             /* m_dynamic (boxed int) */
    WOORT_CONST_TYPE_BOX_REAL,            /* m_dynamic (boxed real) */
    WOORT_CONST_TYPE_BOX_BOOL,            /* m_dynamic (boxed bool) */
    WOORT_CONST_TYPE_STRUCT,              /* m_struct */
} woort_ConstRecordType;

/*
 * 常量池条目的元数据。
 * 对于 extern 函数/闭包，还需要记录库名和函数名以便恢复时重新解析。
 */
typedef struct woort_ConstRecord
{
    woort_ConstRecordType m_type;

    /*
     * extern 函数/闭包专用字段。
     * 所有权属于 CodeEnv（GC destroy 时释放）。
     */
    /* OPTIONAL */ char* m_lib_name;      /* 库名（extern 函数/闭包才有） */
    /* OPTIONAL */ char* m_func_name;     /* 函数名（extern 函数/闭包才有） */

} woort_ConstRecord;

WOORT_NODISCARD bool woort_CodeEnv_bootup(void);
void woort_CodeEnv_shutdown(void);
void woort_CodeEnv_drop_all(void);

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

    /*
     * 常量池元数据（与 m_data_begin 并行）。
     * 每个条目记录该常量槽的类型和 extern 解析信息。
     * 长度与 m_data_count 一致。
     * CodeEnv 拥有所有权，GC destroy 时释放。
     */
    woort_Vector /* woort_ConstRecord */ m_const_records;

    /*
     * 局部变量调试信息（名称 -> 栈偏移量）。
     * m_name 指针指向 m_srcloc_string_pool 中的字符串。
     * CodeEnv 拥有所有权，GC destroy 时释放。
     */
    woort_Vector /* woort_LocalVarDebugInfo */ m_local_var_debug_info;

    /*
     * 静态变量调试信息（名称 -> 静态存储索引）。
     * m_name 指针指向 m_srcloc_string_pool 中的字符串。
     * CodeEnv 拥有所有权，GC destroy 时释放。
     */
    woort_Vector /* woort_StaticVarDebugInfo */ m_static_var_debug_info;

    size_t m_constant_count;
    size_t m_data_count;
    woort_Value m_data_begin[];
};
_Static_assert(offsetof(woort_CodeEnv, m_gc_unit) == 0, 
    "woort_GCUnit must be head of woort_CodeEnv.");

WOORT_NODISCARD bool woort_CodeEnv_create(
    const woort_Bytecode* bytecodes,
    size_t bytecodes_count,
    size_t constant_storage_count,
    size_t static_storage_count,
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
 * 将编译器收集的调试信息（局部变量 + 静态变量）转移到 CodeEnv。
 * CodeEnv 会复制所有名称字符串到其字符串池，拥有完全的所有权。
 *
 * local_var_debug: woort_Vector<woort_LocalVarDebugInfo>
 * static_var_debug: woort_Vector<woort_StaticVarDebugInfo>
 */
void woort_CodeEnv_set_debug_info(
    woort_CodeEnv* env,
    const woort_Vector* local_var_debug,
    const woort_Vector* static_var_debug);

/*
 * 记录一个常量槽的类型信息。
 * 在 woort_CodeEnv_set_const_* 之后调用，用于二进制序列化。
 * 对于 extern 函数/闭包，同时记录库名和函数名。
 *
 * @param env        已锁定的 CodeEnv。
 * @param cidx       常量池索引。
 * @param type       常量类型。
 * @param lib_name   库名（extern 函数/闭包需要，否则传 NULL）。
 * @param func_name  函数名（extern 函数/闭包需要，否则传 NULL）。
 */
WOORT_NODISCARD bool woort_CodeEnv_set_const_record(
    woort_CodeEnv* env,
    woort_IRConstantIndex cidx,
    woort_ConstRecordType type,
    /* OPTIONAL */ const char* lib_name,
    /* OPTIONAL */ const char* func_name);

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

typedef bool (*woort_CodeEnv_ForeachCallback)(woort_CodeEnv* cenv, void* user_data);

void woort_CodeEnv_foreach(
    woort_CodeEnv_ForeachCallback callback,
    void* user_data);
