#include "woort.h"

#include "woort_builtin.h"

#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_threads.h"

#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc_gchandle.h"
#include "woort_gc.h"
#include "woort_utf8.h"
#include "woort_waipo_debugger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <time.h>

/* ================================================================
 * Global handle and lifecycle
 * ================================================================ */

static /* OPTIONAL */ woort_Dylib* g_builtin_lib = NULL;
static woort_AtomicUInt64          g_random_state;

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
            else
            {
                fputs(str, stdout);
                free(str);
            }
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
    {
        woort_VMRuntime* const last_vm = woort_vm_swap(NULL);
        {
            woort_thread_sleep_ms((uint32_t)(tm * 1000.0));
        }
        (void)woort_vm_swap(last_vm);
    }

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
    woort_StackValue elem_slot;
    if (!woort_push_reserve(1, &elem_slot))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);

    for (int i = 0; i < g_cmdlines_argc; i++)
    {
        woort_set_string(elem_slot, g_cmdlines_argv[i]);
        woort_vec_push(WOORT_RETURN_SLOT, elem_slot);
    }

    return woort_ret();
}
static woort_api woort_builtin_host_path(void)
{
    size_t need = woort_exe_path(NULL, 0) + 1;
    if (need <= 1)
        return woort_ret_string("");

    char* path = (char*)malloc(need);
    if (path == NULL)
        return woort_ret_panic("Out of memory.");

    (void)woort_exe_path(path, need);
    woort_set_string((woort_StackValue)-1, path);
    free(path);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api woort_builtin_make_dup(void)
{
    woort_set_dup_boxed(WOORT_RETURN_SLOT, 0);
    return woort_ret();
}

static woort_api woort_builtin_clock(void)
{
    return woort_ret_real(clock() / (woort_Real)CLOCKS_PER_SEC);
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
    if (woort_deserialize_dynbox(WOORT_RETURN_SLOT, woort_string(0)))
        return woort_ret_option_value(WOORT_RETURN_SLOT);

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
    if (wc < (char32_t)'0' || wc >(char32_t)'7')
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
        woort_StackValue elem;
        if (!woort_push_reserve(1, &elem))
            return woort_ret_panic("Stack overflow.");

        woort_set_struct(WOORT_RETURN_SLOT, 2);

        woort_set_string(elem, input + token_length);
        woort_struct_set(WOORT_RETURN_SLOT, 0, elem);
        woort_set_string(elem, string_buf);
        woort_struct_set(WOORT_RETURN_SLOT, 1, elem);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
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
        woort_StackValue elem;
        if (!woort_push_reserve(1, &elem))
            return woort_ret_panic("Stack overflow.");

        woort_set_struct(WOORT_RETURN_SLOT, 2);

        woort_set_string(elem, input + token_length);
        woort_struct_set(WOORT_RETURN_SLOT, 0, elem);
        woort_set_int(elem, (woort_Int)integer);
        woort_struct_set(WOORT_RETURN_SLOT, 1, elem);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
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
        woort_StackValue elem;
        if (!woort_push_reserve(1, &elem))
            return woort_ret_panic("Stack overflow.");

        woort_set_struct(WOORT_RETURN_SLOT, 2);

        woort_set_string(elem, input + token_length);
        woort_struct_set(WOORT_RETURN_SLOT, 0, elem);
        woort_set_real(elem, (woort_Real)real_val);
        woort_struct_set(WOORT_RETURN_SLOT, 1, elem);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
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

    woort_StackValue temp;
    if (!woort_push_reserve(1, &temp))
    {
        free(buf);
        return woort_ret_panic("Stack overflow.");
    }

    woort_set_vec(WOORT_RETURN_SLOT);
    woort_vec_resize(WOORT_RETURN_SLOT, u32_len);

    for (size_t i = 0; i < u32_len; i++)
    {
        woort_set_box_int(temp, (woort_Int)buf[i]);
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, temp);
    }

    free(buf);
    return woort_ret();
}

static woort_api woort_builtin_create_chars_from_str(void)
{
    size_t len = 0;
    const void* raw = woort_buffer(0, &len);
    const char* str = (const char*)raw;

    woort_StackValue temp;
    if (!woort_push_reserve(1, &temp))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);
    woort_vec_resize(WOORT_RETURN_SLOT, len);

    for (size_t i = 0; i < len; i++)
    {
        woort_set_box_int(temp, (woort_Int)(unsigned char)str[i]);
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, temp);
    }

    return woort_ret();
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
    (void)woort_buffer(0, &len);

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

        if (ch < (char32_t)'0' || ch >(char32_t)'7')
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

    size_t result_len = 0;
    char* result = woort_u8destring(enstr, &result_len);
    if (result == NULL)
        return woort_ret_panic("Out of memory.");

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
        /* Advance by actual UTF-8 char size */
        char32_t dummy;
        const size_t chsz = woort_u8combineu32(p, len - (size_t)(p - str), &dummy);
        p += chsz;
    }
    byte_begin = (size_t)(p - str);

    size_t byte_end = byte_begin;
    for (size_t i = ibeg; i < iend; i++)
    {
        char32_t dummy;
        const size_t chsz = woort_u8combineu32(p, len - (size_t)(p - str), &dummy);
        p += chsz;
    }
    byte_end = (size_t)(p - str);

    free(u32);

    return woort_ret_buffer(str + byte_begin, byte_end - byte_begin);
}

typedef struct string_split_iter_t
{
    const char* m_str;        /* heap-allocated copy of original string */
    size_t m_str_len;
    const char* m_sep;            /* heap-allocated copy of separator */
    size_t m_sep_len;
    size_t m_split_from;
} string_split_iter_t;

static woort_api woort_builtin_string_split(void)
{
    string_split_iter_t* const iter = malloc(sizeof(string_split_iter_t));
    if (iter == NULL)
        return woort_ret_panic("Out of memory.");

    iter->m_str = woort_buffer(0, &iter->m_str_len);
    iter->m_sep = woort_buffer(1, &iter->m_sep_len);
    iter->m_split_from = 0;

    return woort_set_gchandle(-1, iter, 0, free, NULL),
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
        const size_t split_str_len = iter->m_str_len - iter->m_split_from;

        if (split_str_len == 0)
        {
            ++iter->m_split_from;
            return woort_ret_option_none();
        }

        const size_t chlen = woort_u8charnlen(split_str_p, split_str_len);
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
 * Array / Vec operations
 * ================================================================ */

static woort_api woort_builtin_array_len(void)
{
    size_t len = woort_vec_len(0);
    return woort_ret_int((woort_Int)len);
}

static woort_api woort_builtin_array_empty(void)
{
    size_t len = woort_vec_len(0);
    return woort_ret_bool(len == 0);
}

static size_t _woort_builtin_array_create()
{
    woort_set_vec(WOORT_RETURN_SLOT);

    const size_t arrsz = (size_t)woort_int(0);
    woort_vec_resize(WOORT_RETURN_SLOT, arrsz);

    return arrsz;
}

static woort_api woort_builtin_array_create_i(void)
{
    const size_t arrsz = _woort_builtin_array_create();

    woort_StackValue box;
    if (!woort_push_reserve(1, &box))
        return woort_ret_panic("Stack overflow.");

    woort_set_box_int(box, woort_int(1));

    for (size_t i = 0; i < arrsz; i++)
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, box);

    return woort_ret();
}

static woort_api woort_builtin_array_create_r(void)
{
    const size_t arrsz = _woort_builtin_array_create();

    woort_StackValue box;
    if (!woort_push_reserve(1, &box))
        return woort_ret_panic("Stack overflow.");

    woort_set_box_real(box, woort_real(1));

    for (size_t i = 0; i < arrsz; i++)
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, box);

    return woort_ret();
}

static woort_api woort_builtin_array_create_b(void)
{
    const size_t arrsz = _woort_builtin_array_create();

    woort_StackValue box;
    if (!woort_push_reserve(1, &box))
        return woort_ret_panic("Stack overflow.");

    woort_set_box_bool(box, woort_bool(1));

    for (size_t i = 0; i < arrsz; i++)
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, box);

    return woort_ret();
}

static woort_api woort_builtin_array_create_x(void)
{
    const size_t arrsz = _woort_builtin_array_create();

    for (size_t i = 0; i < arrsz; i++)
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, 1);

    return woort_ret();
}

static woort_api woort_builtin_serialize_array(void)
{
    char* const result = woort_serialize_vec(0, WOORT_SERIALIZE_FLAG_STRICT);
    if (result != NULL)
    {
        const woort_api v = woort_ret_option_string(result);
        free(result);
        return v;
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_deserialize_array(void)
{
    if (woort_deserialize_vec(WOORT_RETURN_SLOT, woort_string(0)))
        return woort_ret_option_value(WOORT_RETURN_SLOT);

    return woort_ret_option_none();
}

static woort_api woort_builtin_create_str_by_wchar(void)
{
    const woort_GCVec* const vec = woort_internal_value(0)->m_vec;
    size_t len = vec->m_length;

    char32_t* u32 = (char32_t*)malloc(len * sizeof(char32_t));
    if (u32 == NULL && len > 0)
        return woort_ret_panic("Out of memory.");

    woort_StackValue elem_slot;
    if (!woort_push_reserve(1, &elem_slot))
    {
        free(u32);
        return woort_ret_panic("Stack overflow.");
    }

    for (size_t i = 0; i < len; i++)
    {
        (void)woort_vec_get(elem_slot, 0, i);
        u32[i] = (char32_t)woort_unbox_int(elem_slot);
    }

    size_t u8_len = 0;
    char* u8 = woort_u32strtou8(u32, len, &u8_len);
    free(u32);

    if (u8 == NULL)
        return woort_ret_panic("Out of memory.");

    const woort_api r = woort_ret_buffer(u8, u8_len);
    free(u8);
    return r;
}

static woort_api woort_builtin_create_str_by_ascii(void)
{
    const woort_GCVec* const vec = woort_internal_value(0)->m_vec;
    size_t len = vec->m_length;

    char* buf = (char*)malloc(len);
    if (buf == NULL && len > 0)
        return woort_ret_panic("Out of memory.");

    woort_StackValue elem_slot;
    if (!woort_push_reserve(1, &elem_slot))
    {
        free(buf);
        return woort_ret_panic("Stack overflow.");
    }

    for (size_t i = 0; i < len; i++)
    {
        (void)woort_vec_get(elem_slot, 0, i);
        buf[i] = (char)(woort_Int)woort_unbox_int(elem_slot);
    }

    const woort_api r = woort_ret_buffer(buf, len);
    free(buf);
    return r;
}

static woort_api woort_builtin_array_get_u(void)
{
    woort_Int idx = woort_int(1);
    size_t len = woort_vec_len(0);

    if (woort_vec_get(WOORT_RETURN_SLOT, 0, (size_t)idx))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }

    return woort_ret_option_none();
}
static woort_api woort_builtin_array_get_r(void)
{
    woort_Int idx = woort_int(1);
    size_t len = woort_vec_len(0);

    if (woort_vec_get(WOORT_RETURN_SLOT, 0, (size_t)idx))
    {
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }

    return woort_ret_option_none();
}

static woort_api woort_builtin_array_get_or_default_u(void)
{
    woort_Int idx = woort_int(1);
    size_t len = woort_vec_len(0);

    if (woort_vec_get(WOORT_RETURN_SLOT, 0, (size_t)idx))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }

    return woort_ret_value(2);
}
static woort_api woort_builtin_array_get_or_default_r(void)
{
    woort_Int idx = woort_int(1);
    size_t len = woort_vec_len(0);

    if (woort_vec_get(WOORT_RETURN_SLOT, 0, (size_t)idx))
    {
        return woort_ret();
    }

    return woort_ret_value(2);
}

static woort_api woort_builtin_array_find_i(void)
{
    woort_Int key = woort_int(1);
    size_t len = woort_vec_len(0);

    for (size_t i = 0; i < len; i++)
    {
        (void)woort_vec_get(WOORT_RETURN_SLOT, 0, i);

        if (woort_DynBox_equal_int(
            woort_internal_value(WOORT_RETURN_SLOT)->m_dynamic, key))
        {
            return woort_ret_option_int((woort_Int)i);
        }
    }

    return woort_ret_option_none();
}
static woort_api woort_builtin_array_find_r(void)
{
    woort_Real key = woort_real(1);
    size_t len = woort_vec_len(0);

    for (size_t i = 0; i < len; i++)
    {
        (void)woort_vec_get(WOORT_RETURN_SLOT, 0, i);

        if (woort_DynBox_equal_real(
            woort_internal_value(WOORT_RETURN_SLOT)->m_dynamic, key))
        {
            return woort_ret_option_int((woort_Int)i);
        }
    }

    return woort_ret_option_none();
}
static woort_api woort_builtin_array_find_b(void)
{
    bool key = woort_bool(1);
    size_t len = woort_vec_len(0);

    for (size_t i = 0; i < len; i++)
    {
        (void)woort_vec_get(WOORT_RETURN_SLOT, 0, i);

        if (woort_DynBox_equal_bool(
            woort_internal_value(WOORT_RETURN_SLOT)->m_dynamic, key))
        {
            return woort_ret_option_int((woort_Int)i);
        }
    }

    return woort_ret_option_none();
}
static woort_api woort_builtin_array_find_x(void)
{
    woort_DynBox key = woort_internal_value(1)->m_dynamic;
    size_t len = woort_vec_len(0);

    for (size_t i = 0; i < len; i++)
    {
        (void)woort_vec_get(WOORT_RETURN_SLOT, 0, i);

        if (woort_DynBox_equal(
            woort_internal_value(WOORT_RETURN_SLOT)->m_dynamic, key))
        {
            return woort_ret_option_int((woort_Int)i);
        }
    }

    return woort_ret_option_none();
}

typedef struct array_iter_t
{
    const woort_GCVec* m_vec;
    size_t m_index;

} array_iter_t;

static woort_api woort_builtin_array_iter(void)
{
    array_iter_t* iter = (array_iter_t*)malloc(sizeof(array_iter_t));
    if (iter == NULL)
        return woort_ret_panic("Out of memory.");

    iter->m_vec = woort_internal_value(0)->m_vec;
    iter->m_index = 0;

    return woort_ret_gchandle(iter, 0, free, NULL);
}

static woort_api woort_builtin_array_iter_next_u(void)
{
    void* ptr = woort_gcpointer(0);
    array_iter_t* iter = (array_iter_t*)ptr;

    woort_Value* dst = woort_internal_value(WOORT_RETURN_SLOT);

    if (woort_GCVec_get(iter->m_vec, iter->m_index, &dst->m_dynamic))
    {
        iter->m_index++;

        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}
static woort_api woort_builtin_array_iter_next_r(void)
{
    void* ptr = woort_gcpointer(0);
    array_iter_t* iter = (array_iter_t*)ptr;

    woort_Value* dst = woort_internal_value(WOORT_RETURN_SLOT);

    if (woort_GCVec_get(iter->m_vec, iter->m_index, &dst->m_dynamic))
    {
        iter->m_index++;
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_array_connect(void)
{
    woort_StackValue temp;
    if (!woort_push_reserve(1, &temp))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);

    size_t len1 = woort_vec_len(0);
    size_t len2 = woort_vec_len(1);
    woort_vec_resize(WOORT_RETURN_SLOT, len1 + len2);

    for (size_t i = 0; i < len1; i++)
    {
        (void)woort_vec_get(temp, 0, i);
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, temp);
    }

    for (size_t i = 0; i < len2; i++)
    {
        (void)woort_vec_get(temp, 1, i);
        (void)woort_vec_set(WOORT_RETURN_SLOT, len1 + i, temp);
    }

    return woort_ret();
}

static woort_api woort_builtin_array_sub(void)
{
    woort_StackValue temp;
    if (!woort_push_reserve(1, &temp))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);

    size_t begin = (size_t)woort_int(1);
    size_t src_len = woort_vec_len(0);

    if (begin > src_len)
        return woort_ret_panic("Index out of range when getting sub array/vec.");

    size_t sub_len = src_len - begin;
    woort_vec_resize(WOORT_RETURN_SLOT, sub_len);

    for (size_t i = 0; i < sub_len; i++)
    {
        (void)woort_vec_get(temp, 0, begin + i);
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, temp);
    }

    return woort_ret();
}

static woort_api woort_builtin_array_sub_to(void)
{
    woort_StackValue temp;
    if (!woort_push_reserve(1, &temp))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);

    size_t begin = (size_t)woort_int(1);
    size_t count = (size_t)woort_int(2);
    size_t src_len = woort_vec_len(0);

    if (begin > src_len || begin + count > src_len)
        return woort_ret_panic("Index out of range when getting sub array/vec.");

    woort_vec_resize(WOORT_RETURN_SLOT, count);
    for (size_t i = 0; i < count; i++)
    {
        (void)woort_vec_get(temp, 0, begin + i);
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, temp);
    }

    return woort_ret();
}

static woort_api woort_builtin_array_sub_range(void)
{
    woort_StackValue temp;
    if (!woort_push_reserve(1, &temp))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);

    size_t begin = (size_t)woort_int(1);
    size_t end = (size_t)woort_int(2);
    size_t src_len = woort_vec_len(0);

    if (begin > src_len || end > src_len)
        return woort_ret_panic("Index out of range when getting sub array/vec.");

    if (end <= begin)
    {
        woort_vec_resize(WOORT_RETURN_SLOT, 0);
        return woort_ret();
    }

    size_t count = end - begin;
    woort_vec_resize(WOORT_RETURN_SLOT, count);
    for (size_t i = 0; i < count; i++)
    {
        (void)woort_vec_get(temp, 0, begin + i);
        (void)woort_vec_set(WOORT_RETURN_SLOT, i, temp);
    }

    return woort_ret();
}

static woort_api woort_builtin_array_front_u(void)
{
    if (woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }

    return woort_ret_option_none();
}
static woort_api woort_builtin_array_front_r(void)
{
    if (woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
        return woort_ret_option_value(WOORT_RETURN_SLOT);

    return woort_ret_option_none();
}

static woort_api woort_builtin_array_back_u(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_option_none();

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
    return woort_ret_option_value(WOORT_RETURN_SLOT);
}
static woort_api woort_builtin_array_back_r(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_option_none();

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    return woort_ret_option_value(WOORT_RETURN_SLOT);
}

static woort_api woort_builtin_array_front_val_u(void)
{
    if (woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    return woort_ret_panic("Index out of range.");
}
static woort_api woort_builtin_array_front_val_r(void)
{
    if (woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
        return woort_ret();

    return woort_ret_panic("Index out of range.");
}

static woort_api woort_builtin_array_back_val_u(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_panic("Index out of range.");

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
    return woort_ret();
}
static woort_api woort_builtin_array_back_val_r(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_panic("Index out of range.");

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    return woort_ret();
}

static size_t _woort_builtin_array_resize(void)
{
    const size_t cur_len = woort_vec_len(0);
    const size_t new_len = (size_t)woort_int(1);

    woort_vec_resize(0, new_len);

    return new_len > cur_len ? new_len - cur_len : 0;
}

static woort_api woort_builtin_array_resize_i(void)
{
    const size_t fill_count = _woort_builtin_array_resize();

    if (fill_count > 0)
    {
        const size_t cur_len = woort_vec_len(0) - fill_count;

        woort_StackValue box;
        if (!woort_push_reserve(1, &box))
            return woort_ret_panic("Stack overflow.");

        woort_set_box_int(box, woort_int(2));

        for (size_t i = cur_len; i < cur_len + fill_count; i++)
            (void)woort_vec_set(0, i, box);
    }

    return woort_ret_void();
}

static woort_api woort_builtin_array_resize_r(void)
{
    const size_t fill_count = _woort_builtin_array_resize();

    if (fill_count > 0)
    {
        const size_t cur_len = woort_vec_len(0) - fill_count;

        woort_StackValue box;
        if (!woort_push_reserve(1, &box))
            return woort_ret_panic("Stack overflow.");

        woort_set_box_real(box, woort_real(2));

        for (size_t i = cur_len; i < cur_len + fill_count; i++)
            (void)woort_vec_set(0, i, box);
    }

    return woort_ret_void();
}

static woort_api woort_builtin_array_resize_b(void)
{
    const size_t fill_count = _woort_builtin_array_resize();

    if (fill_count > 0)
    {
        const size_t cur_len = woort_vec_len(0) - fill_count;

        woort_StackValue box;
        if (!woort_push_reserve(1, &box))
            return woort_ret_panic("Stack overflow.");

        woort_set_box_bool(box, woort_bool(2));

        for (size_t i = cur_len; i < cur_len + fill_count; i++)
            (void)woort_vec_set(0, i, box);
    }

    return woort_ret_void();
}

static woort_api woort_builtin_array_resize_x(void)
{
    const size_t fill_count = _woort_builtin_array_resize();

    if (fill_count > 0)
    {
        const size_t cur_len = woort_vec_len(0) - fill_count;

        for (size_t i = cur_len; i < cur_len + fill_count; i++)
            (void)woort_vec_set(0, i, 2);
    }

    return woort_ret_void();
}

static woort_api woort_builtin_array_shrink(void)
{
    const size_t newsz = (size_t)woort_int(1);

    return woort_ret_bool(woort_vec_shrink(0, newsz));
}

static woort_api woort_builtin_array_insert_i(void)
{
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    if (!woort_vec_insert(0, (size_t)woort_int(1), WOORT_RETURN_SLOT))
        return woort_ret_panic("Index out of range.");
    return woort_ret_void();
}

static woort_api woort_builtin_array_insert_r(void)
{
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    if (!woort_vec_insert(0, (size_t)woort_int(1), WOORT_RETURN_SLOT))
        return woort_ret_panic("Index out of range.");
    return woort_ret_void();
}

static woort_api woort_builtin_array_insert_b(void)
{
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    if (!woort_vec_insert(0, (size_t)woort_int(1), WOORT_RETURN_SLOT))
        return woort_ret_panic("Index out of range.");
    return woort_ret_void();
}

static woort_api woort_builtin_array_insert_x(void)
{
    if (!woort_vec_insert(0, (size_t)woort_int(1), 2))
        return woort_ret_panic("Index out of range.");
    return woort_ret_void();
}

static woort_api woort_builtin_array_swap(void)
{
    woort_vec_swap(0, 1);
    return woort_ret_void();
}

static woort_api woort_builtin_array_copy(void)
{
    woort_vec_copy(0, 1);
    return woort_ret_void();
}

static woort_api woort_builtin_array_add_i(void)
{
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(1));
    woort_vec_push(0, WOORT_RETURN_SLOT);

    return woort_ret_void();
}

static woort_api woort_builtin_array_add_r(void)
{
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(1));
    woort_vec_push(0, WOORT_RETURN_SLOT);

    return woort_ret_void();
}

static woort_api woort_builtin_array_add_b(void)
{
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(1));
    woort_vec_push(0, WOORT_RETURN_SLOT);

    return woort_ret_void();
}

static woort_api woort_builtin_array_add_x(void)
{
    woort_vec_push(0, 1);
    return woort_ret_void();
}

static woort_api woort_builtin_array_pop_u(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_option_none();

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    (void)woort_vec_pop(0);
    (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);

    return woort_ret_option_value(WOORT_RETURN_SLOT);
}

static woort_api woort_builtin_array_pop_r(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_option_none();

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    (void)woort_vec_pop(0);

    return woort_ret_option_value(WOORT_RETURN_SLOT);
}

static woort_api woort_builtin_array_dequeue_u(void)
{
    if (woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
    {
        (void)woort_vec_erase(0, 0);
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_array_dequeue_r(void)
{
    if (woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
    {
        (void)woort_vec_erase(0, 0);
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_array_pop_val_u(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_panic("Index out of range.");

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    (void)woort_vec_pop(0);
    (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);

    return woort_ret();
}

static woort_api woort_builtin_array_pop_val_r(void)
{
    size_t len = woort_vec_len(0);
    if (len == 0)
        return woort_ret_panic("Index out of range.");

    (void)woort_vec_get(WOORT_RETURN_SLOT, 0, len - 1);
    (void)woort_vec_pop(0);

    return woort_ret();
}

static woort_api woort_builtin_array_dequeue_val_u(void)
{
    if (!woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
        return woort_ret_panic("Index out of range.");

    (void)woort_vec_erase(0, 0);
    (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);

    return woort_ret();
}

static woort_api woort_builtin_array_dequeue_val_r(void)
{
    if (!woort_vec_get(WOORT_RETURN_SLOT, 0, 0))
        return woort_ret_panic("Index out of range.");

    (void)woort_vec_erase(0, 0);

    return woort_ret();
}

static woort_api woort_builtin_array_remove(void)
{
    size_t idx = (size_t)woort_int(1);
    size_t len = woort_vec_len(0);

    return woort_ret_bool(woort_vec_erase(0, idx));
}

static woort_api woort_builtin_array_clear(void)
{
    woort_vec_clear(0);
    return woort_ret_void();
}

/* ================================================================
 * Map / Dict operations
 * ================================================================ */

static woort_api woort_builtin_serialize_map(void)
{
    char* const result = woort_serialize_map(0, WOORT_SERIALIZE_FLAG_STRICT);
    if (result != NULL)
    {
        const woort_api v = woort_ret_option_string(result);
        free(result);
        return v;
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_deserialize_map(void)
{
    if (woort_deserialize_map(WOORT_RETURN_SLOT, woort_string(0)))
        return woort_ret_option_value(WOORT_RETURN_SLOT);

    return woort_ret_option_none();
}

static woort_api woort_builtin_map_create(void)
{
    woort_set_map(WOORT_RETURN_SLOT);
    woort_map_reserve(WOORT_RETURN_SLOT, (size_t)woort_int(0));

    return woort_ret();
}

static woort_api woort_builtin_map_reserve(void)
{
    woort_map_reserve(0, (size_t)woort_int(1));
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_ii(void)
{
    woort_Int key = woort_int(1);
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_ir(void)
{
    woort_Int key = woort_int(1);
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_ib(void)
{
    woort_Int key = woort_int(1);
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_ix(void)
{
    woort_Int key = woort_int(1);
    (void)woort_map_set_by_int(0, key, 2);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_ri(void)
{
    woort_Real key = woort_real(1);
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_rr(void)
{
    woort_Real key = woort_real(1);
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_rb(void)
{
    woort_Real key = woort_real(1);
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_rx(void)
{
    woort_Real key = woort_real(1);
    (void)woort_map_set_by_real(0, key, 2);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_bi(void)
{
    bool key = woort_bool(1);
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_br(void)
{
    bool key = woort_bool(1);
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_bb(void)
{
    bool key = woort_bool(1);
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_bx(void)
{
    bool key = woort_bool(1);
    (void)woort_map_set_by_bool(0, key, 2);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_xi(void)
{
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_xr(void)
{
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_xb(void)
{
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_void();
}

static woort_api woort_builtin_map_set_xx(void)
{
    (void)woort_map_set(0, 1, 2);
    return woort_ret_void();
}

static woort_api woort_builtin_map_len(void)
{
    size_t len = woort_map_len(0);
    return woort_ret_int((woort_Int)len);
}

static woort_api woort_builtin_map_only_get_iu(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_only_get_ir(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_only_get_ru(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_only_get_rr(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_only_get_bu(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_only_get_br(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_only_get_xu(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_only_get_xr(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
        return woort_ret_option_value(WOORT_RETURN_SLOT);
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_find_i(void)
{
    woort_Int key = woort_int(1);
    bool found = woort_map_contains_int(0, key);
    return woort_ret_bool(found);
}

static woort_api woort_builtin_map_find_r(void)
{
    woort_Real key = woort_real(1);
    bool found = woort_map_contains_real(0, key);
    return woort_ret_bool(found);
}

static woort_api woort_builtin_map_find_b(void)
{
    bool key = woort_bool(1);
    bool found = woort_map_contains_bool(0, key);
    return woort_ret_bool(found);
}

static woort_api woort_builtin_map_find_x(void)
{
    bool found = woort_map_contains(0, 1);
    return woort_ret_bool(found);
}

static woort_api woort_builtin_map_get_or_default_iu(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_default_ir(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_default_ru(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_default_rr(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_default_bu(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_default_br(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_default_xu(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_default_xr(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
        return woort_ret();
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_ii(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_ir(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_ib(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_ix(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    (void)woort_map_set_by_int(0, key, 2);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_ri(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_rr(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_rb(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_rx(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    (void)woort_map_set_by_real(0, key, 2);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_bi(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_br(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_bb(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_bx(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    (void)woort_map_set_by_bool(0, key, 2);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_xi(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_int(WOORT_RETURN_SLOT, woort_int(2));
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_xr(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_real(WOORT_RETURN_SLOT, woort_real(2));
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_xb(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    woort_set_box_bool(WOORT_RETURN_SLOT, woort_bool(2));
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_xx(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
        return woort_ret();
    (void)woort_map_set(0, 1, 2);
    return woort_ret_value(2);
}

static woort_api woort_builtin_map_get_or_set_default_do_ii(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Int val = woort_int(WOORT_RETURN_SLOT);
    woort_set_box_int(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_int(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_ir(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Real val = woort_real(WOORT_RETURN_SLOT);
    woort_set_box_real(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_real(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_ib(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    bool val = woort_bool(WOORT_RETURN_SLOT);
    woort_set_box_bool(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret_bool(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_ix(void)
{
    woort_Int key = woort_int(1);
    if (woort_map_get_by_int(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    (void)woort_map_set_by_int(0, key, WOORT_RETURN_SLOT);
    return woort_ret();
}

static woort_api woort_builtin_map_get_or_set_default_do_ri(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Int val = woort_int(WOORT_RETURN_SLOT);
    woort_set_box_int(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_int(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_rr(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Real val = woort_real(WOORT_RETURN_SLOT);
    woort_set_box_real(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_real(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_rb(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    bool val = woort_bool(WOORT_RETURN_SLOT);
    woort_set_box_bool(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret_bool(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_rx(void)
{
    woort_Real key = woort_real(1);
    if (woort_map_get_by_real(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    (void)woort_map_set_by_real(0, key, WOORT_RETURN_SLOT);
    return woort_ret();
}

static woort_api woort_builtin_map_get_or_set_default_do_bi(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Int val = woort_int(WOORT_RETURN_SLOT);
    woort_set_box_int(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_int(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_br(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Real val = woort_real(WOORT_RETURN_SLOT);
    woort_set_box_real(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_real(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_bb(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    bool val = woort_bool(WOORT_RETURN_SLOT);
    woort_set_box_bool(WOORT_RETURN_SLOT, val);
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret_bool(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_bx(void)
{
    bool key = woort_bool(1);
    if (woort_map_get_by_bool(WOORT_RETURN_SLOT, 0, key))
        return woort_ret();
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    (void)woort_map_set_by_bool(0, key, WOORT_RETURN_SLOT);
    return woort_ret();
}

static woort_api woort_builtin_map_get_or_set_default_do_xi(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Int val = woort_int(WOORT_RETURN_SLOT);
    woort_set_box_int(WOORT_RETURN_SLOT, val);
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_int(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_xr(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    woort_Real val = woort_real(WOORT_RETURN_SLOT);
    woort_set_box_real(WOORT_RETURN_SLOT, val);
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_real(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_xb(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
    {
        (void)woort_unbox(WOORT_RETURN_SLOT, WOORT_RETURN_SLOT);
        return woort_ret();
    }
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    bool val = woort_bool(WOORT_RETURN_SLOT);
    woort_set_box_bool(WOORT_RETURN_SLOT, val);
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret_bool(val);
}

static woort_api woort_builtin_map_get_or_set_default_do_xx(void)
{
    if (woort_map_get(WOORT_RETURN_SLOT, 0, 1))
        return woort_ret();
    if (woort_invoke(WOORT_RETURN_SLOT, 2) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke callback function.");
    (void)woort_map_set(0, 1, WOORT_RETURN_SLOT);
    return woort_ret();
}

static woort_api woort_builtin_map_swap(void)
{
    woort_map_swap(0, 1);
    return woort_ret_void();
}

static woort_api woort_builtin_map_copy(void)
{
    woort_map_copy(0, 1);
    return woort_ret_void();
}

static woort_api woort_builtin_map_keys(void)
{
    woort_StackValue key_slot;
    if (!woort_push_reserve(1, &key_slot))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);

    for (size_t idx = 0; ; idx++)
    {
        if (!woort_map_iter(0, idx, key_slot, WOORT_IGNORE))
            break;

        woort_vec_push(WOORT_RETURN_SLOT, key_slot);
    }

    return woort_ret();
}

static woort_api woort_builtin_map_vals(void)
{
    woort_StackValue val_slot;
    if (!woort_push_reserve(1, &val_slot))
        return woort_ret_panic("Stack overflow.");

    woort_set_vec(WOORT_RETURN_SLOT);

    for (size_t idx = 0; ; idx++)
    {
        if (!woort_map_iter(0, idx, WOORT_IGNORE, val_slot))
            break;

        woort_vec_push(WOORT_RETURN_SLOT, val_slot);
    }

    return woort_ret();
}

static woort_api woort_builtin_map_empty(void)
{
    size_t len = woort_map_len(0);
    return woort_ret_bool(len == 0);
}

static woort_api woort_builtin_map_remove_i(void)
{
    bool removed = woort_map_erase_by_int(0, woort_int(1));
    return woort_ret_bool(removed);
}
static woort_api woort_builtin_map_remove_r(void)
{
    bool removed = woort_map_erase_by_real(0, woort_real(1));
    return woort_ret_bool(removed);
}
static woort_api woort_builtin_map_remove_b(void)
{
    bool removed = woort_map_erase_by_bool(0, woort_bool(1));
    return woort_ret_bool(removed);
}
static woort_api woort_builtin_map_remove_x(void)
{
    bool removed = woort_map_erase(0, 1);
    return woort_ret_bool(removed);
}

static woort_api woort_builtin_map_clear(void)
{
    woort_map_clear(0);
    return woort_ret_void();
}

typedef struct map_iter_t
{
    const woort_GCMap* m_map;
    size_t m_index;
} map_iter_t;

static void map_iter_destroy(void* p)
{
    free(p);
}

static woort_api woort_builtin_map_iter(void)
{
    map_iter_t* const iter = malloc(sizeof(map_iter_t));
    if (iter == NULL)
        return woort_ret_panic("Out of memory.");

    iter->m_map = woort_internal_value(0)->m_map;
    iter->m_index = 0;

    return woort_set_gchandle(-1, iter, 0, map_iter_destroy, NULL),
        WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api woort_builtin_map_iter_next_uu(void)
{
    void* ptr = woort_gcpointer(0);
    map_iter_t* iter = (map_iter_t*)ptr;

    woort_DynBox key, val;

    if (woort_GCMap_get_key_value_by_index(iter->m_map, iter->m_index, &key, &val))
    {
        ++iter->m_index;

        woort_set_struct(WOORT_RETURN_SLOT, 2);

        woort_GCStruct* result = woort_internal_value(WOORT_RETURN_SLOT)->m_struct;

        woort_Value temp;

        woort_DynBox_unbox_no_check(key, &temp);
        woort_GC_init_write_barrier_value(&result->m_datas[0], temp);

        woort_DynBox_unbox_no_check(val, &temp);
        woort_GC_init_write_barrier_value(&result->m_datas[1], temp);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_iter_next_ur(void)
{
    void* ptr = woort_gcpointer(0);
    map_iter_t* iter = (map_iter_t*)ptr;

    woort_DynBox key, val;

    if (woort_GCMap_get_key_value_by_index(iter->m_map, iter->m_index, &key, &val))
    {
        ++iter->m_index;

        woort_set_struct(WOORT_RETURN_SLOT, 2);

        woort_GCStruct* result = woort_internal_value(WOORT_RETURN_SLOT)->m_struct;

        woort_Value temp;

        woort_DynBox_unbox_no_check(key, &temp);
        woort_GC_init_write_barrier_value(&result->m_datas[0], temp);

        woort_GC_init_write_barrier_dynbox(&result->m_datas[1].m_dynamic, val);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_iter_next_ru(void)
{
    void* ptr = woort_gcpointer(0);
    map_iter_t* iter = (map_iter_t*)ptr;

    woort_DynBox key, val;

    if (woort_GCMap_get_key_value_by_index(iter->m_map, iter->m_index, &key, &val))
    {
        ++iter->m_index;

        woort_set_struct(WOORT_RETURN_SLOT, 2);

        woort_GCStruct* result = woort_internal_value(WOORT_RETURN_SLOT)->m_struct;

        woort_Value temp;

        woort_GC_init_write_barrier_dynbox(&result->m_datas[0].m_dynamic, key);

        woort_DynBox_unbox_no_check(val, &temp);
        woort_GC_init_write_barrier_value(&result->m_datas[1], temp);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

static woort_api woort_builtin_map_iter_next_rr(void)
{
    void* ptr = woort_gcpointer(0);
    map_iter_t* iter = (map_iter_t*)ptr;

    woort_DynBox key, val;

    if (woort_GCMap_get_key_value_by_index(iter->m_map, iter->m_index, &key, &val))
    {
        ++iter->m_index;

        woort_set_struct(WOORT_RETURN_SLOT, 2);

        woort_GCStruct* result = woort_internal_value(WOORT_RETURN_SLOT)->m_struct;

        woort_GC_init_write_barrier_dynbox(&result->m_datas[0].m_dynamic, key);
        woort_GC_init_write_barrier_dynbox(&result->m_datas[1].m_dynamic, val);

        return woort_ret_option_value(WOORT_RETURN_SLOT);
    }
    return woort_ret_option_none();
}

/* ================================================================
 * Integer / bitwise operations
 * ================================================================ */

static woort_api woort_builtin_int_to_hex(void)
{
    char result[18];
    woort_Int val = woort_int(0);

    int written;
    if (val >= 0)
        written = snprintf(result, sizeof(result), "%llX",
            (unsigned long long)(woort_Handle)val);
    else
        written = snprintf(result, sizeof(result), "-%llX",
            (unsigned long long)(woort_Handle)(-val));

    (void)written;
    return woort_ret_string(result);
}

static woort_api woort_builtin_int_to_oct(void)
{
    char result[24];
    woort_Int val = woort_int(0);

    int written;
    if (val >= 0)
        written = snprintf(result, sizeof(result), "%llo",
            (unsigned long long)(woort_Handle)val);
    else
        written = snprintf(result, sizeof(result), "-%llo",
            (unsigned long long)(woort_Handle)(-val));

    (void)written;
    return woort_ret_string(result);
}

static woort_api woort_builtin_bit_or(void)
{
    woort_Int result = woort_int(0) | woort_int(1);
    return woort_ret_int(result);
}

static woort_api woort_builtin_bit_and(void)
{
    woort_Int result = woort_int(0) & woort_int(1);
    return woort_ret_int(result);
}

static woort_api woort_builtin_bit_xor(void)
{
    woort_Int result = woort_int(0) ^ woort_int(1);
    return woort_ret_int(result);
}

static woort_api woort_builtin_bit_not(void)
{
    woort_Int result = ~woort_int(0);
    return woort_ret_int(result);
}

static woort_api woort_builtin_bit_shl(void)
{
    woort_Int result = woort_int(0) << woort_int(1);
    return woort_ret_int(result);
}

static woort_api woort_builtin_bit_shr(void)
{
    woort_Handle result =
        (woort_Handle)woort_int(0) >> (woort_Handle)woort_int(1);
    return woort_ret_int((woort_Int)result);
}

static woort_api woort_builtin_bit_ashr(void)
{
    woort_Int result = woort_int(0) >> woort_int(1);
    return woort_ret_int(result);
}

/* ================================================================
 * GCHandle close
 * ================================================================ */

static woort_api woort_builtin_gchandle_close(void)
{
    woort_GCHandle* handle =
        (woort_GCHandle*)woort_internal_value(0)->m_gcinstance;

    return woort_ret_bool(woort_GCHandle_close(handle));
}

/* ================================================================
 * Tuple operations
 * ================================================================ */

static woort_api woort_builtin_tuple_nthcdr(void)
{
    woort_StackValue elem;
    if (!woort_push_reserve(1, &elem))
        return woort_ret_panic("Stack overflow.");

    size_t len = woort_struct_len(0);
    size_t idx = (size_t)woort_int(1);

    if (idx > len)
        idx = len;

    woort_set_struct(WOORT_RETURN_SLOT, (uint16_t)(len - idx));
    for (size_t i = idx; i < len; i++)
    {
        woort_struct_get(elem, 0, (uint16_t)i);
        woort_struct_set(WOORT_RETURN_SLOT, (uint16_t)(i - idx), elem);
    }

    return woort_ret();
}

static woort_api woort_builtin_tuple_cdr(void)
{
    woort_StackValue elem;
    if (!woort_push_reserve(1, &elem))
        return woort_ret_panic("Stack overflow.");

    size_t len = woort_struct_len(0);
    if (len == 0)
        return woort_ret_panic("Cannot take cdr of empty tuple.");

    woort_set_struct(WOORT_RETURN_SLOT, (uint16_t)(len - 1));
    for (size_t i = 1; i < len; i++)
    {
        woort_struct_get(elem, 0, (uint16_t)i);
        woort_struct_set(WOORT_RETURN_SLOT, (uint16_t)(i - 1), elem);
    }

    return woort_ret();
}

static woort_api woort_builtin_debug_trace_callstack(void)
{
    const woort_Int layer_count = woort_int(0);
    woort_vm* this_vm = woort_VMRuntime_current();

    woort_StackValue temp_callstack;
    if (!woort_push_reserve(7, &temp_callstack))
    {
        return woort_ret_panic("Stack overflow.");
    }
    woort_StackValue temp_val = temp_callstack + 1;
    woort_StackValue temp_loc = temp_callstack + 2;
    woort_StackValue temp_inner1 = temp_callstack + 3;
    woort_StackValue temp_inner2 = temp_callstack + 4;
    woort_StackValue temp_outer = temp_callstack + 5;
    woort_StackValue temp_result = temp_callstack + 6;

    woort_set_vec(temp_result);

    woort_VMRuntime_TraceCallstack_Iter iter;
    woort_VMRuntime_TraceCallstack trace;
    woort_VMRuntime_trace_begin(this_vm, &iter);

    size_t depth = 0;
    while (woort_VMRuntime_trace_next(&iter, &trace))
    {
        if (layer_count > 0 && (woort_Int)depth >= layer_count)
            break;

        /* Build callstack struct with 3 fields */
        woort_set_struct(temp_callstack, 3);

        /* field 0: function_name */
        {
            const char* name = trace.m_function_name != NULL
                ? trace.m_function_name : "<unknown>";
            woort_set_string(temp_val, name);
            woort_struct_set(temp_callstack, 0, temp_val);
        }

        /* field 1: file_name */
        {
            const char* file = trace.m_file_or_lib_name != NULL
                ? trace.m_file_or_lib_name : "<unknown>";
            woort_set_string(temp_val, file);
            woort_struct_set(temp_callstack, 1, temp_val);
        }

        /* field 2: location (option<((int,int),(int,int))>) */
        if (trace.m_has_location)
        {
            /* inner tuple 1: (begin_line, begin_col) */
            woort_set_struct(temp_inner1, 2);
            woort_set_int(temp_val, (woort_Int)trace.m_location_begin[0]);
            woort_struct_set(temp_inner1, 0, temp_val);
            woort_set_int(temp_val, (woort_Int)trace.m_location_begin[1]);
            woort_struct_set(temp_inner1, 1, temp_val);

            /* inner tuple 2: (end_line, end_col) */
            woort_set_struct(temp_inner2, 2);
            woort_set_int(temp_val, (woort_Int)trace.m_location_end[0]);
            woort_struct_set(temp_inner2, 0, temp_val);
            woort_set_int(temp_val, (woort_Int)trace.m_location_end[1]);
            woort_struct_set(temp_inner2, 1, temp_val);

            /* outer tuple: (inner1, inner2) */
            woort_set_struct(temp_outer, 2);
            woort_struct_set(temp_outer, 0, temp_inner1);
            woort_struct_set(temp_outer, 1, temp_inner2);

            /* wrap in option::Some */
            woort_set_option_value(temp_loc, temp_outer);
        }
        else
        {
            woort_set_option_none(temp_loc);
        }

        woort_struct_set(temp_callstack, 2, temp_loc);

        /* push finished struct onto result vec */
        woort_vec_push(temp_result, temp_callstack);

        ++depth;
    }
    return woort_ret_value(temp_result);
}

static woort_api woort_builtin_debug_runtime_version(void)
{
#define WOORT_VERSION_WRAP(A, B, C, D) {A, B, C, D}
    const woort_Int version[] = WOORT_VERSION;
#undef WOORT_VERSION_WRAP

    woort_set_struct(WOORT_RETURN_SLOT, 4);

    woort_struct_set_int(WOORT_RETURN_SLOT, 0, version[0]);
    woort_struct_set_int(WOORT_RETURN_SLOT, 1, version[1]);
    woort_struct_set_int(WOORT_RETURN_SLOT, 2, version[2]);
    woort_struct_set_int(WOORT_RETURN_SLOT, 3, version[3]);

    return woort_ret();
}

static woort_api woort_builtin_debug_print(void)
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
            _woort_WAIPO_print_value(
                woort_internal_value((woort_StackValue)i)->m_dynamic, false);
        }
    }
    return woort_ret_void();
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
    WOORT_BUILTIN_FUNC(clock),

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

    WOORT_BUILTIN_FUNC(array_create_i),
    WOORT_BUILTIN_FUNC(array_create_r),
    WOORT_BUILTIN_FUNC(array_create_b),
    WOORT_BUILTIN_FUNC(array_create_x),
    WOORT_BUILTIN_FUNC(serialize_array),
    WOORT_BUILTIN_FUNC(deserialize_array),
    WOORT_BUILTIN_FUNC(create_str_by_wchar),
    WOORT_BUILTIN_FUNC(create_str_by_ascii),
    WOORT_BUILTIN_FUNC(array_len),
    WOORT_BUILTIN_FUNC(array_empty),
    WOORT_BUILTIN_FUNC(array_get_u),
    WOORT_BUILTIN_FUNC(array_get_r),
    WOORT_BUILTIN_FUNC(array_get_or_default_u),
    WOORT_BUILTIN_FUNC(array_get_or_default_r),
    WOORT_BUILTIN_FUNC(array_find_x),
    WOORT_BUILTIN_FUNC(array_find_i),
    WOORT_BUILTIN_FUNC(array_find_r),
    WOORT_BUILTIN_FUNC(array_find_b),
    WOORT_BUILTIN_FUNC(array_iter),
    WOORT_BUILTIN_FUNC(array_iter_next_u),
    WOORT_BUILTIN_FUNC(array_iter_next_r),
    WOORT_BUILTIN_FUNC(array_connect),
    WOORT_BUILTIN_FUNC(array_sub),
    WOORT_BUILTIN_FUNC(array_sub_to),
    WOORT_BUILTIN_FUNC(array_sub_range),
    WOORT_BUILTIN_FUNC(array_front_u),
    WOORT_BUILTIN_FUNC(array_front_r),
    WOORT_BUILTIN_FUNC(array_back_u),
    WOORT_BUILTIN_FUNC(array_back_r),
    WOORT_BUILTIN_FUNC(array_front_val_u),
    WOORT_BUILTIN_FUNC(array_front_val_r),
    WOORT_BUILTIN_FUNC(array_back_val_u),
    WOORT_BUILTIN_FUNC(array_back_val_r),
    WOORT_BUILTIN_FUNC(array_resize_i),
    WOORT_BUILTIN_FUNC(array_resize_r),
    WOORT_BUILTIN_FUNC(array_resize_b),
    WOORT_BUILTIN_FUNC(array_resize_x),
    WOORT_BUILTIN_FUNC(array_shrink),
    WOORT_BUILTIN_FUNC(array_insert_i),
    WOORT_BUILTIN_FUNC(array_insert_r),
    WOORT_BUILTIN_FUNC(array_insert_b),
    WOORT_BUILTIN_FUNC(array_insert_x),
    WOORT_BUILTIN_FUNC(array_swap),
    WOORT_BUILTIN_FUNC(array_copy),
    WOORT_BUILTIN_FUNC(array_add_i),
    WOORT_BUILTIN_FUNC(array_add_r),
    WOORT_BUILTIN_FUNC(array_add_b),
    WOORT_BUILTIN_FUNC(array_add_x),
    WOORT_BUILTIN_FUNC(array_pop_u),
    WOORT_BUILTIN_FUNC(array_pop_r),
    WOORT_BUILTIN_FUNC(array_dequeue_u),
    WOORT_BUILTIN_FUNC(array_dequeue_r),
    WOORT_BUILTIN_FUNC(array_pop_val_u),
    WOORT_BUILTIN_FUNC(array_pop_val_r),
    WOORT_BUILTIN_FUNC(array_dequeue_val_u),
    WOORT_BUILTIN_FUNC(array_dequeue_val_r),
    WOORT_BUILTIN_FUNC(array_remove),
    WOORT_BUILTIN_FUNC(array_clear),

    WOORT_BUILTIN_FUNC(serialize_map),
    WOORT_BUILTIN_FUNC(deserialize_map),
    WOORT_BUILTIN_FUNC(map_create),
    WOORT_BUILTIN_FUNC(map_reserve),
    WOORT_BUILTIN_FUNC(map_set_ii),
    WOORT_BUILTIN_FUNC(map_set_ir),
    WOORT_BUILTIN_FUNC(map_set_ib),
    WOORT_BUILTIN_FUNC(map_set_ix),
    WOORT_BUILTIN_FUNC(map_set_ri),
    WOORT_BUILTIN_FUNC(map_set_rr),
    WOORT_BUILTIN_FUNC(map_set_rb),
    WOORT_BUILTIN_FUNC(map_set_rx),
    WOORT_BUILTIN_FUNC(map_set_bi),
    WOORT_BUILTIN_FUNC(map_set_br),
    WOORT_BUILTIN_FUNC(map_set_bb),
    WOORT_BUILTIN_FUNC(map_set_bx),
    WOORT_BUILTIN_FUNC(map_set_xi),
    WOORT_BUILTIN_FUNC(map_set_xr),
    WOORT_BUILTIN_FUNC(map_set_xb),
    WOORT_BUILTIN_FUNC(map_set_xx),
    WOORT_BUILTIN_FUNC(map_len),
    WOORT_BUILTIN_FUNC(map_only_get_iu),
    WOORT_BUILTIN_FUNC(map_only_get_ir),
    WOORT_BUILTIN_FUNC(map_only_get_ru),
    WOORT_BUILTIN_FUNC(map_only_get_rr),
    WOORT_BUILTIN_FUNC(map_only_get_bu),
    WOORT_BUILTIN_FUNC(map_only_get_br),
    WOORT_BUILTIN_FUNC(map_only_get_xu),
    WOORT_BUILTIN_FUNC(map_only_get_xr),
    WOORT_BUILTIN_FUNC(map_find_i),
    WOORT_BUILTIN_FUNC(map_find_r),
    WOORT_BUILTIN_FUNC(map_find_b),
    WOORT_BUILTIN_FUNC(map_find_x),
    WOORT_BUILTIN_FUNC(map_get_or_default_iu),
    WOORT_BUILTIN_FUNC(map_get_or_default_ir),
    WOORT_BUILTIN_FUNC(map_get_or_default_ru),
    WOORT_BUILTIN_FUNC(map_get_or_default_rr),
    WOORT_BUILTIN_FUNC(map_get_or_default_bu),
    WOORT_BUILTIN_FUNC(map_get_or_default_br),
    WOORT_BUILTIN_FUNC(map_get_or_default_xu),
    WOORT_BUILTIN_FUNC(map_get_or_default_xr),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_ii),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_ir),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_ib),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_ix),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_ri),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_rr),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_rb),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_rx),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_bi),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_br),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_bb),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_bx),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_xi),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_xr),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_xb),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_xx),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_ii),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_ir),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_ib),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_ix),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_ri),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_rr),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_rb),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_rx),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_bi),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_br),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_bb),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_bx),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_xi),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_xr),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_xb),
    WOORT_BUILTIN_FUNC(map_get_or_set_default_do_xx),
    WOORT_BUILTIN_FUNC(map_swap),
    WOORT_BUILTIN_FUNC(map_copy),
    WOORT_BUILTIN_FUNC(map_keys),
    WOORT_BUILTIN_FUNC(map_vals),
    WOORT_BUILTIN_FUNC(map_empty),
    WOORT_BUILTIN_FUNC(map_remove_i),
    WOORT_BUILTIN_FUNC(map_remove_r),
    WOORT_BUILTIN_FUNC(map_remove_b),
    WOORT_BUILTIN_FUNC(map_remove_x),
    WOORT_BUILTIN_FUNC(map_clear),
    WOORT_BUILTIN_FUNC(map_iter),
    WOORT_BUILTIN_FUNC(map_iter_next_uu),
    WOORT_BUILTIN_FUNC(map_iter_next_ur),
    WOORT_BUILTIN_FUNC(map_iter_next_ru),
    WOORT_BUILTIN_FUNC(map_iter_next_rr),

    WOORT_BUILTIN_FUNC(int_to_hex),
    WOORT_BUILTIN_FUNC(int_to_oct),
    WOORT_BUILTIN_FUNC(bit_or),
    WOORT_BUILTIN_FUNC(bit_and),
    WOORT_BUILTIN_FUNC(bit_xor),
    WOORT_BUILTIN_FUNC(bit_not),
    WOORT_BUILTIN_FUNC(bit_shl),
    WOORT_BUILTIN_FUNC(bit_shr),
    WOORT_BUILTIN_FUNC(bit_ashr),

    WOORT_BUILTIN_FUNC(gchandle_close),

    WOORT_BUILTIN_FUNC(tuple_nthcdr),
    WOORT_BUILTIN_FUNC(tuple_cdr),

    WOORT_BUILTIN_FUNC(debug_trace_callstack),
    WOORT_BUILTIN_FUNC(debug_runtime_version),
    WOORT_BUILTIN_FUNC(debug_print),

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
