#include "woort.h"

#include "woort_serialize.h"
#include "woort_vector.h"
#include "woort_gc.h"
#include "woort_gc_struct.h"
#include "woort_gc_closure.h"
#include "woort_gc_gchandle.h"
#include "woort_codeenv.h"
#include "woort_util.h"

#include "woort_mem.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>

#define WOORT_SERIALIZE_MAX_DEBUG_DEPTH 64

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
    /* 各平台 printf 对非有限值的拼写不一致（MSVC 输出 "-nan(ind)"，
     * glibc 可能输出 "-nan"），统一归一化为固定拼写以保证 round-trip。 */
    if (isnan(val))
        return _woort_serialize_append_str(buf, "nan");
    if (isinf(val))
        return _woort_serialize_append_str(buf, val > 0 ? "inf" : "-inf");

    char tmp[64];
    const int n = snprintf(tmp, sizeof(tmp), "%.16g", val);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return false;

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
        const woort_GCString* const str = temp_val.m_string;
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
        const woort_GCVec* const vec = temp_val.m_vec;

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
        const woort_GCMap* const gcmap = temp_val.m_map;

        if (gcmap->m_size == 0)
            return _woort_serialize_append_str(buf, "{}");

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
        /* Should not been here. */
        abort();
    }

    return false;
}

/* ========================================================================
 * 内部辅助：snprintf 到临时缓冲区，然后写入 Vector
 * ======================================================================== */

static bool _woort_serialize_append_vfmt(woort_Vector* buf, const char* fmt, ...)
{
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return false;
    return woort_vector_push_back(buf, (size_t)n, tmp);
}

/* ========================================================================
 * 内部序列化：DynBox -> 调试缓冲区
 * ======================================================================== */

WOORT_NODISCARD int _woort_guess_float_weight(const void* valp)
{
    uint64_t bits;
    int64_t as_int;
    double as_double;
    memcpy(&bits, valp, sizeof(bits));
    memcpy(&as_int, valp, sizeof(as_int));
    memcpy(&as_double, valp, sizeof(as_double));

    const uint64_t biased_exp = (bits >> 52) & 0x7FF;

    /* 指数全 0：零或次正规数。真实浮点几乎不会出现这些位模式，它们通常是小
     * 整数的位模式（小正整数高位为 0 → 次正规，例如 4.94e-324），直接判整数。 */
    if (biased_exp == 0)
        return false;

    /* 指数全 1：无穷（frac == 0）或 NaN（frac != 0）。小负整数高位全 1 时会
     * 落入此处（NaN 位模式），真实浮点中 +/-∞ 与 NaN 同样少见，统一判整数。 */
    if (biased_exp == 0x7FF)
        return false;

    /* ---- 以下为正规数：整数解释为大整数，浮点解释为合法规格化数 ----
     * 二者结构上都"合法"，用打分法比较哪种解释更像真实代码里的值。 */

    int int_score = 0;
    int float_score = 1; /* 正规数基础分 */

    const uint64_t abs_int = as_int >= 0
        ? (uint64_t)as_int
        : (uint64_t)-as_int;

    /* 小整数更像整数 */
    if (abs_int < 65536)
        int_score += 2;

    /* 2 的幂更像整数（含 0，但此处 biased_exp != 0，故 abs_int != 0） */
    if ((abs_int & (abs_int - 1)) == 0)
        int_score += 1;

    const double abs_f = as_double >= 0 ? as_double : -as_double;

    /* 数量级落在常规范围 [1e-6, 1e6] 内更像真实浮点 */
    if (1e-6 <= abs_f && abs_f <= 1e6)
        float_score += 2;

    /* 接近整数值的浮点（如 3.0）。这类值整数也能精确表示，若同时又像小整数
     * 则更偏向整数解释。 */
    const double diff = fabs(as_double - round(as_double));
    const double threshold = 1e-6 * (abs_f > 1.0 ? abs_f : 1.0);
    if (diff < threshold)
    {
        float_score += 1;
        if (abs_int < (1ULL << 20))
            int_score += 2;
    }

    /* 整数得分严格更高则判整数（false），否则判浮点（true）。 */
    return float_score - int_score;
}

WOORT_NODISCARD bool _woort_serialize_guess_int_or_real_to_buf(
    const woort_Value* val, woort_Vector* buf)
{
    if (_woort_guess_float_weight(val) > 0)
        return _woort_serialize_append_real(buf, val->m_real);
    else
        return _woort_serialize_append_int(buf, val->m_integer);
}

/* ========================================================================
 * 内部辅助：fuzzy 模式下，使用 _woort_guess_float_weight 的权重值比较
 * boxed（正确装箱）与 raw（missed unbox）两种解释，输出最可能的值。
 *
 * boxed_type: 由 tag bits 检测到的类型（INT 或 REAL）
 * val:        按 tag bits 解箱后的值
 * vp:         原始字节重新解释为 woort_Value
 * ======================================================================== */

WOORT_NODISCARD static bool _woort_serialize_fuzzy_scalar(
    woort_BoxValueType boxed_type,
    const woort_Value* val,
    const woort_Value* vp,
    woort_Vector* buf)
{
    const int w_boxed = _woort_guess_float_weight(val);
    const int w_raw = _woort_guess_float_weight(vp);

    if (boxed_type == WOORT_BOX_VALUE_TYPE_INT)
    {
        /* boxed 解释期望整数（w_boxed <= 0） */
        if (w_boxed > 0)
        {
            /* 解箱结果更像浮点 → boxed INT 不可信，判定 missed unbox */
            return _woort_serialize_guess_int_or_real_to_buf(vp, buf);
        }

        /* boxed 整数可信，与 raw 解释比较 */
        if (w_raw > 0)
        {
            /* raw 字节更像浮点 */
            if (w_raw > -w_boxed)
            {
                /* raw 浮点权重高于 boxed 整数 → 更可能 missed unbox */
                return _woort_serialize_append_real(buf, vp->m_real)
                    && _woort_serialize_append_str(buf, "?Boxed(")
                    && _woort_serialize_append_int(buf, val->m_integer)
                    && _woort_serialize_append_str(buf, ")");
            }
            /* boxed 整数权重更高，但 raw 浮点也有可能 */
            return _woort_serialize_append_int(buf, val->m_integer)
                && _woort_serialize_append_str(buf, "?Raw(")
                && _woort_serialize_append_real(buf, vp->m_real)
                && _woort_serialize_append_str(buf, ")");
        }

        /* 两者都像整数 */
        if (-w_raw >= -w_boxed)
        {
            return _woort_serialize_append_int(buf, vp->m_integer)
                && _woort_serialize_append_str(buf, "?Boxed(")
                && _woort_serialize_append_int(buf, val->m_integer)
                && _woort_serialize_append_str(buf, ")");
        }
        else
        {
            return _woort_serialize_append_int(buf, val->m_integer)
                && _woort_serialize_append_str(buf, "?Raw(")
                && _woort_serialize_append_int(buf, vp->m_integer)
                && _woort_serialize_append_str(buf, ")");
        }
    }
    else /* WOORT_BOX_VALUE_TYPE_REAL */
    {
        /* boxed 解释期望浮点（w_boxed > 0） */
        if (w_boxed <= 0)
        {
            /* 解箱结果更像整数 → boxed REAL 不可信，判定 missed unbox */
            return _woort_serialize_guess_int_or_real_to_buf(vp, buf);
        }

        /* boxed 浮点可信，与 raw 解释比较 */
        if (w_raw <= 0)
        {
            /* raw 字节更像整数 */
            if (-w_raw > w_boxed)
            {
                /* raw 整数权重高于 boxed 浮点 → 更可能 missed unbox */
                return _woort_serialize_append_int(buf, vp->m_integer)
                    && _woort_serialize_append_str(buf, "?Boxed(")
                    && _woort_serialize_append_real(buf, val->m_real)
                    && _woort_serialize_append_str(buf, ")");
            }
            /* boxed 浮点权重更高，但 raw 整数也有可能 */
            return _woort_serialize_append_real(buf, val->m_real)
                && _woort_serialize_append_str(buf, "?Raw(")
                && _woort_serialize_append_int(buf, vp->m_integer)
                && _woort_serialize_append_str(buf, ")");
        }

        /* 两者都像浮点 */
        if (w_raw >= w_boxed)
        {
            return _woort_serialize_append_real(buf, vp->m_real)
                && _woort_serialize_append_str(buf, "?Boxed(")
                && _woort_serialize_append_real(buf, val->m_real)
                && _woort_serialize_append_str(buf, ")");
        }
        else
        {
            return _woort_serialize_append_real(buf, val->m_real)
                && _woort_serialize_append_str(buf, "?Raw(")
                && _woort_serialize_append_real(buf, vp->m_real)
                && _woort_serialize_append_str(buf, ")");
        }
    }
}

WOORT_NODISCARD static size_t _woort_validate_mem_ptr_and_get_capacity(
    void* addr, size_t expected_align_in_byte)
{
    void* const data_head = woort_mem_validate_addr(addr);

    if (data_head == NULL
        || (intptr_t)addr < (intptr_t)data_head
        || (intptr_t)addr % expected_align_in_byte != 0)
        return 0;

    return woort_mem_get_capacity_of_addr_head(data_head)
        - (size_t)((char*)addr - (char*)data_head);
}

WOORT_NODISCARD static bool _woort_check_size_in_capacity_with_overflow_check(
    size_t capacity, size_t base_size, size_t count, size_t elem_size)
{
    if (capacity < base_size
        || (elem_size != 0 && SIZE_MAX / elem_size < count)
        || (SIZE_MAX - base_size < count * elem_size)
        || capacity < base_size + count * elem_size)
        return false;

    return true;
}

void _woort_serialize_dynbox_print_for_debug(woort_DynBox box, bool is_fuzzy)
{
    woort_Vector buf;
    woort_vector_init(&buf, sizeof(char));

    woort_HashMap visited_set;
    woort_hashmap_init(
        &visited_set,
        sizeof(const woort_GCUnit*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    if (_woort_serialize_dynbox_to_buf_for_debug(
        box, &buf, &visited_set, 0, is_fuzzy))
    {
        (void)printf("%.*s", (int)buf.m_size, (const char*)buf.m_data);
    }

    woort_vector_deinit(&buf);
    woort_hashmap_deinit(&visited_set);
}

WOORT_NODISCARD bool _woort_serialize_dynbox_to_buf_for_debug(
    woort_DynBox boxed,
    woort_Vector* buf,
    woort_HashMap* visited_set,
    int depth,
    bool is_fuzzy)
{
    woort_Value* const vp = (woort_Value*)&boxed;

    if (!woort_DynBox_debug_check_is_valid(boxed))
        return _woort_serialize_guess_int_or_real_to_buf(vp, buf);

    if (depth > WOORT_SERIALIZE_MAX_DEBUG_DEPTH)
        return _woort_serialize_append_str(buf, "<max depth>");

    woort_Value val;
    switch (woort_DynBox_unbox_no_check_and_get_type(boxed, &val))
    {
    case WOORT_BOX_VALUE_TYPE_INT:
        if (is_fuzzy)
        {
            return _woort_serialize_fuzzy_scalar(
                WOORT_BOX_VALUE_TYPE_INT, &val, vp, buf);
        }
        return _woort_serialize_append_int(buf, val.m_integer);

    case WOORT_BOX_VALUE_TYPE_REAL:
        if (is_fuzzy)
        {
            return _woort_serialize_fuzzy_scalar(
                WOORT_BOX_VALUE_TYPE_REAL, &val, vp, buf);
        }
        return _woort_serialize_append_real(buf, val.m_real);

    case WOORT_BOX_VALUE_TYPE_BOOL:
        if (is_fuzzy)
        {
            /* 检查原始字节是否为合法 bool 位模式（4=false, 12=true） */
            if (boxed.m_boxed == 0b0100 || boxed.m_boxed == 0b1100)
            {
                /* 合法 bool 模式，但同样的位模式也可能是原始小整数
                 * （4 或 12），两者权重均为 0，真正模棱两可 */
                return _woort_serialize_append_vfmt(buf,
                    "%s? Raw(%llu)",
                    val.m_integer ? "true" : "false",
                    (unsigned long long)boxed.m_boxed);
            }
            /* 非合法 bool 模式 → 确定是 missed unbox */
            return _woort_serialize_guess_int_or_real_to_buf(vp, buf);
        }
        return _woort_serialize_append_str(buf,
            val.m_integer ? "true" : "false");

    case WOORT_BOX_VALUE_TYPE_NIL:
        /* 零字节在所有解释下都是零（nil / int 0 / float 0.0），
         * 没有实质歧义 */
        return _woort_serialize_append_str(buf, "nil");

    case WOORT_BOX_VALUE_TYPE_STRING:
    {
        const woort_GCString* const gcstr = val.m_string;

        size_t gcstr_length;

        const size_t gcstr_capacity = _woort_validate_mem_ptr_and_get_capacity(
            (void*)gcstr, _Alignof(woort_GCString));
        if (gcstr_capacity < sizeof(woort_GCString)
            || !_woort_check_size_in_capacity_with_overflow_check(
                gcstr_capacity,
                sizeof(woort_GCString),
                (gcstr_length = gcstr->m_length),
                sizeof(char)))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        char* const enstr = woort_u8enstring(
            gcstr->m_content, gcstr_length, false);

        if (enstr != NULL)
        {
            const bool ok = _woort_serialize_append_vfmt(
                buf, "%s", enstr);
            free(enstr);
            return ok;
        }
        else
        {
            return _woort_serialize_append_str(
                buf, "<string: out of memory>");
        }
    }

    case WOORT_BOX_VALUE_TYPE_VEC:
    {
        const woort_GCVec* const gcvec = val.m_vec;

        if (!_woort_check_size_in_capacity_with_overflow_check(
            _woort_validate_mem_ptr_and_get_capacity((void*)gcvec, _Alignof(woort_GCVec)),
            sizeof(woort_GCVec),
            0,
            0))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        const size_t gcvec_length = gcvec->m_length;

        if (gcvec_length == 0)
            return _woort_serialize_append_str(buf, "[]");

        switch (woort_hashmap_insert(
            visited_set, (void**)&gcvec, NULL))
        {
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
            return _woort_serialize_append_str(buf, "[...]");
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            return _woort_serialize_append_str(buf, "[<out of memory>]");
        default:
            break;
        }

        woort_DynBox* const gcvec_datas = gcvec->m_datas;
        if (!_woort_check_size_in_capacity_with_overflow_check(
            _woort_validate_mem_ptr_and_get_capacity(gcvec_datas, _Alignof(woort_DynBox)),
            0,
            gcvec_length,
            sizeof(woort_DynBox)))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        if (!_woort_serialize_append_char(buf, '['))
            return false;

        for (size_t i = 0; i < gcvec_length; ++i)
        {
            if (i > 0)
            {
                if (!_woort_serialize_append_str(buf, ", "))
                    return false;
            }
            if (!_woort_serialize_dynbox_to_buf_for_debug(
                gcvec_datas[i], buf, visited_set, depth + 1, false))
            {
                return false;
            }
        }

        (void)woort_hashmap_remove(visited_set, (void**)&gcvec);
        return _woort_serialize_append_char(buf, ']');
    }

    case WOORT_BOX_VALUE_TYPE_MAP:
    {
        const woort_GCMap* const gcmap = val.m_map;

        if (!_woort_check_size_in_capacity_with_overflow_check(
            _woort_validate_mem_ptr_and_get_capacity((void*)gcmap, _Alignof(woort_GCMap)),
            sizeof(woort_GCMap),
            0,
            0))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        const size_t gcmap_size = gcmap->m_size;

        if (gcmap_size == 0)
            return _woort_serialize_append_str(buf, "{}");

        switch (woort_hashmap_insert(
            visited_set, (void**)&gcmap, NULL))
        {
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
            return _woort_serialize_append_str(buf, "{...}");
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            return _woort_serialize_append_str(buf, "{<out of memory>}");
        default:
            break;
        }

        woort_GCMap_Bucket* const gcmap_buckets = gcmap->m_buckets;
        if (!_woort_check_size_in_capacity_with_overflow_check(
            _woort_validate_mem_ptr_and_get_capacity(gcmap_buckets, _Alignof(woort_GCMap_Bucket)),
            0,
            gcmap_size,
            sizeof(woort_GCMap_Bucket)))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        if (!_woort_serialize_append_char(buf, '{'))
            return false;

        for (size_t i = 0; i < gcmap_size; ++i)
        {
            const woort_GCMap_Bucket* const bucket =
                &gcmap_buckets[i];

            if (i > 0)
            {
                if (!_woort_serialize_append_str(buf, ", "))
                    return false;
            }
            if (!_woort_serialize_dynbox_to_buf_for_debug(
                bucket->m_key, buf, visited_set, depth + 1, false))
                return false;
            if (!_woort_serialize_append_str(buf, ": "))
                return false;
            if (!_woort_serialize_dynbox_to_buf_for_debug(
                bucket->m_val, buf, visited_set, depth + 1, false))
                return false;
        }

        (void)woort_hashmap_remove(visited_set, (void**)&gcmap);
        return _woort_serialize_append_char(buf, '}');
    }

    case WOORT_BOX_VALUE_TYPE_STRUCT:
    {
        const woort_GCStruct* const gcstruct = val.m_struct;

        size_t gcstruct_length;
        const size_t gcstruct_capacity =
            _woort_validate_mem_ptr_and_get_capacity(
                (void*)gcstruct, _Alignof(woort_GCStruct));

        if (gcstruct_capacity < sizeof(woort_GCStruct)
            || !_woort_check_size_in_capacity_with_overflow_check(
                gcstruct_capacity,
                sizeof(woort_GCStruct),
                (gcstruct_length = gcstruct->m_size),
                sizeof(woort_Value)))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        if (gcstruct_length == 0)
            return _woort_serialize_append_str(buf, "()");

        switch (woort_hashmap_insert(
            visited_set, (void**)&gcstruct, NULL))
        {
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
            return _woort_serialize_append_str(buf, "(...)");
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            return _woort_serialize_append_str(buf,
                "(<out of memory>)");
        default:
            break;
        }

        if (!_woort_serialize_append_char(buf, '('))
            return false;
        for (size_t i = 0; i < gcstruct_length; ++i)
        {
            if (i > 0)
            {
                if (!_woort_serialize_append_str(buf, ", "))
                    return false;
            }
            if (!_woort_serialize_dynbox_to_buf_for_debug(
                *(woort_DynBox*)&gcstruct->m_datas[i],
                buf, visited_set, depth + 1, true))
            {
                return false;
            }
        }

        (void)woort_hashmap_remove(visited_set, (void**)&gcstruct);
        return _woort_serialize_append_char(buf, ')');
    }

    case WOORT_BOX_VALUE_TYPE_GCHANDLE:
    {
        const woort_GCHandle* const gchandle = val.m_gchandle;

        if (!_woort_check_size_in_capacity_with_overflow_check(
            _woort_validate_mem_ptr_and_get_capacity((void*)gchandle, _Alignof(woort_GCHandle)),
            sizeof(woort_GCHandle),
            0,
            0))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        return _woort_serialize_append_vfmt(
            buf, "<gchandle %p>", (const void*)gchandle);
    }

    case WOORT_BOX_VALUE_TYPE_CLOSURE:
    {
        const woort_GCClosure* const gcclosure = val.m_closure;

        const size_t gcclosure_capacity = _woort_validate_mem_ptr_and_get_capacity(
            (void*)gcclosure, _Alignof(woort_GCClosure));
        if (gcclosure_capacity < sizeof(woort_GCClosure)
            || !_woort_check_size_in_capacity_with_overflow_check(
                gcclosure_capacity,
                sizeof(woort_GCClosure),
                gcclosure->m_size,
                sizeof(woort_Value)))
        {
            return _woort_serialize_append_str(buf, "<bad>");
        }

        if (gcclosure->m_script_function != NULL)
        {
            woort_CodeEnv* cenv = NULL;
            if (woort_CodeEnv_find(gcclosure->m_script_function, &cenv)
                && cenv != NULL)
            {
                const uint32_t offset = (uint32_t)(
                    gcclosure->m_script_function - cenv->m_code_begin);
                const char* const name =
                    woort_CodeEnv_find_function_name_by_offset(cenv, offset);
                if (name != NULL)
                    return _woort_serialize_append_vfmt(
                        buf, "<function %s>", name);
                else
                    return _woort_serialize_append_vfmt(
                        buf, "<function %p>",
                        (const void*)gcclosure->m_script_function);
            }
            else
            {
                return _woort_serialize_append_vfmt(
                    buf, "<function %p>",
                    (const void*)gcclosure->m_script_function);
            }
        }
        else
        {
            return _woort_serialize_append_vfmt(
                buf, "<native %p>",
                (const void*)(uintptr_t)gcclosure->m_native_function);
        }
    }

    default:
        return _woort_serialize_append_str(buf, "<unknown>");
    }
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

WOORT_NODISCARD const char* _woort_deserialize_skip_whitespace(const char* p)
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

    errno = 0;
    const int64_t i64_val = strtoll(start, &end, 0);

    if (*end == '.' || *end == 'e' || *end == 'E')
    {
        const double d = strtod(start, &end);
        *p = end;
        *out_box = woort_DynBox_box_real(d);
        return true;
    }

    if (end == start)
    {
        /* strtoll 无法解析，可能是 inf/-inf/+inf/nan/-nan 等特殊浮点值，
         * 或带符号但无数字的形式。标准 strtod 支持 inf/infinity/nan
         * （大小写不敏感，可带正负号），交由 strtod 处理。*/
        const double d = strtod(start, &end);
        if (end == start)
            return false;
        *p = end;
        *out_box = woort_DynBox_box_real(d);
        return true;
    }

    if (errno == ERANGE)
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

    size_t unescaped_len = 0;
    char* const unescaped = woort_u8destring(tmp, &unescaped_len);
    free(tmp);

    if (unescaped == NULL)
        return false;

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

    /* 数字（含 inf/-inf/nan/-nan 等特殊浮点值；
     * nil/null 已在前文先行处理，此处 'n' 仅可能是 nan） */
    if ((*pp >= '0' && *pp <= '9')
        || *pp == '+' || *pp == '-'
        || *pp == '.'
        || *pp == 'i' || *pp == 'I'
        || *pp == 'n' || *pp == 'N')
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
