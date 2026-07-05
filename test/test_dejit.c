#include "woort.h"
extern void woort_CodeEnv_dejit(woort_CodeEnv* cenv);

#include <stdio.h>
#include <stdint.h>

static woort_CodeEnv* build_fib_env(
    woort_IRCompiler* irc,
    woort_IRConstantIndex* out_c_entry)
{
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfib = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_fib;
    (void)woort_IRCompiler_add_function(irc, 1, 0, &f_fib);
    {
        woort_IRValue* n_arg = woort_IRFunction_get_argument(f_fib, 0);
        woort_IRValue* tmp1 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* tmp2 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f_fib);
        woort_IRLabel* L_base = woort_IRFunction_new_label(f_fib);

        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f_fib, c2);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f_fib, c1);

        (void)woort_IR_jcc_lt(f_fib, n_arg, v2, L_base);

        (void)woort_IR_SUBI(f_fib, tmp1, n_arg, v1);
        (void)woort_IR_SUBI(f_fib, tmp2, n_arg, v2);

        (void)woort_IR_PUSHCHK(f_fib, tmp1);
        (void)woort_IR_CALLNWO(f_fib, cfib, 1, r1);

        (void)woort_IR_PUSHCHK(f_fib, tmp2);
        (void)woort_IR_CALLNWO(f_fib, cfib, 1, r2);

        (void)woort_IR_ADDI(f_fib, sum, r1, r2);
        (void)woort_IR_ret(f_fib, sum);

        (void)woort_IR_bind(f_fib, L_base);
        (void)woort_IR_ret(f_fib, n_arg);
    }

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* result = woort_IRFunction_new_vreg(f_main);
        const woort_IRValue* vn = woort_IRFunction_fetch_const(f_main, cn);
        (void)woort_IR_PUSHCHK(f_main, vn);
        (void)woort_IR_CALLNWO(f_main, cfib, 1, result);
        (void)woort_IR_ret(f_main, result);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* fib_addr;
    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_fib, &fib_addr);
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    (void)woort_CodeEnv_set_const_int(cenv, c2, 2);
    (void)woort_CodeEnv_set_const_int(cenv, c1, 1);
    (void)woort_CodeEnv_set_const_script_function(cenv, cfib, fib_addr);
    (void)woort_CodeEnv_set_const_int(cenv, cn, 10);
    (void)woort_CodeEnv_set_const_script_closure(cenv, c_entry, main_addr);
    woort_CodeEnv_unlock(cenv);

    *out_c_entry = c_entry;
    return cenv;
}

static woort_VmCallStatus invoke_entry(woort_CodeEnv* cenv, woort_IRConstantIndex c_entry, woort_Int* out)
{
    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st == WOORT_VM_CALL_STATUS_NORMAL)
        *out = woort_int(sv + 1);
    woort_pop(2);
    return st;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();
    woort_IRConstantIndex c_entry;
    woort_CodeEnv* cenv = build_fib_env(irc, &c_entry);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    {
        woort_Int got = 0;
        const woort_VmCallStatus st = invoke_entry(cenv, c_entry, &got);
        if (st != WOORT_VM_CALL_STATUS_NORMAL)
        { printf("FAIL jit-run: status=%d\n", (int)st); ++failures; }
        else if (got != 55)
        { printf("FAIL jit-run: expected 55 got %lld\n", (long long)got); ++failures; }
        else
        { printf("ok   jit-run: fib(10) = %lld\n", (long long)got); }
    }

    woort_CodeEnv_dejit(cenv);

    {
        woort_Int got = 0;
        const woort_VmCallStatus st = invoke_entry(cenv, c_entry, &got);
        if (st != WOORT_VM_CALL_STATUS_NORMAL)
        { printf("FAIL interp-run (after dejit): status=%d\n", (int)st); ++failures; }
        else if (got != 55)
        { printf("FAIL interp-run (after dejit): expected 55 got %lld\n", (long long)got); ++failures; }
        else
        { printf("ok   interp-run (after dejit): fib(10) = %lld\n", (long long)got); }
    }

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_dejit: ALL PASS\n");
        return 0;
    }
    printf("test_dejit: %d FAILURES\n", failures);
    return 1;
}
