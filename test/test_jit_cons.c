#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static woort_IRConstantIndex* alloc_consts(woort_IRCompiler* irc, size_t count)
{
    woort_IRConstantIndex* arr =
        (woort_IRConstantIndex*)malloc(sizeof(woort_IRConstantIndex) * (count ? count : 1));
    for (size_t i = 0; i < count; ++i)
        arr[i] = woort_IRCompiler_add_constant(irc);
    return arr;
}

static int run_mkstruct_case(uint32_t fields)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex* c_fields = alloc_consts(irc, fields);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        for (uint32_t i = 0; i < fields; ++i)
            (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_fields[i]));

        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKSTRUCT(f_main, v, fields);
        (void)woort_IR_ret(f_main, v);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    for (uint32_t i = 0; i < fields; ++i)
        woort_CodeEnv_set_const_int(cenv, c_fields[i], (woort_Int)(1000 + i));
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    printf("mkstruct fields=%u: status=%d\n", (unsigned)fields, (int)st);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL: status not NORMAL\n"); ++failures; }

    if (failures == 0)
    {
        const size_t len = woort_struct_len(sv + 1);
        if (len != fields) { printf("FAIL: expected len %u got %llu\n", (unsigned)fields, (unsigned long long)len); ++failures; }

        for (uint32_t i = 0; i < fields && failures == 0; ++i)
        {
            woort_StackValue elem;
            (void)woort_push_reserve(1, &elem);
            woort_struct_get(elem, sv + 1, i);
            const woort_Int val = woort_int(elem);
            if (val != (woort_Int)(1000 + i))
            {
                printf("FAIL: field[%u] expected %d got %lld\n", (unsigned)i, 1000 + (int)i, (long long)val);
                ++failures;
            }
            woort_pop(1);
        }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    free(c_fields);
    return failures;
}

static int run_mkunion_case(uint32_t union_id)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_payload = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKUNION(f_main, v, woort_IRFunction_fetch_const(f_main, c_payload), union_id);
        (void)woort_IR_ret(f_main, v);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_payload, 7777);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    printf("mkunion id=%u: status=%d\n", (unsigned)union_id, (int)st);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL: status not NORMAL\n"); ++failures; }

    if (failures == 0)
    {
        const size_t len = woort_struct_len(sv + 1);
        if (len != 2) { printf("FAIL: union must have 2 fields, got %llu\n", (unsigned long long)len); ++failures; }

        woort_StackValue elem;
        (void)woort_push_reserve(1, &elem);
        woort_struct_get(elem, sv + 1, 0);
        const woort_Int got_id = woort_int(elem);
        if (got_id != (woort_Int)union_id) { printf("FAIL: union id expected %u got %lld\n", (unsigned)union_id, (long long)got_id); ++failures; }

        woort_struct_get(elem, sv + 1, 1);
        const woort_Int got_payload = woort_int(elem);
        if (got_payload != 7777) { printf("FAIL: union payload expected 7777 got %lld\n", (long long)got_payload); ++failures; }
        woort_pop(1);
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_mkclosure_case(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_tmpl = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_upval = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_inner;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_inner);
    {
        (void)woort_IR_ret(f_inner, woort_IRFunction_fetch_const(f_inner, c_upval));
    }

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_upval));

        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKCLOSURE(f_main, v, 1, c_tmpl);
        (void)woort_IR_ret(f_main, v);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* inner_addr;
    (void)woort_CodeEnv_query_function(cenv, f_inner, &inner_addr);
    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_script_closure(cenv, c_tmpl, inner_addr);
    woort_CodeEnv_set_const_int(cenv, c_upval, 4242);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    printf("mkclosure: status=%d\n", (int)st);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL: status not NORMAL\n"); ++failures; }

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
    failures += run_mkstruct_case(0);
    failures += run_mkstruct_case(1);
    failures += run_mkstruct_case(3);
    failures += run_mkunion_case(0);
    failures += run_mkunion_case(5);
    failures += run_mkclosure_case();

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_cons: ALL PASS\n");
        return 0;
    }
    printf("test_jit_cons: %d FAILURES\n", failures);
    return 1;
}
