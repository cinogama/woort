#include "woort.h"

#include <stdio.h>
#include <stdint.h>

static int run_unbox_int(woort_Int value)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* boxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, boxed, WOORT_BOX_VALUE_TYPE_INT,
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* unboxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_UNBOXDYN(f_main, unboxed, WOORT_BOX_VALUE_TYPE_INT, boxed);
        (void)woort_IR_ret(f_main, unboxed);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL unbox_int(%lld): status=%d\n", (long long)value, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != value) { printf("FAIL unbox_int(%lld): got %lld\n", (long long)value, (long long)got); ++failures; }
        else printf("ok unbox_int(%lld)\n", (long long)value);
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_unbox_real(double value)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* boxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, boxed, WOORT_BOX_VALUE_TYPE_REAL,
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* unboxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_UNBOXDYN(f_main, unboxed, WOORT_BOX_VALUE_TYPE_REAL, boxed);
        (void)woort_IR_ret(f_main, unboxed);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_real(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL unbox_real(%g): status=%d\n", value, (int)st); ++failures; }
    else
    {
        const double got = woort_real(sv + 1);
        if (got != value) { printf("FAIL unbox_real(%g): got %g\n", value, got); ++failures; }
        else printf("ok unbox_real(%g)\n", value);
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_unbox_bool(woort_Int value)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* boxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, boxed, WOORT_BOX_VALUE_TYPE_BOOL,
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* unboxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_UNBOXDYN(f_main, unboxed, WOORT_BOX_VALUE_TYPE_BOOL, boxed);
        (void)woort_IR_ret(f_main, unboxed);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL unbox_bool(%lld): status=%d\n", (long long)value, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        const woort_Int expect = (value != 0) ? 1 : 0;
        if (got != expect) { printf("FAIL unbox_bool(%lld): got %lld\n", (long long)value, (long long)got); ++failures; }
        else printf("ok unbox_bool(%lld)\n", (long long)value);
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

    failures += run_unbox_int(0);
    failures += run_unbox_int(42);
    failures += run_unbox_int(-7);
    failures += run_unbox_int((1LL << 61) - 1);
    failures += run_unbox_int(1LL << 61);
    failures += run_unbox_int(-(1LL << 61) - 1);

    failures += run_unbox_real(3.14);
    failures += run_unbox_real(-2.5);
    failures += run_unbox_real(1.7976931348623157e308);

    failures += run_unbox_bool(0);
    failures += run_unbox_bool(1);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_unboxdyn: ALL PASS\n");
        return 0;
    }
    printf("test_jit_unboxdyn: %d FAILURES\n", failures);
    return 1;
}
