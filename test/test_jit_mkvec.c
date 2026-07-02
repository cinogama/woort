#include "woort.h"

#include <stdio.h>
#include <stdint.h>

static int run_mkvec_case(uint32_t count)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_main;

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_seed = woort_IRCompiler_add_constant(irc);

    woort_IRValue** cvals = (woort_IRValue**)woort_IRCompiler_alloc(irc, sizeof(woort_IRValue*) * (count ? count : 1));

    for (uint32_t i = 0; i < count; ++i)
    {
        woort_IRConstantIndex ci = woort_IRCompiler_add_constant(irc);
        cvals[i] = woort_IRFunction_fetch_const(f_main_dummy(), ci);
    }

    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            (void)woort_IR_PUSHCHK(f_main, cvals[i]);
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
    {
        woort_CodeEnv_set_const_int(cenv, woort_IRFunction_const_index_of(f_main, cvals[i]), (woort_Int)(1000 + i));
    }
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    const int ok = (st == WOORT_VM_CALL_STATUS_NORMAL);
    printf("mkvec count=%u: status=%d\n", (unsigned)count, (int)st);

    if (ok)
    {
        const size_t len = woort_vec_len(sv + 1);
        printf("  vec_len=%llu\n", (unsigned long long)len);
        if (len != count) { printf("FAIL: expected len %u got %llu\n", (unsigned)count, (unsigned long long)len); ok = 0; }

        for (uint32_t i = 0; i < count && ok; ++i)
        {
            woort_StackValue elem;
            (void)woort_push_reserve(1, &elem);
            const bool got = woort_vec_get(elem, sv + 1, i);
            if (!got) { printf("FAIL: vec_get(%u) out of range\n", (unsigned)i); ok = 0; }
            else
            {
                const woort_Int val = woort_int(elem);
                printf("  vec[%u]=%lld\n", (unsigned)i, (long long)val);
                if (val != (woort_Int)(1000 + i)) { printf("FAIL: vec[%u] expected %d got %lld\n", (unsigned)i, 1000 + (int)i, (long long)val); ok = 0; }
            }
            woort_pop(1);
        }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return ok ? 0 : 1;
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
