#include "woort_serialize.h"

#include "woort_vector.h"
#include "woort_gc.h"
#include "woort_utf8.h"
#include "woort_util.h"
#include "woomem.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

/* 循环检测最大容量 */
#define WOORT_VISITED_CAPACITY 256

/* ========================================================================
 * 内部工具：输出字符到缓冲区
 * ======================================================================== */

static bool _woort_append_char(woort_Vector* buf, char ch)
{
    return woort_vector_push_back(buf, 1, &ch);
}

static bool _woort_append_str(woort_Vector* buf, const char* str)
{
    const size_t len = strlen(str);
    return woort_vector_push_back(buf, len, str);
}

/* 前向声明 */
static bool _woort_append_cstr(woort_Vector* buf, const char* str, size_t len)
{
    return woort_vector_push_back(buf, len, str);
}

/*
将整数写入缓冲区（使用 snprintf）。
*/
static bool _woort_append_int(woort_Vector* buf, woort_Int val)
{
    char tmp[32];
    const int n = snprintf(tmp, sizeof(tmp), "%" PRId64, (int64_t)val);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return false;
    return woort_vector_push_back(buf, (size_t)n, tmp);
}

/*
将实数写入缓冲区（使用 snprintf）。
*/
static bool _woort_append_real(woort_Vector* buf, woort_Real val)
{
    char tmp[64];
    const int n = snprintf(tmp, sizeof(tmp), "%.17g", val);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return false;
    return woort_vector_push_back(buf, (size_t)n, tmp);
}

/*
写入缩进（仅 PRETTY 模式）。
*/
static bool _woort_append_indent(woort_Vector* buf, int depth, uint32_t flags)
{
    if (!(flags & WOORT_SERIALIZE_FLAG_PRETTY))
        return true;

    if (!_woort_append_char(buf, '\n'))
        return false;

    for (int i = 0; i <= depth; ++i)
    {
        if (!_woort_append_cstr(buf, "    ", 4))
            return false;
    }
    return true;
}

/* ========================================================================
 * 循环检测
 * ======================================================================== */

/*
检查 gcunit 是否已经在 visited_gcunits 中。
*/
static bool _woort_is_visited(
    const woort_GCUnit** visited,
    size_t count,
    const woort_GCUnit* gcunit)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (visited[i] == gcunit)
            return true;
    }
    return false;
}

/* ========================================================================
 * 内部序列化：DynBox -> 字符串缓冲区
 * ======================================================================== */

WOORT_NODISCARD bool _woort_serialize_dynbox_to_buf(
    const woort_DynBox* box,
    woort_Vector* buf,
    const woort_GCUnit** visited_gcunits,
    size_t* visited_count,
    size_t visited_capacity,
    int depth,
    uint32_t flags)
{
    /* 检查是否是内联装箱类型（低 3 位非零） */
    if (box->m_boxed & 0b0111)
    {
        if (0b01 & box->m_boxed)
        {
            const double val = _woort_unbox_float64(box->m_boxed);
            return _woort_append_real(buf, val);
        }

        if (0 == (0b011 & (box->m_boxed ^ WOORT_BOX_VALUE_TYPE_INT)))
        {
            const woort_Int val = _woort_unbox_int64(box->m_boxed);
            return _woort_append_int(buf, val);
        }

        const bool val = _woort_unbox_bool(box->m_boxed);
        return _woort_append_str(buf, val ? "true" : "false");
    }

    /* NIL */
    if (box->m_boxed_gc_unit == NULL)
        return _woort_append_str(buf, "nil");

    const woort_GCUnitProxy* const proxy = box->m_boxed_gc_unit->m_proxy;

    /* 扩展装箱类型 */
    if (proxy == &WOORT_EX_BOX_PROXY)
    {
        const woort_BoxedExValue* const ex = box->m_boxed_ex;
        if (ex->m_is_int)
            return _woort_append_int(buf, ex->m_int);
        else
            return _woort_append_real(buf, ex->m_real);
    }

    /* GC 字符串 */
    if (proxy == &WOORT_GCSTRING_UNIT_PROXY)
    {
        const woort_GCString* const str =
            (const woort_GCString*)box->m_boxed_gc_unit;
        char* const escaped = woort_u8enstring(
            str->m_content, str->m_length, true);
        if (escaped == NULL)
            return false;

        const bool ok = _woort_append_str(buf, escaped);
        free(escaped);
        return ok;
    }

    /* GC 数组（vec） */
    if (proxy == &WOORT_GCVEC_UNIT_PROXY)
    {
        const woort_GCVec* const vec =
            (const woort_GCVec*)box->m_boxed_gc_unit;

        if (vec->m_length == 0)
            return _woort_append_str(buf, "[]");

        if (_woort_is_visited(
            visited_gcunits, *visited_count, &vec->m_gc_unit))
        {
            if (flags & WOORT_SERIALIZE_FLAG_FAIL_ON_CYCLE)
                return false;
            return _woort_append_str(buf, "[...]");
        }

        if (*visited_count >= visited_capacity)
            return false;
        visited_gcunits[(*visited_count)++] = &vec->m_gc_unit;

        if (!_woort_append_char(buf, '['))
            return false;

        for (size_t i = 0; i < vec->m_length; ++i)
        {
            if (i > 0)
            {
                if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
                {
                    if (!_woort_append_str(buf, ",\n"))
                        return false;
                }
                else
                {
                    if (!_woort_append_str(buf, ", "))
                        return false;
                }
            }

            if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
            {
                if (!_woort_append_indent(buf, depth, flags))
                    return false;
            }

            if (!_woort_serialize_dynbox_to_buf(
                    &vec->m_datas[i], buf,
                    visited_gcunits, visited_count, visited_capacity,
                    depth + 1, flags))
            {
                return false;
            }
        }

        if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
        {
            if (!_woort_append_indent(buf, depth - 1, flags))
                return false;
        }

        (*visited_count)--;
        return _woort_append_char(buf, ']');
    }

    /* GC 映射（map） */
    if (proxy == &WOORT_GCMAP_UNIT_PROXY)
    {
        const woort_GCMap* const gcmap =
            (const woort_GCMap*)box->m_boxed_gc_unit;

        if (gcmap->m_size == 0)
            return _woort_append_str(buf, "{}");

        if (_woort_is_visited(
            visited_gcunits, *visited_count, &gcmap->m_gc_unit))
        {
            if (flags & WOORT_SERIALIZE_FLAG_FAIL_ON_CYCLE)
                return false;
            return _woort_append_str(buf, "{...}");
        }

        if (*visited_count >= visited_capacity)
            return false;
        visited_gcunits[(*visited_count)++] = &gcmap->m_gc_unit;

        if (!_woort_append_char(buf, '{'))
            return false;

        for (size_t i = 0; i < gcmap->m_size; ++i)
        {
            const woort_GCMap_Bucket* const bucket =
                &gcmap->m_buckets[i];

            if (i > 0)
            {
                if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
                {
                    if (!_woort_append_str(buf, ",\n"))
                        return false;
                }
                else
                {
                    if (!_woort_append_str(buf, ", "))
                        return false;
                }
            }

            if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
            {
                if (!_woort_append_indent(buf, depth, flags))
                    return false;
            }

            if (!_woort_serialize_dynbox_to_buf(
                    &bucket->m_key, buf,
                    visited_gcunits, visited_count, visited_capacity,
                    depth + 1, flags))
            {
                return false;
            }

            if (!_woort_append_str(buf, ": "))
                return false;

            if (!_woort_serialize_dynbox_to_buf(
                    &bucket->m_val, buf,
                    visited_gcunits, visited_count, visited_capacity,
                    depth + 1, flags))
            {
                return false;
            }
        }

        if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
        {
            if (!_woort_append_indent(buf, depth - 1, flags))
                return false;
        }

        (*visited_count)--;
        return _woort_append_char(buf, '}');
    }

    /* 不支持的类型 */
    return false;
}

/* ========================================================================
 * 内部反序列化：字符串 -> DynBox
 * ======================================================================== */

const char* _woort_skip_whitespace(const char* p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        ++p;
    return p;
}

/* 前向声明 */
WOORT_NODISCARD static bool _woort_deserialize_value(
    const char** p,
    woort_DynBox* out_box);

/*
解析整数或实数。
*/
WOORT_NODISCARD static bool _woort_deserialize_number(
    const char** p,
    woort_DynBox* out_box)
{
    const char* const start = *p;
    char* end = NULL;

    const int64_t i64_val = strtoll(start, &end, 0);

    bool is_real = false;
    {
        const char* s = start;
        while (s < end)
        {
            if (*s == '.' || *s == 'e' || *s == 'E')
            {
                is_real = true;
                break;
            }
            ++s;
        }
    }

    if (is_real)
    {
        const double d = strtod(start, &end);
        *p = end;
        *out_box = woort_DynBox_box_real(d);
        return true;
    }

    if (end == start)
        return false;

    if (i64_val == INT64_MIN || i64_val == INT64_MAX)
    {
        const double d = strtod(start, &end);
        if (end == start)
            return false;
        *p = end;
        if (d >= (double)INT64_MIN && d <= (double)INT64_MAX
            && d == (double)(int64_t)d)
        {
            *out_box = woort_DynBox_box_int((int64_t)d);
        }
        else
        {
            *out_box = woort_DynBox_box_real(d);
        }
        return true;
    }

    *p = end;
    *out_box = woort_DynBox_box_int(i64_val);
    return true;
}

/*
解析字符串字面量。
*/
WOORT_NODISCARD static bool _woort_deserialize_string(
    const char** p,
    woort_DynBox* out_box)
{
    const char* pp = *p;
    assert(pp != NULL);
    assert(*pp == '"');
    ++pp;

    const char* const start = pp;
    while (*pp != '\0' && *pp != '"')
    {
        if (*pp == '\\')
        {
            ++pp;
            if (*pp == '\0')
                return false;
        }
        ++pp;
    }

    if (*pp != '"')
        return false;

    const size_t raw_len = (size_t)(pp - start);
    ++pp;
    *p = pp;

    char* tmp = (char*)malloc(raw_len + 1);
    if (tmp == NULL)
        return false;

    memcpy(tmp, start, raw_len);
    tmp[raw_len] = '\0';

    char* const unescaped = woort_u8destring(tmp);
    free(tmp);

    if (unescaped == NULL)
        return false;

    const size_t unescaped_len = strlen(unescaped);
    const woort_GCString* const gcstr = woort_GCString_make_string(
        unescaped, unescaped_len);
    free(unescaped);

    if (gcstr == NULL)
        return false;

    out_box->m_boxed_gc_unit = (woort_GCUnit*)gcstr;
    return true;
}

/*
解析映射：{ key: value, ... }
*/
WOORT_NODISCARD bool _woort_deserialize_map_impl(
    const char** p,
    woort_GCMap** out_gcmap)
{
    const char* pp = *p;
    assert(pp != NULL);
    assert(*pp == '{');
    ++pp;

    woort_GCMap* const gcmap = woort_GCMap_new();
    if (gcmap == NULL)
        return false;

    pp = _woort_skip_whitespace(pp);

    if (*pp == '}')
    {
        ++pp;
        *p = pp;
        *out_gcmap = gcmap;
        return true;
    }

    for (;;)
    {
        pp = _woort_skip_whitespace(pp);

        woort_DynBox key;
        if (!_woort_deserialize_value(&pp, &key))
            return false;

        pp = _woort_skip_whitespace(pp);

        if (*pp != ':')
            return false;
        ++pp;

        pp = _woort_skip_whitespace(pp);

        woort_DynBox val;
        if (!_woort_deserialize_value(&pp, &val))
            return false;

        woort_GCMap_set_or_insert(gcmap, key, val);

        pp = _woort_skip_whitespace(pp);

        if (*pp == '}')
        {
            ++pp;
            break;
        }
        else if (*pp == ',')
        {
            ++pp;
            pp = _woort_skip_whitespace(pp);
            if (*pp == '}')
            {
                ++pp;
                break;
            }
        }
        else
        {
            return false;
        }
    }

    *p = pp;
    *out_gcmap = gcmap;
    return true;
}

/*
解析数组：[ elem, elem, ... ]
*/
WOORT_NODISCARD bool _woort_deserialize_vec_impl(
    const char** p,
    woort_GCVec** out_gcvec)
{
    const char* pp = *p;
    assert(pp != NULL);
    assert(*pp == '[');
    ++pp;

    woort_GCVec* const gcvec = woort_GCVec_new();
    if (gcvec == NULL)
        return false;

    pp = _woort_skip_whitespace(pp);

    if (*pp == ']')
    {
        ++pp;
        *p = pp;
        *out_gcvec = gcvec;
        return true;
    }

    for (;;)
    {
        pp = _woort_skip_whitespace(pp);

        woort_DynBox elem;
        if (!_woort_deserialize_value(&pp, &elem))
            return false;

        woort_GCVec_push_back(gcvec, elem);

        pp = _woort_skip_whitespace(pp);

        if (*pp == ']')
        {
            ++pp;
            break;
        }
        else if (*pp == ',')
        {
            ++pp;
            pp = _woort_skip_whitespace(pp);
            if (*pp == ']')
            {
                ++pp;
                break;
            }
        }
        else
        {
            return false;
        }
    }

    *p = pp;
    *out_gcvec = gcvec;
    return true;
}

/*
解析单个值，派发到具体类型的解析函数。
*/
WOORT_NODISCARD static bool _woort_deserialize_value(
    const char** p,
    woort_DynBox* out_box)
{
    const char* pp = _woort_skip_whitespace(*p);

    if (*pp == '\0')
        return false;

    /* 映射 { */
    if (*pp == '{')
    {
        woort_GCMap* gcmap = NULL;
        if (!_woort_deserialize_map_impl(&pp, &gcmap))
            return false;

        out_box->m_boxed_gc_unit = &gcmap->m_gc_unit;
        *p = pp;
        return true;
    }

    /* 数组 [ */
    if (*pp == '[')
    {
        woort_GCVec* gcvec = NULL;
        if (!_woort_deserialize_vec_impl(&pp, &gcvec))
            return false;

        out_box->m_boxed_gc_unit = &gcvec->m_gc_unit;
        *p = pp;
        return true;
    }

    /* 字符串 " */
    if (*pp == '"')
    {
        *p = pp;
        return _woort_deserialize_string(p, out_box);
    }

    /* nil */
    if (strncmp(pp, "nil", 3) == 0)
    {
        const char next = pp[3];
        if (next == '\0' || next == ',' || next == '}'
            || next == ']' || next == ' ' || next == '\t'
            || next == '\n' || next == '\r' || next == ':')
        {
            pp += 3;
            out_box->m_boxed_gc_unit = NULL;
            out_box->m_boxed = 0;
            *p = pp;
            return true;
        }
    }

    /* null */
    if (strncmp(pp, "null", 4) == 0)
    {
        const char next = pp[4];
        if (next == '\0' || next == ',' || next == '}'
            || next == ']' || next == ' ' || next == '\t'
            || next == '\n' || next == '\r' || next == ':')
        {
            pp += 4;
            out_box->m_boxed_gc_unit = NULL;
            out_box->m_boxed = 0;
            *p = pp;
            return true;
        }
    }

    /* true */
    if (strncmp(pp, "true", 4) == 0)
    {
        const char next = pp[4];
        if (next == '\0' || next == ',' || next == '}'
            || next == ']' || next == ' ' || next == '\t'
            || next == '\n' || next == '\r' || next == ':')
        {
            pp += 4;
            *out_box = woort_DynBox_box_bool(true);
            *p = pp;
            return true;
        }
    }

    /* false */
    if (strncmp(pp, "false", 5) == 0)
    {
        const char next = pp[5];
        if (next == '\0' || next == ',' || next == '}'
            || next == ']' || next == ' ' || next == '\t'
            || next == '\n' || next == '\r' || next == ':')
        {
            pp += 5;
            *out_box = woort_DynBox_box_bool(false);
            *p = pp;
            return true;
        }
    }

    /* 数字 */
    if ((*pp >= '0' && *pp <= '9')
        || *pp == '+' || *pp == '-'
        || *pp == '.')
    {
        *p = pp;
        return _woort_deserialize_number(p, out_box);
    }

    return false;
}

WOORT_NODISCARD bool _woort_deserialize_dynbox_from_str(
    const char** p,
    woort_DynBox* out_box)
{
    const char* pp = *p;
    woort_DynBox result;

    if (!_woort_deserialize_value(&pp, &result))
        return false;

    pp = _woort_skip_whitespace(pp);

    *p = pp;
    *out_box = result;
    return true;
}
