#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdarg.h>
#include <assert.h>

#include "woomem.h"
#include "woort_gc_string.h"
#include "woort_util.h"
#include "woort_log.h"

const woort_GCUnitProxy WOORT_GCSTRING_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD const woort_GCString* woort_GCString_make_string_for_env_constant(
    woort_CodeEnv* cenv, const char* str, size_t len)
{
    woort_GCString* gcstr;

    do
    {
        gcstr = woomem_allocate_begin(
            sizeof(woort_GCString) + len + 1);

        if (gcstr != NULL)
            break;

        woort_CodeEnv_unlock(cenv);
        {
            _woort_GCUnit_alloc_failed();
        }
        woort_CodeEnv_lock(cenv);

    } while (true);

    gcstr->m_gc_unit.m_proxy = &WOORT_GCSTRING_UNIT_PROXY;
    gcstr->m_length = len;

    memcpy(gcstr->m_content, str, len);
    gcstr->m_content[len] = '\0';

    woort_GCUnit_init_delay_alloc(O, gcstr);

    return gcstr;
}

WOORT_NODISCARD const woort_GCString* woort_GCString_make_string(const char* str, size_t len)
{
    woort_GCString* const gcstr = 
        woort_GCUnit_alloc_delay_init(sizeof(woort_GCString) + len + 1);

    gcstr->m_gc_unit.m_proxy = &WOORT_GCSTRING_UNIT_PROXY;
    gcstr->m_length = len;

    memcpy(gcstr->m_content, str, len);
    gcstr->m_content[len] = '\0';

    woort_GCUnit_init_delay_alloc(O, gcstr);

    return gcstr;
}

WOORT_NODISCARD const woort_GCString* woort_GCString_make_format_va(
    const char* fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    const int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    assert(len >= 0);

    woort_GCString* const gcstr =
        woort_GCUnit_alloc_delay_init(sizeof(woort_GCString) + (size_t)len + 1);

    gcstr->m_gc_unit.m_proxy = &WOORT_GCSTRING_UNIT_PROXY;
    gcstr->m_length = (size_t)len;

    (void)vsnprintf(gcstr->m_content, (size_t)len + 1, fmt, args);

    woort_GCUnit_init_delay_alloc(O, gcstr);

    return gcstr;
}

WOORT_NODISCARD const woort_GCString* woort_GCString_add_string(const woort_GCString* a, const woort_GCString* b)
{
    woort_GCString* const gcstr = 
        woort_GCUnit_alloc_delay_init(sizeof(woort_GCString) + a->m_length + b->m_length + 1);

    gcstr->m_gc_unit.m_proxy = &WOORT_GCSTRING_UNIT_PROXY;
    gcstr->m_length = a->m_length + b->m_length;

    memcpy(gcstr->m_content, a->m_content, a->m_length);
    memcpy(gcstr->m_content + a->m_length, b->m_content, b->m_length);
    gcstr->m_content[a->m_length + b->m_length] = '\0';

    woort_GCUnit_init_delay_alloc(O, gcstr);

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
    return woort_hash_string(str->m_content, str->m_length);
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

WOORT_NODISCARD woort_Int woort_GCString_to_integer(const woort_GCString* str)
{
    return (woort_Int)strtoll(str->m_content, NULL, 0);
}

WOORT_NODISCARD woort_Real woort_GCString_to_real(const woort_GCString* str)
{
    return (woort_Real)strtod(str->m_content, NULL);
}