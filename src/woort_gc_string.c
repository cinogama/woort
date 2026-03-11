#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "woomem.h"
#include "woort_gc_string.h"

const woort_GCUnitProxy g_gcstring_unit_proxy = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD const woort_GCString* woort_GCString_make_string(const char* str, size_t len)
{
    woort_GCString* const gcstr = 
        woort_GCUnit_alloc_attrib(O, sizeof(woort_GCString) + len + 1);

    gcstr->m_gc_unit.m_proxy = &g_gcstring_unit_proxy;
    gcstr->m_length = len;

    memcpy(gcstr->m_content, str, len);
    gcstr->m_content[len] = '\0';

    return gcstr;
}

WOORT_NODISCARD const woort_GCString* woort_GCString_add_string(const woort_GCString* a, const woort_GCString* b)
{
    woort_GCString* const gcstr = 
        woort_GCUnit_alloc_attrib(O, sizeof(woort_GCString) + a->m_length + b->m_length + 1);

    gcstr->m_gc_unit.m_proxy = &g_gcstring_unit_proxy;
    gcstr->m_length = a->m_length + b->m_length;

    memcpy(gcstr->m_content, a->m_content, a->m_length);
    memcpy(gcstr->m_content + a->m_length, b->m_content, b->m_length);
    gcstr->m_content[a->m_length + b->m_length] = '\0';

    return gcstr;
}

WOORT_NODISCARD int woort_GCString_compare(const woort_GCString* a, const woort_GCString* b)
{
    const size_t min_len = a->m_length < b->m_length ? a->m_length : b->m_length;
    const int cmp_result = memcmp(a->m_content, b->m_content, min_len);

    if (cmp_result != 0)
        return cmp_result;

    // If prefix matches, shorter string is smaller
    if (a->m_length < b->m_length)
        return -1;
    if (a->m_length > b->m_length)
        return 1;
    return 0;
}

WOORT_NODISCARD size_t woort_GCString_hash(const woort_GCString* str)
{
    // FNV-1a hash algorithm
    size_t hash = 14695981039346656037ULL; // FNV offset basis
    const unsigned char* ptr = (const unsigned char*)str->m_content;
    const unsigned char* end = ptr + str->m_length;

    while (ptr < end)
    {
        hash ^= (size_t)*ptr++;
        hash *= 1099511628211ULL; // FNV prime
    }

    return hash;
}

WOORT_NODISCARD const woort_GCString* woort_GCString_from_integer(woort_Int value)
{
    // int64_t 最大是 -9223372036854775808，需要最多 21 字节
    char buffer[32];
    const int len = snprintf(buffer, sizeof(buffer), "%" PRId64, value);

    return woort_GCString_make_string(buffer, (size_t)len);
}

WOORT_NODISCARD const woort_GCString* woort_GCString_from_real(woort_Real value)
{
    // double 最大精度约 17 位，加上符号、小数点、指数部分等，需要足够空间
    // 使用 %.17g 格式保证精度，最多需要约 24 字节
    char buffer[64];
    const int len = snprintf(buffer, sizeof(buffer), "%.17g", value);

    return woort_GCString_make_string(buffer, (size_t)len);
}

WOORT_NODISCARD bool woort_GCString_to_integer(const woort_GCString* str, woort_Int* out_value)
{
    if (str == NULL || str->m_length == 0 || out_value == NULL)
        return false;

    // 使用 sscanf 解析整数
    // 支持十进制、十六进制(0x)、八进制(0)格式
    char* end_ptr = NULL;
    const char* start = str->m_content;

    // 跳过前导空白
    while (start < str->m_content + str->m_length && (*start == ' ' || *start == '\t'))
        ++start;

    if (start >= str->m_content + str->m_length)
        return false;

    const woort_Int result = strtoll(start, &end_ptr, 0);

    // 检查是否解析成功
    if (end_ptr == start)
        return false;

    // 跳过尾随空白
    while (end_ptr < str->m_content + str->m_length && (*end_ptr == ' ' || *end_ptr == '\t'))
        ++end_ptr;

    // 确保整个字符串都被解析（除了空白）
    if (end_ptr != str->m_content + str->m_length)
        return false;

    *out_value = result;
    return true;
}

WOORT_NODISCARD bool woort_GCString_to_real(const woort_GCString* str, woort_Real* out_value)
{
    if (str == NULL || str->m_length == 0 || out_value == NULL)
        return false;

    // 使用 strtod 解析浮点数
    char* end_ptr = NULL;
    const char* start = str->m_content;

    // 跳过前导空白
    while (start < str->m_content + str->m_length && (*start == ' ' || *start == '\t'))
        ++start;

    if (start >= str->m_content + str->m_length)
        return false;

    const woort_Real result = strtod(start, &end_ptr);

    // 检查是否解析成功
    if (end_ptr == start)
        return false;

    // 跳过尾随空白
    while (end_ptr < str->m_content + str->m_length && (*end_ptr == ' ' || *end_ptr == '\t'))
        ++end_ptr;

    // 确保整个字符串都被解析（除了空白）
    if (end_ptr != str->m_content + str->m_length)
        return false;

    *out_value = result;
    return true;
}