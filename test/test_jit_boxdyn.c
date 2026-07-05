#include "woort.h"
extern void woort_JIT_compile_env(woort_CodeEnv* cenv);

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int run_box_int_case(woort_Int value)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, v, WOORT_BOX_VALUE_TYPE_INT,
            woort_IRFunction_fetch_const(f_main, c_val));
        (void)woort_IR_ret(f_main, v);
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
    (void)woort_push_reserve(3, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL box_int(%lld): status=%d\n", (long long)value, (int)st); ++failures; }

    if (failures == 0)
    {
        const woort_BoxValueType t = woort_unbox(sv + 2, sv + 1);
        if (t != WOORT_BOX_VALUE_TYPE_INT) { printf("FAIL box_int(%lld): unbox type=%d\n", (long long)value, (int)t); ++failures; }
        const woort_Int got = woort_int(sv + 2);
        if (got != value) { printf("FAIL box_int(%lld): roundtrip got %lld\n", (long long)value, (long long)got); ++failures; }
        else printf("ok box_int(%lld)\n", (long long)value);
    }

    woort_pop(3);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_box_real_case(double value)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, v, WOORT_BOX_VALUE_TYPE_REAL,
            woort_IRFunction_fetch_const(f_main, c_val));
        (void)woort_IR_ret(f_main, v);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_real(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(3, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL box_real(%g): status=%d\n", value, (int)st); ++failures; }

    if (failures == 0)
    {
        const woort_BoxValueType t = woort_unbox(sv + 2, sv + 1);
        if (t != WOORT_BOX_VALUE_TYPE_REAL) { printf("FAIL box_real(%g): unbox type=%d\n", value, (int)t); ++failures; }
        const double got = woort_real(sv + 2);
        if (got != value) { printf("FAIL box_real(%g): roundtrip got %g\n", value, got); ++failures; }
        else printf("ok box_real(%g)\n", value);
    }

    woort_pop(3);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_box_bool_case(woort_Int value)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, v, WOORT_BOX_VALUE_TYPE_BOOL,
            woort_IRFunction_fetch_const(f_main, c_val));
        (void)woort_IR_ret(f_main, v);
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
    (void)woort_push_reserve(3, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL box_bool(%lld): status=%d\n", (long long)value, (int)st); ++failures; }

    if (failures == 0)
    {
        const bool got = woort_unbox_bool(sv + 1);
        const bool expect = (value != 0);
        if (got != expect) { printf("FAIL box_bool(%lld): roundtrip got %d\n", (long long)value, (int)got); ++failures; }
        else printf("ok box_bool(%lld)\n", (long long)value);
    }

    woort_pop(3);

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

    failures += run_box_int_case(0);
    failures += run_box_int_case(42);
    failures += run_box_int_case(-7);
    failures += run_box_int_case((1LL << 61) - 1);
    failures += run_box_int_case(1LL << 61);
    failures += run_box_int_case(-(1LL << 61) - 1);

    failures += run_box_real_case(3.14);
    failures += run_box_real_case(-2.5);
    failures += run_box_real_case(0.0);
    failures += run_box_real_case(1.0);
    failures += run_box_real_case(1.7976931348623157e308);

    failures += run_box_bool_case(0);
    failures += run_box_bool_case(1);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_boxdyn: ALL PASS\n");
        return 0;
    }
    printf("test_jit_boxdyn: %d FAILURES\n", failures);
    return 1;
}
