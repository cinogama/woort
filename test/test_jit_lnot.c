#include "woort.h"
extern void woort_JIT_compile_env(woort_CodeEnv* cenv);

#include <stdio.h>
#include <stdint.h>

static int run_lnot(woort_Int value, woort_Int expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        /* dst 为独立 vreg，强制 IR 编译器走三地址 LNOT（非复合 CLNOT） */
        woort_IRValue* dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LNOT(f_main, dst, woort_IRFunction_fetch_const(f_main, c_val));
        (void)woort_IR_ret(f_main, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL lnot(%lld): status=%d\n", (long long)value, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect) { printf("FAIL lnot(%lld): expected %lld got %lld\n", (long long)value, (long long)expect, (long long)got); ++failures; }
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

    /* 零取反为 1 */
    failures += run_lnot(0, 1);

    /* 任意非零值取反为 0 */
    failures += run_lnot(1, 0);
    failures += run_lnot(2, 0);
    failures += run_lnot(-1, 0);
    failures += run_lnot(-7, 0);
    failures += run_lnot(42, 0);

    /* 大整数与边界（非零一律为假，取反得 0） */
    failures += run_lnot(1LL << 40, 0);
    failures += run_lnot(-(1LL << 50), 0);
    failures += run_lnot(1LL << 62, 0);
    failures += run_lnot(INT64_MIN, 0);
    failures += run_lnot(INT64_MAX, 0);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_lnot: ALL PASS\n");
        return 0;
    }
    printf("test_jit_lnot: %d FAILURES\n", failures);
    return 1;
}
