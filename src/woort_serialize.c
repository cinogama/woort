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
#include <math.h>

/* ========================================================================
 * 内部工具：输出字符到缓冲区
 * ======================================================================== */

static bool _woort_serialize_append_char(woort_Vector* buf, char ch)
{
    return woort_vector_push_back(buf, 1, &ch);
}

static bool _woort_serialize_append_str(woort_Vector* buf, const char* str)
{
    const size_t len = strlen(str);
    return woort_vector_push_back(buf, len, str);
}

/* 前向声明 */
static bool _woort_serialize_append_cstr(woort_Vector* buf, const char* str, size_t len)
{
    return woort_vector_push_back(buf, len, str);
}

/*
将整数写入缓冲区（使用 snprintf）。
*/
static bool _woort_serialize_append_int(woort_Vector* buf, woort_Int val)
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
static bool _woort_serialize_append_real(woort_Vector* buf, woort_Real val)
{
    char tmp[64];
    const int n = snprintf(tmp, sizeof(tmp), "%.16g", val);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return false;

    /* For non-finite values (NaN, Inf), output as-is */
    if (!isfinite(val))
        return woort_vector_push_back(buf, (size_t)n, tmp);

    /* Check if the formatted result already contains '.' or 'e'/'E'.
     * If it does, the number already has a decimal point or uses scientific
     * notation, so output as-is. Otherwise (e.g. "1"), append ".0". */
    bool needs_dot = true;
    for (int i = 0; i < n; ++i)
    {
        if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E')
        {
            needs_dot = false;
            break;
        }
    }

    if (needs_dot)
    {
        char augmented[64];
        memcpy(augmented, tmp, n);
        augmented[n] = '.';
        augmented[n + 1] = '0';
        return woort_vector_push_back(buf, (size_t)(n + 2), augmented);
    }

    return woort_vector_push_back(buf, (size_t)n, tmp);
}

/*
写入缩进（仅 PRETTY 模式）。
*/
static bool _woort_serialize_append_indent(woort_Vector* buf, int depth, uint32_t flags)
{
    if (!(flags & WOORT_SERIALIZE_FLAG_PRETTY))
        return true;

    if (!_woort_serialize_append_char(buf, '\n'))
        return false;

    for (int i = 0; i <= depth; ++i)
    {
        if (!_woort_serialize_append_cstr(buf, "    ", 4))
            return false;
    }
    return true;
}

/* ========================================================================
 * 内部序列化：DynBox -> 字符串缓冲区
 * ======================================================================== */

WOORT_NODISCARD bool _woort_serialize_dynbox_to_buf(
    woort_DynBox box,
    woort_Vector* buf,
    woort_HashMap* visited_set,
    int depth,
    uint32_t flags)
{
    woort_Value temp_val;
    switch (woort_DynBox_unbox_no_check_and_get_type(box, &temp_val))
    {
    case WOORT_BOX_VALUE_TYPE_INT:
        return _woort_serialize_append_int(buf, temp_val.m_integer);

    case WOORT_BOX_VALUE_TYPE_REAL:
        return _woort_serialize_append_real(buf, temp_val.m_real);

    case WOORT_BOX_VALUE_TYPE_BOOL:
        return _woort_serialize_append_str(buf, temp_val.m_integer ? "true" : "false");

    case WOORT_BOX_VALUE_TYPE_NIL:
        if (flags & WOORT_SERIALIZE_FLAG_USE_NULL)
            return _woort_serialize_append_str(buf, "null");
        return _woort_serialize_append_str(buf, "nil");

    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        const woort_GCString* const str =
            (const woort_GCString*)temp_val.m_gcinstance;
        char* const escaped = woort_u8enstring(
            str->m_content, str->m_length, true);
        if (escaped == NULL)
            return false;

        const bool ok = _woort_serialize_append_str(buf, escaped);
        free(escaped);
        return ok;
    }

    case WOORT_BOX_VALUE_TYPE_VEC:
    {
        const woort_GCVec* const vec =
            (const woort_GCVec*)temp_val.m_gcinstance;

        if (vec->m_length == 0)
            return _woort_serialize_append_str(buf, "[]");

        woort_hashmap_Result _hr = woort_hashmap_insert(
            visited_set, 
            (void**)&vec,
            NULL);

        if (_hr == WOORT_HASHMAP_RESULT_ALREADY_EXIST)
        {
            if (flags & WOORT_SERIALIZE_FLAG_STRICT)
                return false;
            return _woort_serialize_append_str(buf, "[...]");
        }
        if (_hr == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
            return false;

        if (!_woort_serialize_append_char(buf, '['))
            return false;

        for (size_t i = 0; i < vec->m_length; ++i)
        {
            if (i > 0)
            {
                if (!_woort_serialize_append_str(buf, ", "))
                    return false;
            }

            if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
            {
                if (!_woort_serialize_append_indent(buf, depth, flags))
                    return false;
            }

            if (!_woort_serialize_dynbox_to_buf(
                vec->m_datas[i], buf, visited_set, depth + 1, flags))
            {
                return false;
            }
        }

        if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
        {
            if (!_woort_serialize_append_indent(buf, depth - 1, flags))
                return false;
        }

        if (!woort_hashmap_remove(visited_set, (void**)&vec))
            return false;
        return _woort_serialize_append_char(buf, ']');
    }

    case WOORT_BOX_VALUE_TYPE_MAP:
    {
        const woort_GCMap* const gcmap =
            (const woort_GCMap*)temp_val.m_gcinstance;

        if (gcmap->m_size == 0)
            return _woort_serialize_append_str(buf, "{}");

        void* _unused;
        woort_hashmap_Result _hr = woort_hashmap_insert(
            visited_set, 
            (void**)&gcmap,
            NULL);

        if (_hr == WOORT_HASHMAP_RESULT_ALREADY_EXIST)
        {
            if (flags & WOORT_SERIALIZE_FLAG_STRICT)
                return false;
            return _woort_serialize_append_str(buf, "{...}");
        }
        if (_hr == WOORT_HASHMAP_RESULT_OUT_OF_MEMORY)
            return false;

        if (!_woort_serialize_append_char(buf, '{'))
            return false;

        for (size_t i = 0; i < gcmap->m_size; ++i)
        {
            const woort_GCMap_Bucket* const bucket =
                &gcmap->m_buckets[i];

            if (i > 0)
            {
                if (!_woort_serialize_append_str(buf, ", "))
                    return false;
            }

            if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
            {
                if (!_woort_serialize_append_indent(buf, depth, flags))
                    return false;
            }

            if (!_woort_serialize_dynbox_to_buf(
                bucket->m_key, buf, visited_set, depth + 1, flags))
            {
                return false;
            }

            if (!_woort_serialize_append_str(buf, ": "))
                return false;

            if (!_woort_serialize_dynbox_to_buf(
                bucket->m_val, buf, visited_set, depth + 1, flags))
            {
                return false;
            }
        }

        if (flags & WOORT_SERIALIZE_FLAG_PRETTY)
        {
            if (!_woort_serialize_append_indent(buf, depth - 1, flags))
                return false;
        }

        if (!woort_hashmap_remove(visited_set, (void**)&gcmap))
            return false;
        return _woort_serialize_append_char(buf, '}');
    }

    case WOORT_BOX_VALUE_TYPE_STRUCT:
        if (flags & WOORT_SERIALIZE_FLAG_STRICT)
            return false;
        return _woort_serialize_append_str(buf, "<struct>");
    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
        if (flags & WOORT_SERIALIZE_FLAG_STRICT)
            return false;
        return _woort_serialize_append_str(buf, "<gchandle>");
    case WOORT_BOX_VALUE_TYPE_CLOSURE:
        if (flags & WOORT_SERIALIZE_FLAG_STRICT)
            return false;
        return _woort_serialize_append_str(buf, "<function>");
    default:
        // Should not been here.
        abort();
    }

    return false;
}

/* ========================================================================
 * 公开 impl：woort_Value* 版本的序列化入口
 * ======================================================================== */

WOORT_NODISCARD bool _woort_serialize_map_impl(
    woort_Value* dst,
    const woort_Value* src_val,
    uint32_t flags)
{
    const woort_GCMap* const gcmap = src_val->m_map;
    assert(gcmap != NULL);

    woort_DynBox box;
    memset(&box, 0, sizeof(box));
    box.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)gcmap);

    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*), 
        0,
        woort_util_ptr_hash, 
        woort_util_ptr_equal);

    if (!_woort_serialize_dynbox_to_buf(
        box, &buf, &visited_set, 0, flags))
    {
        woort_hashmap_deinit(&visited_set);
        woort_vector_deinit(&buf);
        return false;
    }

    woort_hashmap_deinit(&visited_set);

    const woort_GCString* const gcstr = woort_GCString_make_string(
        buf.m_data, buf.m_size);
    woort_vector_deinit(&buf);

    dst->m_string = gcstr;
    return true;
}

WOORT_NODISCARD bool _woort_serialize_vec_impl(
    woort_Value* dst,
    const woort_Value* src_val,
    uint32_t flags)
{
    const woort_GCVec* const gcvec = src_val->m_vec;
    assert(gcvec != NULL);

    woort_DynBox box;
    memset(&box, 0, sizeof(box));
    box.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)gcvec);

    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*), 
        0,
        woort_util_ptr_hash, 
        woort_util_ptr_equal);

    if (!_woort_serialize_dynbox_to_buf(
        box, &buf, &visited_set, 0, flags))
    {
        woort_hashmap_deinit(&visited_set);
        woort_vector_deinit(&buf);
        return false;
    }

    woort_hashmap_deinit(&visited_set);

    const woort_GCString* const gcstr = woort_GCString_make_string(
        buf.m_data, buf.m_size);
    woort_vector_deinit(&buf);

    dst->m_string = gcstr;
    return true;
}

/* ========================================================================
 * 内部反序列化：字符串 -> DynBox
 * ======================================================================== */

const char* _woort_deserialize_skip_whitespace(const char* p)
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
    {
        woort_DynBox tmp;
        tmp.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)gcstr);
        woort_GC_init_write_barrier_dynbox(out_box, tmp);
    }

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
    {
        woort_DynBox tmp;
        tmp.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)gcmap);
        woort_GC_init_write_barrier_dynbox(out_gcmap, tmp);
    }

    pp = _woort_deserialize_skip_whitespace(pp);

    if (*pp == '}')
    {
        ++pp;
        *p = pp;
        return true;
    }

    for (;;)
    {
        pp = _woort_deserialize_skip_whitespace(pp);

        woort_GCMap_Bucket* const bucket = woort_GCMap_emplace_prepare(gcmap);

        if (!_woort_deserialize_value(&pp, &bucket->m_key))
            return false;

        pp = _woort_deserialize_skip_whitespace(pp);

        if (*pp != ':')
            return false;
        ++pp;

        pp = _woort_deserialize_skip_whitespace(pp);

        if (!_woort_deserialize_value(&pp, &bucket->m_val))
            return false;

        woort_GCMap_emplace_commit(gcmap);

        pp = _woort_deserialize_skip_whitespace(pp);

        if (*pp == '}')
        {
            ++pp;
            break;
        }
        else if (*pp == ',')
        {
            ++pp;
            pp = _woort_deserialize_skip_whitespace(pp);
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
    {
        woort_DynBox tmp;
        tmp.m_boxed = _woort_gcunit_to_boxed((woort_GCUnit*)gcvec);
        woort_GC_init_write_barrier_dynbox(out_gcvec, tmp);
    }

    pp = _woort_deserialize_skip_whitespace(pp);

    if (*pp == ']')
    {
        ++pp;
        *p = pp;
        return true;
    }

    for (;;)
    {
        pp = _woort_deserialize_skip_whitespace(pp);

        woort_DynBox* elem = woort_GCVec_emplace_back(gcvec, 1);

        if (!_woort_deserialize_value(&pp, elem))
            return false;

        pp = _woort_deserialize_skip_whitespace(pp);

        if (*pp == ']')
        {
            ++pp;
            break;
        }
        else if (*pp == ',')
        {
            ++pp;
            pp = _woort_deserialize_skip_whitespace(pp);
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
    const char* pp = _woort_deserialize_skip_whitespace(*p);

    if (*pp == '\0')
        return false;

    /* 映射 { */
    if (*pp == '{')
    {
        if (!_woort_deserialize_map_impl(&pp, out_box))
            return false;

        *p = pp;
        return true;
    }

    /* 数组 [ */
    if (*pp == '[')
    {
        if (!_woort_deserialize_vec_impl(&pp, out_box))
            return false;

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

    pp = _woort_deserialize_skip_whitespace(pp);

    *p = pp;
    return true;
}
