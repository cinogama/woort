#include "woort.h"
extern void woort_JIT_compile_env(woort_CodeEnv* cenv);

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static const woort_Int g_elems[4] = { 1000, 1001, 1002, 1003 };

static int run_ldidxvec(uint32_t count, woort_Int idx)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_idx = woort_IRCompiler_add_constant(irc);

    woort_IRConstantIndex* c_elems =
        (woort_IRConstantIndex*)malloc(sizeof(woort_IRConstantIndex) * (count ? count : 1));
    for (uint32_t i = 0; i < count; ++i)
        c_elems[i] = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            woort_IRValue* boxed = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_BOXDYN(f_main, boxed, WOORT_BOX_VALUE_TYPE_INT,
                woort_IRFunction_fetch_const(f_main, c_elems[i]));
            (void)woort_IR_PUSHCHK(f_main, boxed);
        }

        woort_IRValue* v_vec = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKVEC(f_main, v_vec, count);

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDVEC(f_main, v_dst, v_vec, woort_IRFunction_fetch_const(f_main, c_idx));
        (void)woort_IR_ret(f_main, v_dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_idx, idx);
    for (uint32_t i = 0; i < count; ++i)
        woort_CodeEnv_set_const_int(cenv, c_elems[i], g_elems[i]);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL)
    {
        printf("FAIL ldidxvec(count=%u idx=%lld): status=%d\n", (unsigned)count, (long long)idx, (int)st);
        ++failures;
    }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != g_elems[(uint32_t)idx])
        {
            printf("FAIL ldidxvec(count=%u idx=%lld): got %lld\n", (unsigned)count, (long long)idx, (long long)got);
            ++failures;
        }
        else
        {
            printf("ok ldidxvec(count=%u idx=%lld)\n", (unsigned)count, (long long)idx);
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

static int run_ldidxvecx(uint32_t count, woort_Int idx)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_idx = woort_IRCompiler_add_constant(irc);

    woort_IRConstantIndex* c_elems =
        (woort_IRConstantIndex*)malloc(sizeof(woort_IRConstantIndex) * (count ? count : 1));
    for (uint32_t i = 0; i < count; ++i)
        c_elems[i] = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            woort_IRValue* boxed = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_BOXDYN(f_main, boxed, WOORT_BOX_VALUE_TYPE_INT,
                woort_IRFunction_fetch_const(f_main, c_elems[i]));
            (void)woort_IR_PUSHCHK(f_main, boxed);
        }

        woort_IRValue* v_vec = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKVEC(f_main, v_vec, count);

        woort_IRValue* v_boxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDVECX(f_main, v_boxed, v_vec, woort_IRFunction_fetch_const(f_main, c_idx));

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_UNBOXDYN(f_main, v_dst, WOORT_BOX_VALUE_TYPE_INT, v_boxed);
        (void)woort_IR_ret(f_main, v_dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_idx, idx);
    for (uint32_t i = 0; i < count; ++i)
        woort_CodeEnv_set_const_int(cenv, c_elems[i], g_elems[i]);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL)
    {
        printf("FAIL ldidxvecx(count=%u idx=%lld): status=%d\n", (unsigned)count, (long long)idx, (int)st);
        ++failures;
    }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != g_elems[(uint32_t)idx])
        {
            printf("FAIL ldidxvecx(count=%u idx=%lld): got %lld\n", (unsigned)count, (long long)idx, (long long)got);
            ++failures;
        }
        else
        {
            printf("ok ldidxvecx(count=%u idx=%lld)\n", (unsigned)count, (long long)idx);
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

    failures += run_ldidxvec(1, 0);
    failures += run_ldidxvec(3, 0);
    failures += run_ldidxvec(3, 1);
    failures += run_ldidxvec(3, 2);
    failures += run_ldidxvec(4, 3);

    failures += run_ldidxvecx(1, 0);
    failures += run_ldidxvecx(3, 0);
    failures += run_ldidxvecx(3, 1);
    failures += run_ldidxvecx(3, 2);
    failures += run_ldidxvecx(4, 3);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_ldidxvec: ALL PASS\n");
        return 0;
    }
    printf("test_jit_ldidxvec: %d FAILURES\n", failures);
    return 1;
}
