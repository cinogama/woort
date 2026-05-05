#include "woort.h"

#include "woort_builtin.h"

#include "woort_vector.h"
#include "woort_atomic.h"
#include "woort_threads.h"

#include "woort_gc_vec.h"
#include "woort_gc_map.h"
#include "woort_gc_struct.h"
#include "woort_gc.h"

#include <stdio.h>
#include <stdlib.h>
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
