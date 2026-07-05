#include "woort.h"
extern void woort_JIT_compile_env(woort_CodeEnv* cenv);

#include <stdio.h>
#include <stdint.h>
#include <math.h>

typedef bool (*real_binop_builder)(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

typedef bool (*real_unop_builder)(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

static int run_real_binop(real_binop_builder build, const char* name, woort_Real a, woort_Real b, woort_Real expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f_main);
        (void)build(f_main, dst,
            woort_IRFunction_fetch_const(f_main, c_a),
            woort_IRFunction_fetch_const(f_main, c_b));
        (void)woort_IR_ret(f_main, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_real(cenv, c_a, a);
    woort_CodeEnv_set_const_real(cenv, c_b, b);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL %s(%g,%g): status=%d\n", name, a, b, (int)st); ++failures; }
    else
    {
        const woort_Real got = woort_real(sv + 1);
        /* 浮点比较容差：相对误差 1 ULP，外加绝对容差以覆盖零附近 */
        const woort_Real diff = fabs(got - expect);
        const woort_Real tol = fabs(expect) * 2.220446049250313e-16 + 1e-300;
        if (!(diff <= tol)) { printf("FAIL %s(%g,%g): expected %g got %g\n", name, a, b, expect, got); ++failures; }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

/* 单目实数运算（NEGR），结果为实数 */
static int run_real_unop(real_unop_builder build, const char* name, woort_Real src, woort_Real expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_src = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f_main);
        (void)build(f_main, dst, woort_IRFunction_fetch_const(f_main, c_src));
        (void)woort_IR_ret(f_main, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_real(cenv, c_src, src);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL %s(%g): status=%d\n", name, src, (int)st); ++failures; }
    else
    {
        const woort_Real got = woort_real(sv + 1);
        const woort_Real diff = fabs(got - expect);
        const woort_Real tol = fabs(expect) * 2.220446049250313e-16 + 1e-300;
        if (!(diff <= tol)) { printf("FAIL %s(%g): expected %g got %g\n", name, src, expect, got); ++failures; }
    }

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    return failures;
}

/* 实数比较运算，结果为整数 0/1 */
static int run_real_cmpop(real_binop_builder build, const char* name, woort_Real a, woort_Real b, woort_Int expect)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f_main);
        (void)build(f_main, dst,
            woort_IRFunction_fetch_const(f_main, c_a),
            woort_IRFunction_fetch_const(f_main, c_b));
        (void)woort_IR_ret(f_main, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_real(cenv, c_a, a);
    woort_CodeEnv_set_const_real(cenv, c_b, b);
    woort_CodeEnv_unlock(cenv);

    woort_JIT_compile_env(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL) { printf("FAIL %s(%g,%g): status=%d\n", name, a, b, (int)st); ++failures; }
    else
    {
        const woort_Int got = woort_int(sv + 1);
        if (got != expect) { printf("FAIL %s(%g,%g): expected %lld got %lld\n", name, a, b, (long long)expect, (long long)got); ++failures; }
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

    /* ===== ADDR ===== */
    failures += run_real_binop(woort_IR_ADDR, "addr", 1.5, 2.5, 4.0);
    failures += run_real_binop(woort_IR_ADDR, "addr", 0.1, 0.2, 0.30000000000000004);
    failures += run_real_binop(woort_IR_ADDR, "addr", 5.0, -3.0, 2.0);
    failures += run_real_binop(woort_IR_ADDR, "addr", 3.14159, -3.14159, 0.0);
    failures += run_real_binop(woort_IR_ADDR, "addr", 0.0, 0.0, 0.0);
    failures += run_real_binop(woort_IR_ADDR, "addr", 42.0, 0.0, 42.0);
    failures += run_real_binop(woort_IR_ADDR, "addr", -1.25, -2.75, -4.0);
    failures += run_real_binop(woort_IR_ADDR, "addr", 1e15, 1e-15, 1e15 + 1e-15);
    failures += run_real_binop(woort_IR_ADDR, "addr", 1.0 / 3.0, 2.0 / 3.0, 1.0);

    /* ===== SUBR ===== */
    failures += run_real_binop(woort_IR_SUBR, "subr", 5.0, 3.0, 2.0);
    failures += run_real_binop(woort_IR_SUBR, "subr", 3.0, 5.0, -2.0);
    failures += run_real_binop(woort_IR_SUBR, "subr", 5.0, 5.0, 0.0);
    failures += run_real_binop(woort_IR_SUBR, "subr", -2.5, -1.0, -1.5);
    failures += run_real_binop(woort_IR_SUBR, "subr", 1e15, 1e-15, 1e15 - 1e-15);
    failures += run_real_binop(woort_IR_SUBR, "subr", 0.3, 0.1, 0.3 - 0.1);

    /* ===== MULR ===== */
    failures += run_real_binop(woort_IR_MULR, "mulr", 2.5, 4.0, 10.0);
    failures += run_real_binop(woort_IR_MULR, "mulr", -3.0, 2.0, -6.0);
    failures += run_real_binop(woort_IR_MULR, "mulr", -3.0, -3.0, 9.0);
    failures += run_real_binop(woort_IR_MULR, "mulr", 0.0, 1234.5, 0.0);
    failures += run_real_binop(woort_IR_MULR, "mulr", 0.1, 0.1, 0.1 * 0.1);
    failures += run_real_binop(woort_IR_MULR, "mulr", 1e100, 1e-100, 1.0);

    /* ===== DIVR ===== */
    failures += run_real_binop(woort_IR_DIVR, "divr", 10.0, 4.0, 2.5);
    failures += run_real_binop(woort_IR_DIVR, "divr", -9.0, 3.0, -3.0);
    failures += run_real_binop(woort_IR_DIVR, "divr", -9.0, -3.0, 3.0);
    failures += run_real_binop(woort_IR_DIVR, "divr", 1.0, 3.0, 1.0 / 3.0);
    failures += run_real_binop(woort_IR_DIVR, "divr", 0.0, 5.0, 0.0);
    failures += run_real_binop(woort_IR_DIVR, "divr", 1e15, 1e-15, 1e30);

    /* ===== MODR ===== */
    failures += run_real_binop(woort_IR_MODR, "modr", 10.0, 3.0, fmod(10.0, 3.0));
    failures += run_real_binop(woort_IR_MODR, "modr", 10.5, 3.0, fmod(10.5, 3.0));
    failures += run_real_binop(woort_IR_MODR, "modr", -10.0, 3.0, fmod(-10.0, 3.0));
    failures += run_real_binop(woort_IR_MODR, "modr", 10.0, -3.0, fmod(10.0, -3.0));
    failures += run_real_binop(woort_IR_MODR, "modr", 5.0, 5.0, 0.0);
    failures += run_real_binop(woort_IR_MODR, "modr", 3.14159265358979, 2.0, fmod(3.14159265358979, 2.0));

    /* ===== NEGR ===== */
    failures += run_real_unop(woort_IR_NEGR, "negr", 0.0, -0.0);
    failures += run_real_unop(woort_IR_NEGR, "negr", 3.14, -3.14);
    failures += run_real_unop(woort_IR_NEGR, "negr", -2.5, 2.5);
    failures += run_real_unop(woort_IR_NEGR, "negr", 1e100, -1e100);
    failures += run_real_unop(woort_IR_NEGR, "negr", -1e-100, 1e-100);

    /* ===== LTR ===== */
    failures += run_real_cmpop(woort_IR_LTR, "ltr", 1.0, 2.0, 1);
    failures += run_real_cmpop(woort_IR_LTR, "ltr", 2.0, 1.0, 0);
    failures += run_real_cmpop(woort_IR_LTR, "ltr", 2.5, 2.5, 0);
    failures += run_real_cmpop(woort_IR_LTR, "ltr", -3.0, -2.0, 1);
    failures += run_real_cmpop(woort_IR_LTR, "ltr", 0.1, 0.2, 1);

    /* ===== GTR ===== */
    failures += run_real_cmpop(woort_IR_GTR, "gtr", 2.0, 1.0, 1);
    failures += run_real_cmpop(woort_IR_GTR, "gtr", 1.0, 2.0, 0);
    failures += run_real_cmpop(woort_IR_GTR, "gtr", 2.5, 2.5, 0);
    failures += run_real_cmpop(woort_IR_GTR, "gtr", -2.0, -3.0, 1);
    failures += run_real_cmpop(woort_IR_GTR, "gtr", 1e15, 1e15 - 1.0, 1);

    /* ===== LER ===== */
    failures += run_real_cmpop(woort_IR_LER, "ler", 1.0, 2.0, 1);
    failures += run_real_cmpop(woort_IR_LER, "ler", 2.0, 1.0, 0);
    failures += run_real_cmpop(woort_IR_LER, "ler", 2.5, 2.5, 1);
    failures += run_real_cmpop(woort_IR_LER, "ler", -3.0, -3.0, 1);
    failures += run_real_cmpop(woort_IR_LER, "ler", 0.3, 0.1 + 0.2, 1);

    /* ===== GER ===== */
    failures += run_real_cmpop(woort_IR_GER, "ger", 2.0, 1.0, 1);
    failures += run_real_cmpop(woort_IR_GER, "ger", 1.0, 2.0, 0);
    failures += run_real_cmpop(woort_IR_GER, "ger", 2.5, 2.5, 1);
    failures += run_real_cmpop(woort_IR_GER, "ger", -3.0, -3.0, 1);
    failures += run_real_cmpop(woort_IR_GER, "ger", 1.0 / 3.0, 0.3333333333333333, 1);

    /* ===== EQR ===== */
    failures += run_real_cmpop(woort_IR_EQR, "eqr", 2.5, 2.5, 1);
    failures += run_real_cmpop(woort_IR_EQR, "eqr", 2.5, 2.6, 0);
    failures += run_real_cmpop(woort_IR_EQR, "eqr", -0.0, 0.0, 1);
    failures += run_real_cmpop(woort_IR_EQR, "eqr", 0.1 + 0.2, 0.3, 0);

    /* ===== NER ===== */
    failures += run_real_cmpop(woort_IR_NER, "ner", 2.5, 2.6, 1);
    failures += run_real_cmpop(woort_IR_NER, "ner", 2.5, 2.5, 0);
    failures += run_real_cmpop(woort_IR_NER, "ner", -3.0, 3.0, 1);
    failures += run_real_cmpop(woort_IR_NER, "ner", 0.1 + 0.2, 0.3, 1);

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_addr: ALL PASS\n");
        return 0;
    }
    printf("test_jit_addr: %d FAILURES\n", failures);
    return 1;
}
