#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int run_stidveci(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_init = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_idx = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* boxed_init = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, boxed_init, WOORT_BOX_VALUE_TYPE_INT,
            woort_IRFunction_fetch_const(f_main, c_init));
        (void)woort_IR_PUSHCHK(f_main, boxed_init);

        woort_IRValue* v_vec = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKVEC(f_main, v_vec, 1);

        (void)woort_IR_STIDVECI(f_main, v_vec,
            woort_IRFunction_fetch_const(f_main, c_idx),
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDVEC(f_main, v_dst, v_vec,
            woort_IRFunction_fetch_const(f_main, c_idx));
        (void)woort_IR_ret(f_main, v_dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_init, 0);
    woort_CodeEnv_set_const_int(cenv, c_idx, 0);
    woort_CodeEnv_set_const_int(cenv, c_val, 777);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL stidveci: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 777) { printf("FAIL stidveci: got %lld\n", (long long)got); ++failures; }
        else printf("ok stidveci\n");
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_stidvecx(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_init = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_idx = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* boxed_init = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, boxed_init, WOORT_BOX_VALUE_TYPE_INT,
            woort_IRFunction_fetch_const(f_main, c_init));
        (void)woort_IR_PUSHCHK(f_main, boxed_init);

        woort_IRValue* v_vec = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKVEC(f_main, v_vec, 1);

        woort_IRValue* boxed_val = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_BOXDYN(f_main, boxed_val, WOORT_BOX_VALUE_TYPE_INT,
            woort_IRFunction_fetch_const(f_main, c_val));

        (void)woort_IR_STIDVECX(f_main, v_vec,
            woort_IRFunction_fetch_const(f_main, c_idx),
            boxed_val);

        woort_IRValue* v_boxed = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDVECX(f_main, v_boxed, v_vec,
            woort_IRFunction_fetch_const(f_main, c_idx));

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
    woort_CodeEnv_set_const_int(cenv, c_init, 0);
    woort_CodeEnv_set_const_int(cenv, c_idx, 0);
    woort_CodeEnv_set_const_int(cenv, c_val, 888);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL stidvecx: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 888) { printf("FAIL stidvecx: got %lld\n", (long long)got); ++failures; }
        else printf("ok stidvecx\n");
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_stidmapii(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_key = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* v_map = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKMAP(f_main, v_map, 0);

        (void)woort_IR_STIDMAPII(f_main, v_map,
            woort_IRFunction_fetch_const(f_main, c_key),
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDDICTI(f_main, v_dst, v_map,
            woort_IRFunction_fetch_const(f_main, c_key));
        (void)woort_IR_ret(f_main, v_dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_key, 5);
    woort_CodeEnv_set_const_int(cenv, c_val, 333);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL stidmapii: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 333) { printf("FAIL stidmapii: got %lld\n", (long long)got); ++failures; }
        else printf("ok stidmapii\n");
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_stiddictii(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_key = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_init = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_find = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_key));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_init));

        woort_IRValue* v_map = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKMAP(f_main, v_map, 1);

        (void)woort_IR_STIDDICTII(f_main, v_map,
            woort_IRFunction_fetch_const(f_main, c_find),
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDDICTI(f_main, v_dst, v_map,
            woort_IRFunction_fetch_const(f_main, c_find));
        (void)woort_IR_ret(f_main, v_dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_box_int(cenv, c_key, 9);
    woort_CodeEnv_set_const_box_int(cenv, c_init, 111);
    woort_CodeEnv_set_const_int(cenv, c_val, 222);
    woort_CodeEnv_set_const_int(cenv, c_find, 9);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL stiddictii: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 222) { printf("FAIL stiddictii: got %lld\n", (long long)got); ++failures; }
        else printf("ok stiddictii\n");
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

static int run_stidstruct(void)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_f0));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_f1));

        woort_IRValue* v_struct = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKSTRUCT(f_main, v_struct, 2);

        (void)woort_IR_STIDSTRUCT(f_main, v_struct, 1,
            woort_IRFunction_fetch_const(f_main, c_val));

        woort_IRValue* v_dst = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDSTRUCT(f_main, v_dst, v_struct, 1);
        (void)woort_IR_ret(f_main, v_dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_f0, 0);
    woort_CodeEnv_set_const_int(cenv, c_f1, 0);
    woort_CodeEnv_set_const_int(cenv, c_val, 999);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL stidstruct: status=%d\n", (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != 999) { printf("FAIL stidstruct: got %lld\n", (long long)got); ++failures; }
        else printf("ok stidstruct\n");
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

    failures += run_stidveci();
    failures += run_stidvecx();
    failures += run_stidmapii();
    failures += run_stiddictii();
    failures += run_stidstruct();

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_stid: ALL PASS\n");
        return 0;
    }
    printf("test_jit_stid: %d FAILURES\n", failures);
    return 1;
}
