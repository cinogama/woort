#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int run_mkvec_case(uint32_t count)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);

    woort_IRConstantIndex* c_elems =
        (woort_IRConstantIndex*)malloc(sizeof(woort_IRConstantIndex) * (count ? count : 1));
    for (uint32_t i = 0; i < count; ++i)
        c_elems[i] = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_elems[i]));
        }

        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKVEC(f_main, v, count);
        (void)woort_IR_ret(f_main, v);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    for (uint32_t i = 0; i < count; ++i)
        woort_CodeEnv_set_const_int(cenv, c_elems[i], (woort_Int)(1000 + i));
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    printf("mkvec count=%u: status=%d\n", (unsigned)count, (int)st);
    if (st != WOORT_VM_CALL_STATUS_NORMAL)
    {
        printf("FAIL: status not NORMAL\n");
        ++failures;
    }

    if (failures == 0)
    {
        const size_t len = woort_vec_len(sv + 1);
        if (len != count)
        {
            printf("FAIL: expected len %u got %llu\n", (unsigned)count, (unsigned long long)len);
            ++failures;
        }

        for (uint32_t i = 0; i < count && failures == 0; ++i)
        {
            woort_StackValue elem;
            (void)woort_push_reserve(1, &elem);
            const bool got = woort_vec_get(elem, sv + 1, i);
            if (!got)
            {
                printf("FAIL: vec_get(%u) out of range\n", (unsigned)i);
                ++failures;
            }
            else
            {
                const woort_Int val = woort_int(elem);
                if (val != (woort_Int)(1000 + i))
                {
                    printf("FAIL: vec[%u] expected %d got %lld\n", (unsigned)i, 1000 + (int)i, (long long)val);
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

    free(c_elems);
    return failures;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;
    failures += run_mkvec_case(0);
    failures += run_mkvec_case(1);
    failures += run_mkvec_case(3);
    failures += run_mkvec_case(8);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_mkvec: ALL PASS\n");
        return 0;
    }
    printf("test_jit_mkvec: %d FAILURES\n", failures);
    return 1;
}
