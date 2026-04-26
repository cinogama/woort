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
 * 循环检测（woort_HashMap 辅助函数）
 * ======================================================================== */

WOORT_NODISCARD size_t _woort_serialize_ptr_hash(const void* key)
{
    return *(const size_t*)key;
}

WOORT_NODISCARD bool _woort_serialize_ptr_equal(
    const void* key1, const void* key2)
{
    return *(const size_t*)key1 == *(const size_t*)key2;
}

/* ========================================================================
 * 内部序列化：DynBox -> 字符串缓冲区
 * ======================================================================== */

WOORT_NODISCARD bool _woort_serialize_dynbox_to_buf(
    const woort_DynBox* box,
    woort_Vector* buf,
    woort_HashMap* visited_set,
    int depth,
    uint32_t flags)
{
    woort_Value temp_val;
    switch (woort_DynBox_unbox_no_check_and_get_type(*box, &temp_val))
    {
    case WOORT_BOX_VALUE_TYPE_INT:
        return _woort_append_int(buf, temp_val.m_integer);

    case WOORT_BOX_VALUE_TYPE_REAL:
        return _woort_append_real(buf, temp_val.m_real);

    case WOORT_BOX_VALUE_TYPE_BOOL:
        return _woort_append_str(buf, temp_val.m_integer ? "true" : "false");

    case WOORT_BOX_VALUE_TYPE_NIL:
        return _woort_append_str(buf, "nil");

    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        const woort_GCString* const str =
            (const woort_GCString*)temp_val.m_gcinstance;
        char* const escaped = woort_u8enstring(
            str->m_content, str->m_length, true);
        if (escaped == NULL)
            return false;

        const bool ok = _woort_append_str(buf, escaped);
        free(escaped);
        return ok;
    }

    case WOORT_BOX_VALUE_TYPE_VEC:
    {
        const woort_GCVec* const vec =
            (const woort_GCVec*)temp_val.m_gcinstance;

        if (vec->m_length == 0)
            return _woort_append_str(buf, "[]");

        void* _unused;
        woort_hashmap_Result _hr = woort_hashmap_get_or_emplace(
            visited_set, &vec->m_gc_unit, &_unused);
        if (_hr == WOORT_HASHMAP_RESULT_ALREADY_EXIST)
        {
            if (flags & WOORT_SERIALIZE_FLAG_FAIL_ON_CYCLE)
                return false;
            return _woort_append_str(buf, "[...]");
        }
        if (_hr == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
            return false;

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
                &vec->m_datas[i], buf, visited_set, depth + 1, flags))
            {
                return false;
            }
        }

        if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
        {
            if (!_woort_append_indent(buf, depth - 1, flags))
                return false;
        }

        if (!woort_hashmap_remove(visited_set, &vec->m_gc_unit))
            return false;
        return _woort_append_char(buf, ']');
    }

    case WOORT_BOX_VALUE_TYPE_MAP:
    {
        const woort_GCMap* const gcmap =
            (const woort_GCMap*)temp_val.m_gcinstance;

        if (gcmap->m_size == 0)
            return _woort_append_str(buf, "{}");

        void* _unused;
        woort_hashmap_Result _hr = woort_hashmap_get_or_emplace(
            visited_set, &gcmap->m_gc_unit, &_unused);
        if (_hr == WOORT_HASHMAP_RESULT_ALREADY_EXIST)
        {
            if (flags & WOORT_SERIALIZE_FLAG_FAIL_ON_CYCLE)
                return false;
            return _woort_append_str(buf, "{...}");
        }
        if (_hr == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
            return false;

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
                &bucket->m_key, buf, visited_set, depth + 1, flags))
            {
                return false;
            }

            if (!_woort_append_str(buf, ": "))
                return false;

            if (!_woort_serialize_dynbox_to_buf(
                &bucket->m_val, buf, visited_set, depth + 1, flags))
            {
                return false;
            }
        }

        if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
        {
            if (!_woort_append_indent(buf, depth - 1, flags))
                return false;
        }

        if (!woort_hashmap_remove(visited_set, &gcmap->m_gc_unit))
            return false;
        return _woort_append_char(buf, '}');
    }

    case WOORT_BOX_VALUE_TYPE_STRUCT:
        return _woort_append_str(buf, "<struct>");
    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
        return _woort_append_str(buf, "<gchandle>");
    case WOORT_BOX_VALUE_TYPE_CLOSURE:
        return _woort_append_str(buf, "<function>");
    }

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
    woort_GC_mixed_write_barrier_gcaddr(
        &out_box->m_boxed_gc_unit, gcstr);

    free(unescaped);

    return true;
}

/*
解析映射：{ key: value, ... }
*/
WOORT_NODISCARD bool _woort_deserialize_map_impl(
    const char** p,
    woort_DynBox* out_gcmap)
{
    const char* pp = *p;
    assert(pp != NULL);
    assert(*pp == '{');
    ++pp;

    woort_GCMap* const gcmap = woort_GCMap_new();
    woort_GC_mixed_write_barrier_gcaddr(
        &out_gcmap->m_boxed_gc_unit, gcmap);

    pp = _woort_skip_whitespace(pp);

    if (*pp == '}')
    {
        ++pp;
        *p = pp;
        return true;
    }

    for (;;)
    {
        pp = _woort_skip_whitespace(pp);

        woort_GCMap_Bucket* const bucket = woort_GCMap_emplace_prepare(gcmap);

        if (!_woort_deserialize_value(&pp, &bucket->m_key))
            return false;

        pp = _woort_skip_whitespace(pp);

        if (*pp != ':')
            return false;
        ++pp;

        pp = _woort_skip_whitespace(pp);

        if (!_woort_deserialize_value(&pp, &bucket->m_val))
            return false;

        woort_GCMap_emplace_commit(gcmap);

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
    return true;
}

/*
解析数组：[ elem, elem, ... ]
*/
WOORT_NODISCARD bool _woort_deserialize_vec_impl(
    const char** p,
    woort_DynBox* out_gcvec)
{
    const char* pp = *p;
    assert(pp != NULL);
    assert(*pp == '[');
    ++pp;

    woort_GCVec* const gcvec = woort_GCVec_new();
    woort_GC_mixed_write_barrier_gcaddr(
        &out_gcvec->m_boxed_gc_unit, gcvec);

    pp = _woort_skip_whitespace(pp);

    if (*pp == ']')
    {
        ++pp;
        *p = pp;
        return true;
    }

    for (;;)
    {
        pp = _woort_skip_whitespace(pp);

        woort_DynBox* elem = woort_GCVec_emplace_back(gcvec, 1);

        if (!_woort_deserialize_value(&pp, elem))
            return false;

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

    if (!_woort_deserialize_value(&pp, out_box))
        return false;

    pp = _woort_skip_whitespace(pp);

    *p = pp;
    return true;
}
