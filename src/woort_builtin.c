#include "woort.h"

#include "woort_builtin.h"

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

/* ================================================================
 * Function table for the "woolang" fake library
 * ================================================================ */

static const woort_ExternLibFunc g_woolang_funcs[] = {
    {"woostd_return_it_self", (void*)&woort_builtin_return_it_self},
    {"woostd_bad_function",   (void*)&woort_builtin_bad_function},
    {"woostd_panic",           (void*)&woort_builtin_panic},
    {"woostd_print",           (void*)&woort_builtin_print},
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
