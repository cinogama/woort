#include "woort.h"

#include "woort_builtin.h"

#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_threads.h"

#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc.h"
#include "woort_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <time.h>

/* ================================================================
 * Global handle and lifecycle
 * ================================================================ */

static /* OPTIONAL */ woort_Dylib* g_builtin_lib = NULL;
static woort_AtomicUInt64          g_random_state = { 0 };

static int    g_cmdlines_argc = 0;
static char** g_cmdlines_argv = NULL;

/* ================================================================
 * Built-in native function implementations
 * ================================================================ */

static woort_api woort_builtin_return_it_self(void)
{
    return woort_ret_value(0);
}

static woort_api woort_builtin_bad_function(void)
{
    return woort_ret_panic("This function cannot be invoked.");
}

static woort_api woort_builtin_panic(void)
{
    return woort_ret_panic("%s", woort_string(0));
}

static woort_api woort_builtin_print(void)
{
    const woort_Int argn = woort_int(0);
    for (woort_Int i = 1; i <= argn; ++i)
    {
        if (i != 1)
            fputc(' ', stdout);

        if (woort_unbox_type((woort_StackValue)i)
            == WOORT_BOX_VALUE_TYPE_STRING)
        {
            fputs(woort_string((woort_StackValue)i), stdout);
        }
        else
        {
            char* const str =
                woort_serialize_dynbox(
                    (woort_StackValue)i,
                    WOORT_SERIALIZE_FLAG_NONE);
            if (str == NULL)
                return woort_ret_panic("Out of memory.");

            fputs(str, stdout);
            free(str);
        }
    }
    return woort_ret_void();
}

static woort_api woort_builtin_input_read_i(void)
{
    long long result;

    woort_vm* this_vm = woort_vm_swap(NULL);
    while (scanf("%lld", &result) != 1)
    {
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    (void)woort_vm_swap(this_vm);

    return woort_ret_int((woort_Int)result);
}
static woort_api woort_builtin_input_read_r(void)
{
    double result;

    woort_vm* this_vm = woort_vm_swap(NULL);
    while (scanf("%lf", &result) != 1)
    {
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    (void)woort_vm_swap(this_vm);

    return woort_ret_real((woort_Real)result);
}
static woort_api woort_builtin_input_read_s(void)
{
    woort_vm* this_vm = woort_vm_swap(NULL);

    woort_Vector vec;
    woort_vector_init(&vec, sizeof(char));

    for (;;)
    {
        int c;
        /* Skip leading whitespace (same semantics as scanf %s) */
        while ((c = getchar()) != EOF
            && (c == ' ' || c == '\t' || c == '\n' || c == '\r'))
            ;
        if (c == EOF)
        {
            woort_vector_clear(&vec);
            continue;
        }

        /* Read non-whitespace characters */
        do
        {
            const char ch = (char)c;
            if (!woort_vector_push_back(&vec, 1, &ch))
            {
                woort_vector_deinit(&vec);
                (void)woort_vm_swap(this_vm);
                return woort_ret_panic("Out of memory.");
            }
        } while ((c = getchar()) != EOF
            && c != ' ' && c != '\t' && c != '\n' && c != '\r');

        /* Put back the whitespace terminator (matching scanf/std::cin>>) */
        if (c != EOF)
            ungetc(c, stdin);

        break;
    }

    (void)woort_vm_swap(this_vm);

    const woort_api r = woort_ret_buffer(vec.m_data, vec.m_size);
    woort_vector_deinit(&vec);

    return r;
}
static woort_api woort_builtin_input_readline(void)
{
    woort_vm* this_vm = woort_vm_swap(NULL);

    woort_Vector vec;
    woort_vector_init(&vec, sizeof(char));

    for (;;)
    {
        int c;
        while ((c = getchar()) != EOF && c != '\n')
        {
            const char ch = (char)c;
            if (!woort_vector_push_back(&vec, 1, &ch))
            {
                woort_vector_deinit(&vec);
                (void)woort_vm_swap(this_vm);
                return woort_ret_panic("Out of memory.");
            }
        }

        if (c == EOF && vec.m_size == 0)
        {
            woort_vector_clear(&vec);
            continue;
        }

        break;
    }

    (void)woort_vm_swap(this_vm);

    const woort_api r = woort_ret_buffer(vec.m_data, vec.m_size);
    woort_vector_deinit(&vec);
    return r;
}
static uint64_t _woort_random_u64(void)
{
    uint64_t z;
    uint64_t old = woort_atomic_load(&g_random_state);
    do
    {
        z = old + 0x9E3779B97F4A7C15ULL;
    } while (!woort_atomic_compare_exchange_weak(&g_random_state, &old, z));

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static woort_api woort_builtin_random_i(void)
{
    woort_Int from = woort_int(0);
    woort_Int to = woort_int(1);

    if (to < from)
    {
        woort_Int tmp = from;
        from = to;
        to = tmp;
    }

    if (from == to)
        return woort_ret_int(from);

    const uint64_t r = _woort_random_u64();
    const uint64_t range = (uint64_t)(to - from) + 1;
    return woort_ret_int((woort_Int)(from + (woort_Int)(r % range)));
}
static woort_api woort_builtin_random_r(void)
{
    woort_Real from = woort_real(0);
    woort_Real to = woort_real(1);

    if (to < from)
    {
        woort_Real tmp = from;
        from = to;
        to = tmp;
    }

    if (from == to)
        return woort_ret_real(from);

    const uint64_t r = _woort_random_u64();
    const woort_Real uniform = (woort_Real)(r >> 11) * 0x1.0p-53;
    return woort_ret_real(from + (to - from) * uniform);
}
static woort_api woort_builtin_yield(void)
{
    return woort_ret_yield();
}
static woort_api woort_builtin_sleep(void)
{
    const woort_Real tm = woort_real(0);

    if (tm >= 0.0)
        woort_thread_sleep_ms((uint32_t)(tm * 1000.0));

    return woort_ret_void();
}
static woort_api woort_builtin_is_same(void)
{
    const woort_Int a = woort_int(0);
    const woort_Int b = woort_int(1);
    return woort_ret_bool(a == b);
}
static woort_api woort_builtin_cmdlines(void)
{
    woort_StackValue vec_slot;
    if (!woort_push_reserve(1, &vec_slot))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(vec_slot);

    for (int i = 0; i < g_cmdlines_argc; i++)
    {
        woort_StackValue elem_slot;
        if (!woort_push_reserve(1, &elem_slot))
            return woort_ret_panic("Stack overflow.");

        woort_set_string(elem_slot, g_cmdlines_argv[i]);
        woort_vec_push(vec_slot, elem_slot);
    }

    return woort_ret_value(vec_slot);
}
static woort_api woort_builtin_host_path(void)
{
    char* path = woort_exe_path();
    if (path == NULL)
        return woort_ret_panic("Failed to get executable path.");

    woort_set_string((woort_StackValue)-1, path);
    free(path);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api woort_builtin_make_dup(void)
{
    woort_StackValue result_slot;
    if (!woort_push_reserve(1, &result_slot))
        return woort_ret_panic("Stack overflow.");

    woort_Value _unboxed;
    const woort_DynBox box = woort_internal_value(0)->m_dynamic;

    switch (woort_DynBox_unbox_no_check_and_get_type(box, &_unboxed))
    {
    case WOORT_BOX_VALUE_TYPE_VEC:
    {
        const woort_GCVec* const src = _unboxed.m_vec;

        woort_set_vec(result_slot + 0);
        woort_GCVec* const dst = woort_internal_value(result_slot + 0)->m_vec;

        woort_GCVec_resize(dst, src->m_length);

        for (size_t i = 0; i < src->m_length; i++)
        {
            woort_GC_mixed_write_barrier_dynbox(
                &dst->m_datas[i], src->m_datas[i]);
        }
        break;
    }
    case WOORT_BOX_VALUE_TYPE_MAP:
    {
        const woort_GCMap* const src = _unboxed.m_map;

        woort_set_map(result_slot + 0);
        woort_GCMap* const dst = woort_internal_value(result_slot + 0)->m_map;

        for (size_t i = 0; i < src->m_size; i++)
        {
            woort_GCMap_set_or_insert(dst, src->m_buckets[i].m_key, src->m_buckets[i].m_val);
        }
        break;
    }
    case WOORT_BOX_VALUE_TYPE_STRUCT:
    {
        const woort_GCStruct* const src = _unboxed.m_struct;

        woort_set_struct(result_slot + 0, src->m_size);
        woort_GCStruct* const dst = woort_internal_value(result_slot + 0)->m_struct;

        for (size_t i = 0; i < src->m_size; i++)
        {
            woort_GC_mixed_write_barrier_value(&dst->m_datas[i], src->m_datas[i]);
        }
        break;
    }
    default:
        return woort_ret_value(0);
    }

    return woort_ret_value(result_slot + 0);
}

static woort_api woort_builtin_serialize_dynamic(void)
{
    char* const result = woort_serialize_dynbox(0, WOORT_SERIALIZE_FLAG_STRICT);

    if (result != NULL)
    {
        const woort_api v = woort_ret_option_string(result);
        free(result);

        return v;
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_deserialize_dynamic(void)
{
    woort_StackValue result_slot;
    if (!woort_push_reserve(1, &result_slot))
        return woort_ret_panic("Stack overflow.");

    if (woort_deserialize_dynbox(result_slot, woort_string(0)))
        return woort_ret_option_value(result_slot);

    return woort_ret_option_none();
}

static woort_api woort_builtin_char_tostring(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    size_t len;
    char result[WOORT_UTF8MAXLEN];

    woort_u32exractu8(wc, result, &len);

    return woort_ret_buffer(result, len);
}

static woort_api woort_builtin_char_toupper(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_int(
        (woort_Int)(
            woort_u32isu16(wc)
            ? (char32_t)towupper((wchar_t)(wc))
            : wc
            ));
}

static woort_api woort_builtin_char_tolower(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_int(
        (woort_Int)(
            woort_u32isu16(wc)
            ? (char32_t)towlower((wchar_t)(wc))
            : wc
            ));
}

static woort_api woort_builtin_char_isspace(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_bool(
        woort_u32isu16(wc) && iswspace((wchar_t)(wc)));
}

static woort_api woort_builtin_char_isalpha(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_bool(
        !woort_u32isu16(wc) || iswalpha((wchar_t)(wc)));
}

static woort_api woort_builtin_char_isalnum(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_bool(
        !woort_u32isu16(wc) || iswalnum((wchar_t)(wc)));
}

static woort_api woort_builtin_char_isnumber(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_bool(
        woort_u32isu16(wc) && iswdigit((wchar_t)(wc)));
}

static woort_api woort_builtin_char_ishex(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_bool(
        woort_u32isu16(wc) && iswxdigit((wchar_t)(wc)));
}

static woort_api woort_builtin_char_isoct(void)
{
    const char32_t wc = (char32_t)woort_int(0);

    return woort_ret_bool(
        wc >= (char32_t)'0' && wc <= (char32_t)'7');
}

static woort_api woort_builtin_char_hexnum(void)
{
    const char32_t wc = (char32_t)woort_int(0);
    if (!woort_u32isu16(wc) || !iswxdigit((wchar_t)(wc)))
        return woort_ret_panic("Non-hexadecimal character.");

    int val;
    if (wc >= (char32_t)'0' && wc <= (char32_t)'9')
        val = (int)(wc - (char32_t)'0');
    else if (wc >= (char32_t)'A' && wc <= (char32_t)'F')
        val = 10 + (int)(wc - (char32_t)'A');
    else
        val = 10 + (int)(wc - (char32_t)'a');

    return woort_ret_int((woort_Int)val);
}

static woort_api woort_builtin_char_octnum(void)
{
    const char32_t wc = (char32_t)woort_int(0);
    if (wc < (char32_t)'0' || wc > (char32_t)'7')
        return woort_ret_panic("Non-octal character.");

    return woort_ret_int((woort_Int)(wc - (char32_t)'0'));
}

static woort_api woort_builtin_take_token(void)
{
    const char* input = woort_string(0);
    const char* format = woort_string(1);

    size_t format_len = 0;
    while (format[format_len])
        format_len++;

    /* Every '%' doubles, plus trailing "%zn\0" */
    size_t buf_size = format_len * 2 + 4;
    char* matching_format = (char*)malloc(buf_size);
    if (matching_format == NULL)
        return woort_ret_panic("Out of memory.");

    size_t j = 0;
    for (size_t i = 0; format[i]; i++)
    {
        matching_format[j++] = format[i];
        if (format[i] == '%')
            matching_format[j++] = '%';
    }
    matching_format[j++] = '%';
    matching_format[j++] = 'z';
    matching_format[j++] = 'n';
    matching_format[j] = '\0';

    size_t token_length = 0;
    int result = sscanf(input, matching_format, &token_length);
    free(matching_format);

    if (result >= 0)
        return woort_ret_option_string(input + token_length);

    return woort_ret_option_none();
}

static woort_api woort_builtin_take_string(void)
{
    const char* input = woort_string(0);
    size_t token_length;
    char string_buf[1024];

    if (sscanf(input, "%s%zn", string_buf, &token_length) == 1)
    {
        woort_StackValue base;
        if (!woort_push_reserve(2, &base))
            return woort_ret_panic("Stack overflow.");

        woort_StackValue result = base + 0;
        woort_StackValue elem = base + 1;

        woort_set_struct(result, 2);

        woort_set_string(elem, input + token_length);
        woort_struct_set(result, 0, elem);
        woort_set_string(elem, string_buf);
        woort_struct_set(result, 1, elem);

        return woort_ret_option_value(result);
    }

    return woort_ret_option_none();
}

static woort_api woort_builtin_take_int(void)
{
    const char* input = woort_string(0);
    size_t token_length;
    long long integer;

    if (sscanf(input, "%lld%zn", &integer, &token_length) == 1)
    {
        woort_StackValue base;
        if (!woort_push_reserve(2, &base))
            return woort_ret_panic("Stack overflow.");

        woort_StackValue result = base + 0;
        woort_StackValue elem = base + 1;

        woort_set_struct(result, 2);

        woort_set_string(elem, input + token_length);
        woort_struct_set(result, 0, elem);
        woort_set_int(elem, (woort_Int)integer);
        woort_struct_set(result, 1, elem);

        return woort_ret_option_value(result);
    }

    return woort_ret_option_none();
}

static woort_api woort_builtin_take_real(void)
{
    const char* input = woort_string(0);
    size_t token_length;
    double real_val;

    if (sscanf(input, "%lf%zn", &real_val, &token_length) == 1)
    {
        woort_StackValue base;
        if (!woort_push_reserve(2, &base))
            return woort_ret_panic("Stack overflow.");

        woort_StackValue result = base + 0;
        woort_StackValue elem = base + 1;

        woort_set_struct(result, 2);

        woort_set_string(elem, input + token_length);
        woort_struct_set(result, 0, elem);
        woort_set_real(elem, (woort_Real)real_val);
        woort_struct_set(result, 1, elem);

        return woort_ret_option_value(result);
    }

    return woort_ret_option_none();
}

static woort_api woort_builtin_create_wchars_from_str(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* str = (const char*)raw;

    size_t u32_len = 0;
    char32_t* buf = woort_u8strtou32(str, len, &u32_len);
    if (buf == NULL)
        return woort_ret_panic("Out of memory.");

    woort_StackValue base;
    if (!woort_push_reserve(1, &base))
    {
        free(buf);
        return woort_ret_panic("Stack overflow.");
    }

    woort_StackValue vec_slot = base + 0;
    woort_set_vec(vec_slot);
    woort_vec_resize(vec_slot, u32_len);

    for (size_t i = 0; i < u32_len; i++)
    {
        woort_set_box_int(base + 0, (woort_Int)buf[i]);
        woort_vec_set(vec_slot, i, base + 0);
    }

    free(buf);
    return woort_ret_value(vec_slot);
}

static woort_api woort_builtin_create_chars_from_str(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* str = (const char*)raw;

    woort_StackValue base;
    if (!woort_push_reserve(1, &base))
        return woort_ret_panic("Stack overflow.");

    woort_StackValue vec_slot = base + 0;
    woort_set_vec(vec_slot);
    woort_vec_resize(vec_slot, len);

    for (size_t i = 0; i < len; i++)
    {
        woort_set_box_int(base + 0, (woort_Int)(unsigned char)str[i]);
        woort_vec_set(vec_slot, i, base + 0);
    }

    return woort_ret_value(vec_slot);
}

static woort_api woort_builtin_get_ascii_val_from_str(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* str = (const char*)raw;

    size_t idx = (size_t)woort_int(1);
    if (idx >= len)
        return woort_ret_panic("Index out of range.");

    return woort_ret_int((woort_Int)(unsigned char)str[idx]);
}

/* ================================================================
 * String functions
 * ================================================================ */

static woort_api woort_builtin_str_char_len(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);

    return woort_ret_int((woort_Int)woort_u8strnlen((const char*)raw, len));
}

static woort_api woort_builtin_str_byte_len(void)
{
    size_t len = 0;
    woort_buffer(0, &len);

    return woort_ret_int((woort_Int)len);
}

static woort_api woort_builtin_string_sub(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);

    size_t sub_len = 0;
    const char* sub = woort_u8substr((const char*)raw, len,
        (size_t)woort_int(1), &sub_len);

    return woort_ret_buffer(sub, sub_len);
}

static woort_api woort_builtin_string_sub_len(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);

    size_t sub_len = 0;
    const char* sub = woort_u8substrn((const char*)raw, len,
        (size_t)woort_int(1), (size_t)woort_int(2), &sub_len);

    return woort_ret_buffer(sub, sub_len);
}

static woort_api woort_builtin_string_sub_range(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);

    size_t sub_len = 0;
    const char* sub = woort_u8substrr((const char*)raw, len,
        (size_t)woort_int(1), (size_t)woort_int(2), &sub_len);

    return woort_ret_buffer(sub, sub_len);
}

static woort_api woort_builtin_string_toupper(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);

    size_t u32_len = 0;
    char32_t* u32 = woort_u8strtou32((const char*)raw, len, &u32_len);
    if (u32 == NULL)
        return woort_ret_panic("Out of memory.");

    for (size_t i = 0; i < u32_len; i++)
    {
        if (woort_u32isu16(u32[i]))
            u32[i] = (char32_t)towupper((wchar_t)u32[i]);
    }

    size_t u8_len = 0;
    char* u8 = woort_u32strtou8(u32, u32_len, &u8_len);
    free(u32);

    if (u8 == NULL)
        return woort_ret_panic("Out of memory.");

    const woort_api r = woort_ret_buffer(u8, u8_len);
    free(u8);
    return r;
}

static woort_api woort_builtin_string_tolower(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);

    size_t u32_len = 0;
    char32_t* u32 = woort_u8strtou32((const char*)raw, len, &u32_len);
    if (u32 == NULL)
        return woort_ret_panic("Out of memory.");

    for (size_t i = 0; i < u32_len; i++)
    {
        if (woort_u32isu16(u32[i]))
            u32[i] = (char32_t)towlower((wchar_t)u32[i]);
    }

    size_t u8_len = 0;
    char* u8 = woort_u32strtou8(u32, u32_len, &u8_len);
    free(u32);

    if (u8 == NULL)
        return woort_ret_panic("Out of memory.");

    const woort_api r = woort_ret_buffer(u8, u8_len);
    free(u8);
    return r;
}

static woort_api woort_builtin_string_isspace(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* p = (const char*)raw;
    const char* end = p + len;

    if (len == 0)
        return woort_ret_bool(false);

    while (p < end)
    {
        char32_t ch;
        size_t chsz = woort_u8combineu32(p, (size_t)(end - p), &ch);
        if (chsz == 0)
            break;

        if (!woort_u32isu16(ch) || !iswspace((wchar_t)ch))
            return woort_ret_bool(false);

        p += chsz;
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_isalpha(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* p = (const char*)raw;
    const char* end = p + len;

    if (len == 0)
        return woort_ret_bool(false);

    while (p < end)
    {
        char32_t ch;
        size_t chsz = woort_u8combineu32(p, (size_t)(end - p), &ch);
        if (chsz == 0)
            break;

        if (woort_u32isu16(ch) && !iswalpha((wchar_t)ch))
            return woort_ret_bool(false);

        p += chsz;
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_isalnum(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* p = (const char*)raw;
    const char* end = p + len;

    if (len == 0)
        return woort_ret_bool(false);

    while (p < end)
    {
        char32_t ch;
        size_t chsz = woort_u8combineu32(p, (size_t)(end - p), &ch);
        if (chsz == 0)
            break;

        if (woort_u32isu16(ch) && !iswalnum((wchar_t)ch))
            return woort_ret_bool(false);

        p += chsz;
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_isnumber(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* p = (const char*)raw;
    const char* end = p + len;

    if (len == 0)
        return woort_ret_bool(false);

    while (p < end)
    {
        char32_t ch;
        size_t chsz = woort_u8combineu32(p, (size_t)(end - p), &ch);
        if (chsz == 0)
            break;

        if (!woort_u32isu16(ch) || !iswdigit((wchar_t)ch))
            return woort_ret_bool(false);

        p += chsz;
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_ishex(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* p = (const char*)raw;
    const char* end = p + len;

    if (len == 0)
        return woort_ret_bool(false);

    while (p < end)
    {
        char32_t ch;
        size_t chsz = woort_u8combineu32(p, (size_t)(end - p), &ch);
        if (chsz == 0)
            break;

        if (!woort_u32isu16(ch) || !iswxdigit((wchar_t)ch))
            return woort_ret_bool(false);

        p += chsz;
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_isoct(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* p = (const char*)raw;
    const char* end = p + len;

    if (len == 0)
        return woort_ret_bool(false);

    while (p < end)
    {
        char32_t ch;
        size_t chsz = woort_u8combineu32(p, (size_t)(end - p), &ch);
        if (chsz == 0)
            break;

        if (ch < (char32_t)'0' || ch > (char32_t)'7')
            return woort_ret_bool(false);

        p += chsz;
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_enstring(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);

    char* result = woort_u8enstring((const char*)raw, len, 0);
    if (result == NULL)
        return woort_ret_panic("Out of memory.");

    const woort_api r = woort_ret_string(result);
    free(result);
    return r;
}

static woort_api woort_builtin_string_destring(void)
{
    const char* enstr = woort_string(0);

    char* result = woort_u8destring(enstr);
    if (result == NULL)
        return woort_ret_panic("Out of memory.");

    size_t result_len = strlen(result);
    const woort_api r = woort_ret_buffer(result, result_len);
    free(result);
    return r;
}

static woort_api woort_builtin_string_beginwith(void)
{
    size_t aim_len = 0;
    const void* aim_raw = woort_buffer(0, &aim_len);
    size_t begin_len = 0;
    const void* begin_raw = woort_buffer(1, &begin_len);

    const char* aim = (const char*)aim_raw;
    const char* begin = (const char*)begin_raw;

    if (begin_len > aim_len)
        return woort_ret_bool(false);

    for (size_t i = 0; i < begin_len; i++)
    {
        if (aim[i] != begin[i])
            return woort_ret_bool(false);
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_endwith(void)
{
    size_t aim_len = 0;
    const void* aim_raw = woort_buffer(0, &aim_len);
    size_t end_len = 0;
    const void* end_raw = woort_buffer(1, &end_len);

    const char* aim = (const char*)aim_raw;
    const char* end = (const char*)end_raw;

    if (end_len > aim_len)
        return woort_ret_bool(false);

    for (size_t i = 0; i < end_len; i++)
    {
        if (aim[aim_len - end_len + i] != end[i])
            return woort_ret_bool(false);
    }

    return woort_ret_bool(true);
}

static woort_api woort_builtin_string_replace(void)
{
    size_t aim_len = 0;
    const void* aim_raw = woort_buffer(0, &aim_len);
    size_t match_len = 0;
    const void* match_raw = woort_buffer(1, &match_len);
    size_t repl_len = 0;
    const void* repl_raw = woort_buffer(2, &repl_len);

    const char* aim = (const char*)aim_raw;
    const char* match = (const char*)match_raw;
    const char* repl = (const char*)repl_raw;

    /* Build result string with replace-all */
    size_t cap = aim_len * 2 + 1;
    if (cap < aim_len + 1)
        cap = aim_len + 1;
    char* buf = (char*)malloc(cap);
    if (buf == NULL)
        return woort_ret_panic("Out of memory.");

    size_t out = 0;
    size_t pos = 0;

    while (pos <= aim_len)
    {
        /* Try to find match at current position */
        size_t remaining = aim_len - pos;
        int found = 0;

        if (match_len <= remaining)
        {
            found = 1;
            for (size_t j = 0; j < match_len; j++)
            {
                if (aim[pos + j] != match[j])
                {
                    found = 0;
                    break;
                }
            }
        }

        if (found && match_len > 0)
        {
            /* Replace */
            while (out + repl_len >= cap)
            {
                cap *= 2;
                char* new_buf = (char*)realloc(buf, cap);
                if (new_buf == NULL) { free(buf); return woort_ret_panic("Out of memory."); }
                buf = new_buf;
            }
            for (size_t j = 0; j < repl_len; j++)
                buf[out++] = repl[j];
            pos += match_len;
        }
        else
        {
            /* Copy character */
            if (pos >= aim_len)
                break;

            if (out + 1 >= cap)
            {
                cap *= 2;
                char* new_buf = (char*)realloc(buf, cap);
                if (new_buf == NULL) { free(buf); return woort_ret_panic("Out of memory."); }
                buf = new_buf;
            }
            buf[out++] = aim[pos++];
        }
    }

    const woort_api r = woort_ret_buffer(buf, out);
    free(buf);
    return r;
}

static woort_api woort_builtin_string_find(void)
{
    size_t aim_len = 0;
    const void* aim_raw = woort_buffer(0, &aim_len);
    size_t match_len = 0;
    const void* match_raw = woort_buffer(1, &match_len);

    size_t u32_aim_len = 0;
    char32_t* aim_u32 = woort_u8strtou32((const char*)aim_raw, aim_len, &u32_aim_len);
    if (aim_u32 == NULL && aim_len > 0)
        return woort_ret_panic("Out of memory.");

    if (match_len == 0)
    {
        free(aim_u32);
        return woort_ret_option_int(0);
    }

    size_t u32_match_len = 0;
    char32_t* match_u32 = woort_u8strtou32((const char*)match_raw, match_len, &u32_match_len);
    if (match_u32 == NULL)
    {
        free(aim_u32);
        return woort_ret_panic("Out of memory.");
    }

    /* Find match in aim by char32 comparison */
    woort_Int result = -1;
    for (size_t i = 0; i + u32_match_len <= u32_aim_len; i++)
    {
        int ok = 1;
        for (size_t j = 0; j < u32_match_len; j++)
        {
            if (aim_u32[i + j] != match_u32[j])
            {
                ok = 0;
                break;
            }
        }
        if (ok)
        {
            result = (woort_Int)i;
            break;
        }
    }

    free(aim_u32);
    free(match_u32);

    if (result >= 0)
        return woort_ret_option_int(result);

    return woort_ret_option_none();
}

static woort_api woort_builtin_string_find_from(void)
{
    size_t aim_len = 0;
    const void* aim_raw = woort_buffer(0, &aim_len);
    size_t match_len = 0;
    const void* match_raw = woort_buffer(1, &match_len);
    size_t from = (size_t)woort_int(2);

    size_t u32_aim_len = 0;
    char32_t* aim_u32 = woort_u8strtou32((const char*)aim_raw, aim_len, &u32_aim_len);
    if (aim_u32 == NULL)
        return woort_ret_panic("Out of memory.");

    if (match_len == 0)
    {
        free(aim_u32);
        woort_Int r = ((size_t)from < u32_aim_len) ? (woort_Int)from : (woort_Int)u32_aim_len;
        return woort_ret_option_int(r);
    }

    size_t u32_match_len = 0;
    char32_t* match_u32 = woort_u8strtou32((const char*)match_raw, match_len, &u32_match_len);
    if (match_u32 == NULL)
    {
        free(aim_u32);
        return woort_ret_panic("Out of memory.");
    }

    woort_Int result = -1;
    for (size_t i = (size_t)from; i + u32_match_len <= u32_aim_len; i++)
    {
        int ok = 1;
        for (size_t j = 0; j < u32_match_len; j++)
        {
            if (aim_u32[i + j] != match_u32[j])
            {
                ok = 0;
                break;
            }
        }
        if (ok)
        {
            result = (woort_Int)i;
            break;
        }
    }

    free(aim_u32);
    free(match_u32);

    if (result >= 0)
        return woort_ret_option_int(result);

    return woort_ret_option_none();
}

static woort_api woort_builtin_string_rfind(void)
{
    size_t aim_len = 0;
    const void* aim_raw = woort_buffer(0, &aim_len);
    size_t match_len = 0;
    const void* match_raw = woort_buffer(1, &match_len);

    size_t u32_aim_len = 0;
    char32_t* aim_u32 = woort_u8strtou32((const char*)aim_raw, aim_len, &u32_aim_len);
    if (aim_u32 == NULL)
        return woort_ret_panic("Out of memory.");

    size_t u32_match_len = 0;
    char32_t* match_u32 = woort_u8strtou32((const char*)match_raw, match_len, &u32_match_len);
    if (match_u32 == NULL)
    {
        free(aim_u32);
        return woort_ret_panic("Out of memory.");
    }

    woort_Int result = -1;
    if (u32_match_len == 0)
    {
        result = (woort_Int)u32_aim_len;
    }
    else
    {
        for (size_t i = u32_aim_len; i-- > 0; )
        {
            if (i + u32_match_len > u32_aim_len)
                continue;
            int ok = 1;
            for (size_t j = 0; j < u32_match_len; j++)
            {
                if (aim_u32[i + j] != match_u32[j])
                {
                    ok = 0;
                    break;
                }
            }
            if (ok)
            {
                result = (woort_Int)i;
                break;
            }
        }
    }

    free(aim_u32);
    free(match_u32);

    if (result >= 0)
        return woort_ret_option_int(result);

    return woort_ret_option_none();
}

static woort_api woort_builtin_string_rfind_from(void)
{
    size_t aim_len = 0;
    const void* aim_raw = woort_buffer(0, &aim_len);
    size_t match_len = 0;
    const void* match_raw = woort_buffer(1, &match_len);
    size_t from = (size_t)woort_int(2);

    size_t u32_aim_len = 0;
    char32_t* aim_u32 = woort_u8strtou32((const char*)aim_raw, aim_len, &u32_aim_len);
    if (aim_u32 == NULL)
        return woort_ret_panic("Out of memory.");

    size_t u32_match_len = 0;
    char32_t* match_u32 = woort_u8strtou32((const char*)match_raw, match_len, &u32_match_len);
    if (match_u32 == NULL)
    {
        free(aim_u32);
        return woort_ret_panic("Out of memory.");
    }

    woort_Int result = -1;
    for (size_t i = (from < u32_aim_len) ? from + 1 : u32_aim_len; i-- > 0; )
    {
        if (u32_match_len == 0)
        {
            result = (woort_Int)i;
            break;
        }
        if (i + u32_match_len > u32_aim_len)
            continue;
        int ok = 1;
        for (size_t j = 0; j < u32_match_len; j++)
        {
            if (aim_u32[i + j] != match_u32[j])
            {
                ok = 0;
                break;
            }
        }
        if (ok)
        {
            result = (woort_Int)i;
            break;
        }
    }

    free(aim_u32);
    free(match_u32);

    if (result >= 0 && (size_t)result < from)
        return woort_ret_option_int(result);

    return woort_ret_option_none();
}

static woort_api woort_builtin_string_trim(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* str = (const char*)raw;

    size_t u32_len = 0;
    char32_t* u32 = woort_u8strtou32(str, len, &u32_len);
    if (u32 == NULL)
        return woort_ret_panic("Out of memory.");

    size_t ibeg = 0;
    size_t iend = u32_len;

    while (ibeg != iend)
    {
        char32_t ch = u32[ibeg];
        if (woort_u32isu16(ch) && (iswspace((wchar_t)ch) || iswcntrl((wchar_t)ch)))
            ibeg++;
        else
            break;
    }

    while (iend != ibeg)
    {
        char32_t ch = u32[iend - 1];
        if (woort_u32isu16(ch) && (iswspace((wchar_t)ch) || iswcntrl((wchar_t)ch)))
            iend--;
        else
            break;
    }

    /* Convert trimmed u32 range back to UTF-8 */
    /* We need to find the byte offset and byte length of the trimmed substring */
    size_t byte_begin = 0;
    const char* p = str;
    for (size_t i = 0; i < ibeg; i++)
    {
        size_t chsz = 0;
        woort_u8combineu32(p, len - (size_t)(p - str), NULL);
        /* Advance by actual UTF-8 char size */
        char32_t dummy;
        chsz = woort_u8combineu32(p, len - (size_t)(p - str), &dummy);
        p += chsz;
    }
    byte_begin = (size_t)(p - str);

    size_t byte_end = byte_begin;
    for (size_t i = ibeg; i < iend; i++)
    {
        char32_t dummy;
        size_t chsz = woort_u8combineu32(p, len - (size_t)(p - str), &dummy);
        p += chsz;
    }
    byte_end = (size_t)(p - str);

    free(u32);

    return woort_ret_buffer(str + byte_begin, byte_end - byte_begin);
}

typedef struct string_split_iter_t
{
    char* m_str;        /* heap-allocated copy of original string */
    size_t m_str_len;
    char* m_sep;        /* heap-allocated copy of separator */
    size_t m_sep_len;
    size_t m_split_from;
} string_split_iter_t;

static void string_split_iter_destroy(void* p)
{
    string_split_iter_t* iter = (string_split_iter_t*)p;
    free(iter->m_str);
    free(iter->m_sep);
    free(iter);
}

static woort_api woort_builtin_string_split(void)
{
    size_t str_len = 0;
    const void* str_raw = woort_buffer(0, &str_len);
    const char* sep = woort_string(1);
    size_t sep_len = strlen(sep);

    string_split_iter_t* iter = (string_split_iter_t*)malloc(sizeof(string_split_iter_t));
    if (iter == NULL)
        return woort_ret_panic("Out of memory.");

    iter->m_str = (char*)malloc(str_len + 1);
    if (iter->m_str == NULL)
    {
        free(iter);
        return woort_ret_panic("Out of memory.");
    }
    memcpy(iter->m_str, (const char*)str_raw, str_len);
    iter->m_str[str_len] = '\0';
    iter->m_str_len = str_len;

    iter->m_sep = (char*)malloc(sep_len + 1);
    if (iter->m_sep == NULL)
    {
        free(iter->m_str);
        free(iter);
        return woort_ret_panic("Out of memory.");
    }
    memcpy(iter->m_sep, sep, sep_len + 1);
    iter->m_sep_len = sep_len;
    iter->m_split_from = 0;

    return woort_set_gchandle(-1, iter, 0, string_split_iter_destroy, NULL),
        WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api woort_builtin_string_split_iter(void)
{
    void* ptr = woort_gcpointer(0);
    string_split_iter_t* iter = (string_split_iter_t*)ptr;

    if (iter->m_split_from > iter->m_str_len)
        return woort_ret_option_none();

    if (iter->m_sep_len == 0)
    {
        /* Split by character */
        const char* split_str_p = iter->m_str + iter->m_split_from;
        size_t split_str_len = iter->m_str_len - iter->m_split_from;

        if (split_str_len == 0)
        {
            ++iter->m_split_from;
            return woort_ret_option_none();
        }

        size_t chlen = woort_u8charnlen(split_str_p, split_str_len);
        iter->m_split_from += chlen;
        return woort_ret_option_buffer(split_str_p, chlen);
    }
    else
    {
        /* Find separator */
        const char* fnd = NULL;
        for (size_t i = iter->m_split_from; i + iter->m_sep_len <= iter->m_str_len; i++)
        {
            int match = 1;
            for (size_t j = 0; j < iter->m_sep_len; j++)
            {
                if (iter->m_str[i + j] != iter->m_sep[j])
                {
                    match = 0;
                    break;
                }
            }
            if (match)
            {
                fnd = iter->m_str + i;
                break;
            }
        }

        if (fnd == NULL)
        {
            /* No separator found: return remaining string */
            const char* view = iter->m_str + iter->m_split_from;
            size_t view_len = iter->m_str_len - iter->m_split_from;

            iter->m_split_from = iter->m_str_len + 1;

            return woort_ret_option_buffer(view, view_len);
        }

        size_t fnd_pos = (size_t)(fnd - iter->m_str);
        const char* view = iter->m_str + iter->m_split_from;
        size_t view_len = fnd_pos - iter->m_split_from;

        iter->m_split_from = fnd_pos + iter->m_sep_len;
        return woort_ret_option_buffer(view, view_len);
    }
}

static woort_api woort_builtin_string_append_char(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    char32_t wc = (char32_t)woort_int(1);

    char u8buf[WOORT_UTF8MAXLEN];
    size_t u8len;
    woort_u32exractu8(wc, u8buf, &u8len);

    size_t result_len = len + u8len;
    char* result = (char*)malloc(result_len);
    if (result == NULL)
        return woort_ret_panic("Out of memory.");

    memcpy(result, (const char*)raw, len);
    memcpy(result + len, u8buf, u8len);

    const woort_api r = woort_ret_buffer(result, result_len);
    free(result);
    return r;
}

static woort_api woort_builtin_string_append_cchar(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    char ch = (char)(woort_Int)woort_int(1);

    char* result = (char*)malloc(len + 1);
    if (result == NULL)
        return woort_ret_panic("Out of memory.");

    memcpy(result, (const char*)raw, len);
    result[len] = ch;

    const woort_api r = woort_ret_buffer(result, len + 1);
    free(result);
    return r;
}

/* ================================================================
 * Function table for the "woolang" fake library
 * ================================================================ */

#define WOORT_BUILTIN_FUNC(name) \
    {"woostd_" #name, (void*)&woort_builtin_##name}

static const woort_ExternLibFunc g_woolang_funcs[] = {
    WOORT_BUILTIN_FUNC(return_it_self),
    WOORT_BUILTIN_FUNC(bad_function),

    WOORT_BUILTIN_FUNC(panic),
    WOORT_BUILTIN_FUNC(print),
    WOORT_BUILTIN_FUNC(input_read_i),
    WOORT_BUILTIN_FUNC(input_read_r),
    WOORT_BUILTIN_FUNC(input_read_s),
    WOORT_BUILTIN_FUNC(input_readline),
    WOORT_BUILTIN_FUNC(random_i),
    WOORT_BUILTIN_FUNC(random_r),
    WOORT_BUILTIN_FUNC(yield),
    WOORT_BUILTIN_FUNC(cmdlines),
    WOORT_BUILTIN_FUNC(host_path),
    WOORT_BUILTIN_FUNC(sleep),
    WOORT_BUILTIN_FUNC(is_same),
    WOORT_BUILTIN_FUNC(make_dup),

    WOORT_BUILTIN_FUNC(serialize_dynamic),
    WOORT_BUILTIN_FUNC(deserialize_dynamic),

    WOORT_BUILTIN_FUNC(char_tostring),
    WOORT_BUILTIN_FUNC(char_toupper),
    WOORT_BUILTIN_FUNC(char_tolower),
    WOORT_BUILTIN_FUNC(char_isspace),
    WOORT_BUILTIN_FUNC(char_isalpha),
    WOORT_BUILTIN_FUNC(char_isalnum),
    WOORT_BUILTIN_FUNC(char_isnumber),
    WOORT_BUILTIN_FUNC(char_ishex),
    WOORT_BUILTIN_FUNC(char_isoct),
    WOORT_BUILTIN_FUNC(char_hexnum),
    WOORT_BUILTIN_FUNC(char_octnum),

    WOORT_BUILTIN_FUNC(take_token),
    WOORT_BUILTIN_FUNC(take_string),
    WOORT_BUILTIN_FUNC(take_int),
    WOORT_BUILTIN_FUNC(take_real),
    WOORT_BUILTIN_FUNC(create_wchars_from_str),
    WOORT_BUILTIN_FUNC(create_chars_from_str),
    WOORT_BUILTIN_FUNC(get_ascii_val_from_str),

    WOORT_BUILTIN_FUNC(str_char_len),
    WOORT_BUILTIN_FUNC(str_byte_len),
    WOORT_BUILTIN_FUNC(string_sub),
    WOORT_BUILTIN_FUNC(string_sub_len),
    WOORT_BUILTIN_FUNC(string_sub_range),
    WOORT_BUILTIN_FUNC(string_toupper),
    WOORT_BUILTIN_FUNC(string_tolower),
    WOORT_BUILTIN_FUNC(string_isspace),
    WOORT_BUILTIN_FUNC(string_isalpha),
    WOORT_BUILTIN_FUNC(string_isalnum),
    WOORT_BUILTIN_FUNC(string_isnumber),
    WOORT_BUILTIN_FUNC(string_ishex),
    WOORT_BUILTIN_FUNC(string_isoct),
    WOORT_BUILTIN_FUNC(string_enstring),
    WOORT_BUILTIN_FUNC(string_destring),
    WOORT_BUILTIN_FUNC(string_beginwith),
    WOORT_BUILTIN_FUNC(string_endwith),
    WOORT_BUILTIN_FUNC(string_replace),
    WOORT_BUILTIN_FUNC(string_find),
    WOORT_BUILTIN_FUNC(string_find_from),
    WOORT_BUILTIN_FUNC(string_rfind),
    WOORT_BUILTIN_FUNC(string_rfind_from),
    WOORT_BUILTIN_FUNC(string_trim),
    WOORT_BUILTIN_FUNC(string_split),
    WOORT_BUILTIN_FUNC(string_split_iter),
    WOORT_BUILTIN_FUNC(string_append_char),
    WOORT_BUILTIN_FUNC(string_append_cchar),

    WOORT_EXTERN_LIB_FUNC_END,
};

bool _woort_builtin_bootup(int argc, char** argv)
{
    g_cmdlines_argc = argc;
    g_cmdlines_argv = argv;

    g_builtin_lib = woort_dylib_fake("woolang", g_woolang_funcs, NULL);

    /* Initialize random seed with event-based entropy */
    {
        uint64_t seed = ((uint64_t)(uintptr_t)&g_random_state) * 0x9E3779B97F4A7C15ULL;
        seed ^= (uint64_t)clock();
        seed += (uint64_t)time(NULL);

        /* SplitMix64 mixing pass */
        seed += 0x9E3779B97F4A7C15ULL;
        seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBULL;
        seed = seed ^ (seed >> 31);

        woort_atomic_init(&g_random_state, seed);
    }

    return g_builtin_lib != NULL;
}

void _woort_builtin_shutdown(void)
{
    if (g_builtin_lib != NULL)
    {
        woort_dylib_unload(g_builtin_lib, WOORT_DYLIB_UNREF_AND_BURY);
        g_builtin_lib = NULL;
    }
}

WOORT_NODISCARD /* OPTIONAL */ woort_Dylib* woort_get_builtin_lib(void)
{
    return g_builtin_lib;
}
