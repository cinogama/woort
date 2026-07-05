#include "woort.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_last_arg = 0;
static int g_call_count = 0;

woort_api native_double_and_print(void)
{
    const woort_Int arg = woort_int(0);
    g_last_arg = (int)arg;
    ++g_call_count;
    printf("native called with %lld\n", (long long)arg);
    return woort_ret_int(arg * 2);
}

woort_api native_noarg_const(void)
{
    ++g_call_count;
    return woort_ret_int(42);
}

static int run_jit_callnfp_witharg(void)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_main;

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_native = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_arg = woort_IRCompiler_add_constant(irc);

    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_arg));

        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNFP(f_main, c_native, 1, r);
        (void)woort_IR_ret(f_main, r);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_extern_function(cenv, c_native, native_double_and_print);
    woort_CodeEnv_set_const_int(cenv, c_arg, 21);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    const int ok = (st == WOORT_VM_CALL_STATUS_NORMAL);
    const woort_Int got = woort_int(sv + 1);
    printf("callnfp witharg: status=%d got=%lld\n", (int)st, (long long)got);
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    if (!ok) { printf("FAIL: status not NORMAL\n"); return 1; }
    if (got != 42) { printf("FAIL: expected 42 got %lld\n", (long long)got); return 1; }
    if (g_last_arg != 21) { printf("FAIL: native saw arg %d, expected 21\n", g_last_arg); return 1; }
    return 0;
}

static int run_jit_callnfp_noarg(void)
{
    g_call_count = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_main;

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_native = woort_IRCompiler_add_constant(irc);

    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNFP(f_main, c_native, 0, r);
        (void)woort_IR_ret(f_main, r);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_extern_function(cenv, c_native, native_noarg_const);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    const int ok = (st == WOORT_VM_CALL_STATUS_NORMAL);
    const woort_Int got = woort_int(sv + 1);
    printf("callnfp noarg: status=%d got=%lld\n", (int)st, (long long)got);
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    if (!ok) { printf("FAIL: status not NORMAL\n"); return 1; }
    if (got != 42) { printf("FAIL: expected 42 got %lld\n", (long long)got); return 1; }
    if (g_call_count != 1) { printf("FAIL: native called %d times\n", g_call_count); return 1; }
    return 0;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;
    failures += run_jit_callnfp_witharg();
    failures += run_jit_callnfp_noarg();

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_callnfp: ALL PASS\n");
        return 0;
    }
    printf("test_jit_callnfp: %d FAILURES\n", failures);
    return 1;
}
