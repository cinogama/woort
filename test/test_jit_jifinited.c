#include "woort.h"

#include <stdio.h>
#include <stdint.h>

extern void woort_JIT_compile_env(woort_CodeEnv* cenv);

static int run_jifinited_fallthrough(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRStaticIndex s_flag = woort_IRCompiler_add_static(irc);
    woort_IRStaticIndex s_value = woort_IRCompiler_add_static(irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f);
    {
        woort_IRLabel* L_done = woort_IRFunction_new_label(f);

        (void)woort_IR_jifinited(f, s_flag, L_done);

        const woort_IRValue* v42 = woort_IRFunction_fetch_const(f, c42);
        (void)woort_IR_ASTORE(f, s_value, v42);

        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f, c2);
        (void)woort_IR_ASTORE(f, s_flag, v2);

        (void)woort_IR_bind(f, L_done);

        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        (void)woort_IR_ALOAD(f, result, s_value);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* entry_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &entry_addr);
    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c42, 42);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, entry_addr);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL jifinited_fallthrough: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 42) { printf("FAIL jifinited_fallthrough: expected 42 got %lld\n", (long long)got); ++failures; }
    }
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_jifinited_multi_invoke(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRStaticIndex s_flag = woort_IRCompiler_add_static(irc);
    woort_IRStaticIndex s_value = woort_IRCompiler_add_static(irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f);
    {
        woort_IRLabel* L_done = woort_IRFunction_new_label(f);

        (void)woort_IR_jifinited(f, s_flag, L_done);

        const woort_IRValue* v42 = woort_IRFunction_fetch_const(f, c42);
        (void)woort_IR_ASTORE(f, s_value, v42);

        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f, c2);
        (void)woort_IR_ASTORE(f, s_flag, v2);

        (void)woort_IR_bind(f, L_done);

        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        (void)woort_IR_ALOAD(f, result, s_value);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* entry_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &entry_addr);
    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c42, 42);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, entry_addr);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    for (int i = 0; i < 3; ++i)
    {
        woort_StackValue sv;
        (void)woort_push_reserve(2, &sv);
        woort_load_const(sv, cenv, c_entry);
        const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
        if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL jifinited_multi[%d]: status=%d\n", i, (int)st); ++failures; }
        else
        {
            const woort_Int got = woort_int(sv + 1);
            if (got != 42) { printf("FAIL jifinited_multi[%d]: expected 42 got %lld\n", i, (long long)got); ++failures; }
        }
        woort_pop(2);
    }

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_cas(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_exp = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_des = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_zero = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRStaticIndex s_val = woort_IRCompiler_add_static(irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f);
    {
        woort_IRValue* v_result = woort_IRFunction_new_vreg(f);
        woort_IRValue* v_exp = woort_IRFunction_new_vreg(f);
        woort_IRValue* v_des = woort_IRFunction_new_vreg(f);

        const woort_IRValue* v_init = woort_IRFunction_fetch_const(f, c10);
        (void)woort_IR_ASTORE(f, s_val, v_init);

        const woort_IRValue* k_exp = woort_IRFunction_fetch_const(f, c_exp);
        const woort_IRValue* k_zero = woort_IRFunction_fetch_const(f, c_zero);
        (void)woort_IR_ADDI(f, v_exp, k_exp, k_zero);

        const woort_IRValue* k_des = woort_IRFunction_fetch_const(f, c_des);
        const woort_IRValue* k_zero2 = woort_IRFunction_fetch_const(f, c_zero);
        (void)woort_IR_ADDI(f, v_des, k_des, k_zero2);

        (void)woort_IR_CAS(f, s_val, v_exp, v_des);

        (void)woort_IR_ALOAD(f, v_result, s_val);
        (void)woort_IR_ret(f, v_result);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* entry_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &entry_addr);
    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c_exp, 10);
    woort_CodeEnv_set_const_int(cenv, c_des, 99);
    woort_CodeEnv_set_const_int(cenv, c_zero, 0);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, entry_addr);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL cas: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 99) { printf("FAIL cas: expected 99 got %lld\n", (long long)got); ++failures; }
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

    failures += run_jifinited_fallthrough();
    failures += run_jifinited_multi_invoke();
    failures += run_cas();

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_jifinited: ALL PASS\n");
        return 0;
    }
    printf("test_jit_jifinited: %d FAILURES\n", failures);
    return 1;
}
