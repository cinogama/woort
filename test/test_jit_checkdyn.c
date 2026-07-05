#include "woort.h"

#include <stdio.h>
#include <stdint.h>

static int run_check_int(woort_Int value, uint8_t check_type, woort_Int expect)
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

        woort_IRValue* out = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CHECKDYN(f_main, out, check_type, boxed);
        (void)woort_IR_ret(f_main, out);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL check int(%lld) as %u: status=%d\n", (long long)value, (unsigned)check_type, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect) { printf("FAIL check int(%lld) as %u: expected %lld got %lld\n", (long long)value, (unsigned)check_type, (long long)expect, (long long)got); ++failures; }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_check_real(double value, uint8_t check_type, woort_Int expect)
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

        woort_IRValue* out = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CHECKDYN(f_main, out, check_type, boxed);
        (void)woort_IR_ret(f_main, out);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_real(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL check real(%g) as %u: status=%d\n", value, (unsigned)check_type, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect) { printf("FAIL check real(%g) as %u: expected %lld got %lld\n", value, (unsigned)check_type, (long long)expect, (long long)got); ++failures; }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_check_bool(woort_Int value, uint8_t check_type, woort_Int expect)
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

        woort_IRValue* out = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CHECKDYN(f_main, out, check_type, boxed);
        (void)woort_IR_ret(f_main, out);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_val, value);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL check bool(%lld) as %u: status=%d\n", (long long)value, (unsigned)check_type, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect) { printf("FAIL check bool(%lld) as %u: expected %lld got %lld\n", (long long)value, (unsigned)check_type, (long long)expect, (long long)got); ++failures; }
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

    failures += run_check_int(42, WOORT_BOX_VALUE_TYPE_INT, 1);
    failures += run_check_int(42, WOORT_BOX_VALUE_TYPE_REAL, 0);
    failures += run_check_int(42, WOORT_BOX_VALUE_TYPE_BOOL, 0);
    failures += run_check_int(42, WOORT_BOX_VALUE_TYPE_NIL, 0);
    failures += run_check_int(1LL << 61, WOORT_BOX_VALUE_TYPE_INT, 1);
    failures += run_check_int(1LL << 61, WOORT_BOX_VALUE_TYPE_REAL, 0);
    failures += run_check_int(-(1LL << 61) - 1, WOORT_BOX_VALUE_TYPE_INT, 1);

    failures += run_check_real(3.14, WOORT_BOX_VALUE_TYPE_REAL, 1);
    failures += run_check_real(3.14, WOORT_BOX_VALUE_TYPE_INT, 0);
    failures += run_check_real(3.14, WOORT_BOX_VALUE_TYPE_BOOL, 0);
    failures += run_check_real(1.7976931348623157e308, WOORT_BOX_VALUE_TYPE_REAL, 1);
    failures += run_check_real(1.7976931348623157e308, WOORT_BOX_VALUE_TYPE_INT, 0);

    failures += run_check_bool(1, WOORT_BOX_VALUE_TYPE_BOOL, 1);
    failures += run_check_bool(1, WOORT_BOX_VALUE_TYPE_INT, 0);
    failures += run_check_bool(0, WOORT_BOX_VALUE_TYPE_BOOL, 1);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_checkdyn: ALL PASS\n");
        return 0;
    }
    printf("test_jit_checkdyn: %d FAILURES\n", failures);
    return 1;
}
