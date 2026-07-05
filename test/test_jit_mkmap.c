#include "woort.h"
extern void woort_JIT_compile_env(woort_CodeEnv* cenv);

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static woort_IRConstantIndex* alloc_consts(woort_IRCompiler* irc, size_t count)
{
    woort_IRConstantIndex* arr =
        (woort_IRConstantIndex*)malloc(sizeof(woort_IRConstantIndex) * (count ? count : 1));
    for (size_t i = 0; i < count; ++i)
        arr[i] = woort_IRCompiler_add_constant(irc);
    return arr;
}

static int run_mkmap_case(uint32_t pairs)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex* c_keys = alloc_consts(irc, pairs);
    woort_IRConstantIndex* c_vals = alloc_consts(irc, pairs);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        for (uint32_t i = 0; i < pairs; ++i)
        {
            (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_keys[i]));
            (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_vals[i]));
        }

        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKMAP(f_main, v, pairs);
        (void)woort_IR_ret(f_main, v);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    for (uint32_t i = 0; i < pairs; ++i)
    {
        woort_CodeEnv_set_const_box_int(cenv, c_keys[i], (woort_Int)(100 + i * 10));
        woort_CodeEnv_set_const_box_int(cenv, c_vals[i], (woort_Int)(2000 + i * 100));
    }
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    printf("mkmap pairs=%u: status=%d\n", (unsigned)pairs, (int)st);
    if (st != WOORT_VM_CALL_STATUS_NORMAL)
    {
        printf("FAIL: status not NORMAL\n");
        ++failures;
    }

    if (failures == 0)
    {
        const size_t len = woort_map_len(sv + 1);
        if (len != pairs)
        {
            printf("FAIL: expected len %u got %llu\n", (unsigned)pairs, (unsigned long long)len);
            ++failures;
        }

        for (uint32_t i = 0; i < pairs && failures == 0; ++i)
        {
            woort_StackValue elem;
            (void)woort_push_reserve(1, &elem);
            const woort_Int key = (woort_Int)(100 + i * 10);
            const bool got = woort_map_get_by_int(elem, sv + 1, key);
            if (!got)
            {
                printf("FAIL: map_get_by_int(%lld) not found\n", (long long)key);
                ++failures;
            }
            else
            {
                const woort_Int val = woort_unbox_int(elem);
                if (val != (woort_Int)(2000 + i * 100))
                {
                    printf("FAIL: key %lld expected %lld got %lld\n",
                        (long long)key, (long long)(woort_Int)(2000 + i * 100), (long long)val);
                    ++failures;
                }
            }
            woort_pop(1);
        }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    free(c_keys);
    free(c_vals);
    return failures;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;
    failures += run_mkmap_case(0);
    failures += run_mkmap_case(1);
    failures += run_mkmap_case(3);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_mkmap: ALL PASS\n");
        return 0;
    }
    printf("test_jit_mkmap: %d FAILURES\n", failures);
    return 1;
}
