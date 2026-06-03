#pragma once

/*
woort_util.h
*/

#include "woort_diagnosis.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

WOORT_NODISCARD size_t woort_util_abs_diff(
    size_t a,
    size_t b);

WOORT_NODISCARD size_t woort_util_ptr_hash(
    const void* ptr_addr);

WOORT_NODISCARD size_t woort_hash_string(
    const char* str,
    size_t len);

WOORT_NODISCARD bool woort_util_ptr_equal(
    const void* ptr_a_addr,
    const void* ptr_b_addr);

/*
 * hashmap 回调：对 const char* 字符串内容做哈希。
 * key 指向 const char*（即 const char* const*）。
 */
WOORT_NODISCARD size_t woort_util_cstr_hash(const void* key);

/*
 * hashmap 回调：对 const char* 字符串内容做相等比较（strcmp）。
 * key 指向 const char*（即 const char* const*）。
 */
WOORT_NODISCARD bool woort_util_cstr_equal(
    const void* key1,
    const void* key2);

WOORT_NODISCARD /* OPTIONAL */ char* woort_dupstr_with_format_v(
    const char* format, va_list args);
