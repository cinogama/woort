#pragma once

/*
woort_serialize.h
*/

#include "woort.h"
#include "woort_gc_map.h"
#include "woort_gc_vec.h"
#include "woort_gc_string.h"
#include "woort_value.h"
#include "woort_vector.h"
#include "woort_hashmap.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * 内部辅助：将 DynBox 序列化到 woort_Vector 缓冲区。
 * visited_set: 已访问的 GC Unit 集合（woort_HashMap），用于循环检测。
 * depth: 当前递归深度，用于缩进。
 * flags: woort_SerializeFlag 位掩码。
 * 返回 true 成功，false 失败。
 */
WOORT_NODISCARD bool _woort_serialize_dynbox_to_buf(
    woort_DynBox box,
    woort_Vector* buf,
    woort_HashMap* visited_set,
    int depth,
    uint32_t flags);

/*
 * 内部辅助：将 DynBox 以调试友好的格式序列化到 woort_Vector 缓冲区。
 * 与 _woort_serialize_dynbox_to_buf 不同，此函数输出人类可读的调试信息，
 * 包括 struct/closure/gchandle 的详细描述以及原始 box 值。
 * is_fuzzy: 如果给定的 box 可能不是一个真正的 box 值，同时显示原始 i64/f64 值。
 * 返回 true 成功，false 失败。
 */
WOORT_NODISCARD bool _woort_serialize_dynbox_to_buf_for_debug(
    woort_DynBox box,
    woort_Vector* buf,
    woort_HashMap* visited_set,
    int depth,
    bool show_raw);

/*
内部 impl：将 GCMap 序列化为字符串，写入 *dst。
src_val->m_map 作为源。
*/
WOORT_NODISCARD bool _woort_serialize_map_impl(
    woort_Value* dst,
    const woort_Value* src_val,
    uint32_t flags);

/*
内部 impl：将 GCVec 序列化为字符串，写入 *dst。
src_val->m_vec 作为源。
*/
WOORT_NODISCARD bool _woort_serialize_vec_impl(
    woort_Value* dst,
    const woort_Value* src_val,
    uint32_t flags);

/*
内部辅助：跳过空白字符。
*/
const char* _woort_deserialize_skip_whitespace(const char* p);

/*
内部辅助：从字符串解析一个 DynBox 值。
*/
WOORT_NODISCARD bool _woort_deserialize_dynbox_from_str(
    const char** p,
    woort_DynBox* out_box);

/*
内部辅助：解析映射字面量 { ... }。
*/
WOORT_NODISCARD bool _woort_deserialize_map_impl(
    const char** p,
    woort_DynBox* out_gcmap);

/*
内部辅助：解析数组字面量 [ ... ]。
*/
WOORT_NODISCARD bool _woort_deserialize_vec_impl(
    const char** p,
    woort_DynBox* out_gcvec);
