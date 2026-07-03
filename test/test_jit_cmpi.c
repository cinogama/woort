#include "woort.h"

#include <stdio.h>
#include <stdint.h>

typedef bool (*cmpi_builder)(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

static int run_cmpi(cmpi_builder build, woort_Int a, woort_Int b, woort_Int expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f_main);
        (void)build(f_main, dst,
            woort_IRFunction_fetch_const(f_main, c_a),
            woort_IRFunction_fetch_const(f_main, c_b));
        (void)woort_IR_ret(f_main, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_a, a);
    woort_CodeEnv_set_const_int(cenv, c_b, b);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL cmpi(%lld,%lld): status=%d\n", (long long)a, (long long)b, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect) { printf("FAIL cmpi(%lld,%lld): expected %lld got %lld\n", (long long)a, (long long)b, (long long)expect, (long long)got); ++failures; }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;

    failures += run_cmpi(woort_IR_LTI, 1, 2, 1);
    failures += run_cmpi(woort_IR_LTI, 2, 1, 0);
    failures += run_cmpi(woort_IR_LTI, 5, 5, 0);
    failures += run_cmpi(woort_IR_LTI, -5, -3, 1);
    failures += run_cmpi(woort_IR_LTI, -(1LL << 40), (1LL << 40), 1);

    failures += run_cmpi(woort_IR_GTI, 2, 1, 1);
    failures += run_cmpi(woort_IR_GTI, 1, 2, 0);
    failures += run_cmpi(woort_IR_GTI, 5, 5, 0);
    failures += run_cmpi(woort_IR_GTI, -3, -5, 1);
    failures += run_cmpi(woort_IR_GTI, (1LL << 40), -(1LL << 40), 1);

    failures += run_cmpi(woort_IR_LEI, 1, 2, 1);
    failures += run_cmpi(woort_IR_LEI, 2, 1, 0);
    failures += run_cmpi(woort_IR_LEI, 5, 5, 1);
    failures += run_cmpi(woort_IR_LEI, -7, -7, 1);
    failures += run_cmpi(woort_IR_LEI, -(1LL << 50), 0, 1);

    failures += run_cmpi(woort_IR_GEI, 2, 1, 1);
    failures += run_cmpi(woort_IR_GEI, 1, 2, 0);
    failures += run_cmpi(woort_IR_GEI, 5, 5, 1);
    failures += run_cmpi(woort_IR_GEI, -7, -7, 1);
    failures += run_cmpi(woort_IR_GEI, 0, -(1LL << 50), 1);

    failures += run_cmpi(woort_IR_EQI, 5, 5, 1);
    failures += run_cmpi(woort_IR_EQI, 5, 6, 0);
    failures += run_cmpi(woort_IR_EQI, -5, -5, 1);
    failures += run_cmpi(woort_IR_EQI, -(1LL << 60), -(1LL << 60), 1);
    failures += run_cmpi(woort_IR_EQI, 0, 1, 0);

    failures += run_cmpi(woort_IR_NEI, 5, 6, 1);
    failures += run_cmpi(woort_IR_NEI, 5, 5, 0);
    failures += run_cmpi(woort_IR_NEI, -5, 5, 1);
    failures += run_cmpi(woort_IR_NEI, (1LL << 60), -(1LL << 60), 1);
    failures += run_cmpi(woort_IR_NEI, 0, 0, 0);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_cmpi: ALL PASS\n");
        return 0;
    }
    printf("test_jit_cmpi: %d FAILURES\n", failures);
    return 1;
}
