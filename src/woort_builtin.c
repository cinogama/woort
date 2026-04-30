#include "woort.h"

#include "woort_builtin.h"

#include "woort_vector.h"

#include <stdio.h>

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
    woort_value s;
    if (!woort_push_reserve(1, &s))
        return woort_ret_panic("Failed to reserve stack.");

    const woort_Int argn = woort_int(0);
    for (woort_Int i = 1; i <= argn; ++i)
    {
        if (i != 1)
            fputc(' ', stdout);

        if (woort_unbox_type((woort_value)i) == WOORT_BOX_VALUE_TYPE_STRING)
            fputs(woort_string((woort_value)i), stdout);
        else
        {
            if (!woort_serialize_dynbox(s, (woort_value)i, WOORT_SERIALIZE_FLAG_NONE))
                return woort_ret_panic("Out of memory.");

            fputs(woort_string(s), stdout);
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

    const woort_api r =  woort_ret_buffer(vec.m_data, vec.m_size);
    woort_vector_deinit(&vec);

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
    WOORT_EXTERN_LIB_FUNC_END,
};

/* ================================================================
 * Global handle and lifecycle
 * ================================================================ */

static /* OPTIONAL */ woort_Dylib* g_builtin_lib = NULL;

bool _woort_builtin_bootup(void)
{
    g_builtin_lib = woort_dylib_fake("woolang", g_woolang_funcs, NULL);
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
