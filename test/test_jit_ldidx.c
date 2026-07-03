#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int run_ldidxdict_int(int boxed_result)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_key = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_find = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_key));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_map = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKMAP(f_main, v_map, 1);

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        if (boxed_result)
            (void)woort_IR_LDIDXDICTIX(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));
        else
            (void)woort_IR_LDIDXDICTI(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));

        if (boxed_result)
        {
            woort_IRValue* v_unboxed = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_UNBOXDYN(f_main, v_unboxed, WOORT_BOX_VALUE_TYPE_INT, v_dst);
            (void)woort_IR_ret(f_main, v_unboxed);
        }
        else
        {
            (void)woort_IR_ret(f_main, v_dst);
        }
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_box_int(cenv, c_key, 100);
    woort_CodeEnv_set_const_box_int(cenv, c_val, 2000);
    woort_CodeEnv_set_const_int(cenv, c_find, 100);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL ldidxdict_int(%d): status=%d\n", boxed_result, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 2000) { printf("FAIL ldidxdict_int(%d): got %lld\n", boxed_result, (long long)got); ++failures; }
        else printf("ok ldidxdict_int(%d)\n", boxed_result);
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_ldidxdict_real(int boxed_result)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_key = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_find = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_key));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_map = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKMAP(f_main, v_map, 1);

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        if (boxed_result)
            (void)woort_IR_LDIDXDICTRX(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));
        else
            (void)woort_IR_LDIDXDICTR(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));

        if (boxed_result)
        {
            woort_IRValue* v_unboxed = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_UNBOXDYN(f_main, v_unboxed, WOORT_BOX_VALUE_TYPE_INT, v_dst);
            (void)woort_IR_ret(f_main, v_unboxed);
        }
        else
        {
            (void)woort_IR_ret(f_main, v_dst);
        }
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_box_real(cenv, c_key, 3.5);
    woort_CodeEnv_set_const_box_int(cenv, c_val, 7000);
    woort_CodeEnv_set_const_real(cenv, c_find, 3.5);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL ldidxdict_real(%d): status=%d\n", boxed_result, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 7000) { printf("FAIL ldidxdict_real(%d): got %lld\n", boxed_result, (long long)got); ++failures; }
        else printf("ok ldidxdict_real(%d)\n", boxed_result);
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_ldidxdict_bool(int boxed_result)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_key = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_find = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_key));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_map = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKMAP(f_main, v_map, 1);

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        if (boxed_result)
            (void)woort_IR_LDIDXDICTBX(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));
        else
            (void)woort_IR_LDIDXDICTB(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));

        if (boxed_result)
        {
            woort_IRValue* v_unboxed = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_UNBOXDYN(f_main, v_unboxed, WOORT_BOX_VALUE_TYPE_INT, v_dst);
            (void)woort_IR_ret(f_main, v_unboxed);
        }
        else
        {
            (void)woort_IR_ret(f_main, v_dst);
        }
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_box_bool(cenv, c_key, 1);
    woort_CodeEnv_set_const_box_int(cenv, c_val, 8000);
    woort_CodeEnv_set_const_int(cenv, c_find, 1);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL ldidxdict_bool(%d): status=%d\n", boxed_result, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 8000) { printf("FAIL ldidxdict_bool(%d): got %lld\n", boxed_result, (long long)got); ++failures; }
        else printf("ok ldidxdict_bool(%d)\n", boxed_result);
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_ldidxdict_dyn(int boxed_result)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_key = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_find = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_key));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_map = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKMAP(f_main, v_map, 1);

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        if (boxed_result)
            (void)woort_IR_LDIDXDICTXX(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));
        else
            (void)woort_IR_LDIDXDICTX(f_main, v_dst, v_map, woort_IRFunction_fetch_const(f_main, c_find));

        if (boxed_result)
        {
            woort_IRValue* v_unboxed = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_UNBOXDYN(f_main, v_unboxed, WOORT_BOX_VALUE_TYPE_INT, v_dst);
            (void)woort_IR_ret(f_main, v_unboxed);
        }
        else
        {
            (void)woort_IR_ret(f_main, v_dst);
        }
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_box_int(cenv, c_key, 100);
    woort_CodeEnv_set_const_box_int(cenv, c_val, 2000);
    woort_CodeEnv_set_const_box_int(cenv, c_find, 100);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL ldidxdict_dyn(%d): status=%d\n", boxed_result, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 2000) { printf("FAIL ldidxdict_dyn(%d): got %lld\n", boxed_result, (long long)got); ++failures; }
        else printf("ok ldidxdict_dyn(%d)\n", boxed_result);
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_ldidstruct(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f2 = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_f0));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_f1));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_f2));

        woort_IRValue* v_struct = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKSTRUCT(f_main, v_struct, 3);

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDXSTRUCT(f_main, v_dst, v_struct, 1);
        (void)woort_IR_ret(f_main, v_dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_f0, 10);
    woort_CodeEnv_set_const_int(cenv, c_f1, 20);
    woort_CodeEnv_set_const_int(cenv, c_f2, 30);
    woort_CodeEnv_unlock(cenv);

    woort_codeenv_jit_compile_(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL ldidstruct: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 20) { printf("FAIL ldidstruct: got %lld\n", (long long)got); ++failures; }
        else printf("ok ldidstruct\n");
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

    failures += run_ldidxdict_int(0);
    failures += run_ldidxdict_int(1);
    failures += run_ldidxdict_real(0);
    failures += run_ldidxdict_real(1);
    failures += run_ldidxdict_bool(0);
    failures += run_ldidxdict_bool(1);
    failures += run_ldidxdict_dyn(0);
    failures += run_ldidxdict_dyn(1);
    failures += run_ldidstruct();

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_ldidx: ALL PASS\n");
        return 0;
    }
    printf("test_jit_ldidx: %d FAILURES\n", failures);
    return 1;
}
