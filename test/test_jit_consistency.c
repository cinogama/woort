#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <math.h>

/*
 * JIT/interpreter consistency test.
 *
 * For every bytecode touched by the x64 JIT optimization pass this program
 * builds the same IR function once, executes it on the interpreter (before
 * woort_CodeEnv_jit attaches a JIT entry point) and then on the JIT, and
 * asserts both backends produce identical results.
 *
 * Special focus on the real comparison opcodes (LTR/GTR/LER/GER/EQR/NER)
 * with NaN / signed-zero / infinity operands, which previously diverged.
 */

typedef bool (*binop_builder)(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* a,
    const woort_IRValue* b);

typedef bool (*unop_builder)(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src);

typedef struct { woort_Real a, b; } rpair_t;
typedef struct { woort_Int a, b; } ipair_t;

/* ------------------------------------------------------------------ */
/* common helpers                                                      */
/* ------------------------------------------------------------------ */

static woort_CodeEnv* build_binop_cenv(
    binop_builder build,
    woort_IRConstantIndex* out_main,
    woort_IRConstantIndex* out_a,
    woort_IRConstantIndex* out_b,
    woort_IRFunction** out_func)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();

    const woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    const woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    const woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f);
        (void)build(f, dst,
            woort_IRFunction_fetch_const(f, c_a),
            woort_IRFunction_fetch_const(f, c_b));
        (void)woort_IR_ret(f, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    *out_main = c_main;
    *out_a = c_a;
    *out_b = c_b;
    *out_func = f;
    return cenv;
}

static int invoke_and_read_int(woort_CodeEnv* cenv, woort_IRConstantIndex c_main, woort_Int* out)
{
    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);

    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    int ok = 1;
    if (st != WOORT_VM_CALL_STATUS_NORMAL)
        ok = 0;
    else
        *out = woort_int(sv + 1);

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);
    return ok;
}

/* ------------------------------------------------------------------ */
/* real-typed operand differential (result is int 0/1)                */
/* ------------------------------------------------------------------ */

static int diff_real_binop(binop_builder build, const char* name, woort_Real a, woort_Real b)
{
    int failures = 0;

    woort_IRConstantIndex c_main, c_a, c_b;
    woort_IRFunction* f;
    woort_CodeEnv* cenv = build_binop_cenv(build, &c_main, &c_a, &c_b, &f);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_real(cenv, c_a, a);
    woort_CodeEnv_set_const_real(cenv, c_b, b);
    woort_CodeEnv_unlock(cenv);

    woort_Int got_interp = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_interp))
    {
        printf("FAIL %s interp(%g,%g): abnormal status\n", name, a, b);
        ++failures;
    }

    woort_CodeEnv_jit(cenv);

    woort_Int got_jit = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_jit))
    {
        printf("FAIL %s jit(%g,%g): abnormal status\n", name, a, b);
        ++failures;
    }

    woort_CodeEnv_drop(cenv);

    if (got_interp != got_jit)
    {
        printf("FAIL %s(%g,%g): interp=%lld jit=%lld\n",
            name, a, b, (long long)got_interp, (long long)got_jit);
        ++failures;
    }

    return failures;
}

/* ------------------------------------------------------------------ */
/* integer-typed operand differential (result is int 0/1)             */
/* ------------------------------------------------------------------ */

static int diff_int_binop(binop_builder build, const char* name, woort_Int a, woort_Int b)
{
    int failures = 0;

    woort_IRConstantIndex c_main, c_a, c_b;
    woort_IRFunction* f;
    woort_CodeEnv* cenv = build_binop_cenv(build, &c_main, &c_a, &c_b, &f);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_a, a);
    woort_CodeEnv_set_const_int(cenv, c_b, b);
    woort_CodeEnv_unlock(cenv);

    woort_Int got_interp = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_interp))
    {
        printf("FAIL %s interp(%lld,%lld): abnormal status\n", name, (long long)a, (long long)b);
        ++failures;
    }

    woort_CodeEnv_jit(cenv);

    woort_Int got_jit = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_jit))
    {
        printf("FAIL %s jit(%lld,%lld): abnormal status\n", name, (long long)a, (long long)b);
        ++failures;
    }

    woort_CodeEnv_drop(cenv);

    if (got_interp != got_jit)
    {
        printf("FAIL %s(%lld,%lld): interp=%lld jit=%lld\n",
            name, (long long)a, (long long)b, (long long)got_interp, (long long)got_jit);
        ++failures;
    }

    return failures;
}

/* ------------------------------------------------------------------ */
/* integer unary (LNOT) differential                                  */
/* ------------------------------------------------------------------ */

static int diff_int_unop(unop_builder build, const char* name, woort_Int a)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();
    const woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    const woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f);
    {
        woort_IRValue* dst = woort_IRFunction_new_vreg(f);
        (void)build(f, dst, woort_IRFunction_fetch_const(f, c_a));
        (void)woort_IR_ret(f, dst);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);
    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_a, a);
    woort_CodeEnv_unlock(cenv);

    woort_Int got_interp = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_interp))
    {
        printf("FAIL %s interp(%lld): abnormal status\n", name, (long long)a);
        ++failures;
    }

    woort_CodeEnv_jit(cenv);

    woort_Int got_jit = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_jit))
    {
        printf("FAIL %s jit(%lld): abnormal status\n", name, (long long)a);
        ++failures;
    }

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    if (got_interp != got_jit)
    {
        printf("FAIL %s(%lld): interp=%lld jit=%lld\n",
            name, (long long)a, (long long)got_interp, (long long)got_jit);
        ++failures;
    }

    return failures;
}

/* ------------------------------------------------------------------ */
/* compound integer div/mod (CDIVI/CMODI): dst == src[0]              */
/* ------------------------------------------------------------------ */

static int diff_compound_divmod(int is_mod, const char* name, woort_Int x, woort_Int y)
{
    int failures = 0;

    woort_IRCompiler* irc = woort_IRCompiler_create();
    const woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    const woort_IRConstantIndex c_x = woort_IRCompiler_add_constant(irc);
    const woort_IRConstantIndex c_y = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f);
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        const woort_IRValue* vx = woort_IRFunction_fetch_const(f, c_x);
        const woort_IRValue* vy = woort_IRFunction_fetch_const(f, c_y);
        (void)woort_IR_MOV(f, a, vx);
        if (is_mod)
            (void)woort_IR_MODI(f, a, a, vy);
        else
            (void)woort_IR_DIVI(f, a, a, vy);
        (void)woort_IR_ret(f, a);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);
    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_x, x);
    woort_CodeEnv_set_const_int(cenv, c_y, y);
    woort_CodeEnv_unlock(cenv);

    woort_Int got_interp = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_interp))
    {
        printf("FAIL %s interp(%lld,%lld): abnormal status\n", name, (long long)x, (long long)y);
        ++failures;
    }

    woort_CodeEnv_jit(cenv);

    woort_Int got_jit = 0;
    if (!invoke_and_read_int(cenv, c_main, &got_jit))
    {
        printf("FAIL %s jit(%lld,%lld): abnormal status\n", name, (long long)x, (long long)y);
        ++failures;
    }

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    if (got_interp != got_jit)
    {
        printf("FAIL %s(%lld,%lld): interp=%lld jit=%lld\n",
            name, (long long)x, (long long)y, (long long)got_interp, (long long)got_jit);
        ++failures;
    }

    return failures;
}

/* ------------------------------------------------------------------ */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;

    static const rpair_t rpairs[] = {
        {1.0, 2.0}, {2.0, 1.0}, {1.5, 1.5}, {-3.0, -2.0}, {0.1, 0.2},
        {NAN, 1.0}, {1.0, NAN}, {NAN, NAN}, {NAN, INFINITY}, {INFINITY, NAN},
        {-0.0, 0.0}, {0.0, -0.0}, {-0.0, -0.0},
        {INFINITY, 1.0}, {1.0, INFINITY}, {-INFINITY, INFINITY},
        {INFINITY, INFINITY}, {-INFINITY, -INFINITY},
        {1e308, -1e308}, {3.0, 3.0}
    };
    const int nrpairs = (int)(sizeof(rpairs) / sizeof(rpairs[0]));

    static const binop_builder real_ops[] = {
        woort_IR_LTR, woort_IR_GTR, woort_IR_LER,
        woort_IR_GER, woort_IR_EQR, woort_IR_NER
    };
    static const char* const real_names[] = {
        "LTR", "GTR", "LER", "GER", "EQR", "NER"
    };
    for (int op = 0; op < 6; ++op)
        for (int i = 0; i < nrpairs; ++i)
            failures += diff_real_binop(real_ops[op], real_names[op], rpairs[i].a, rpairs[i].b);

    static const ipair_t ipairs[] = {
        {1, 2}, {2, 1}, {5, 5}, {-5, -3}, {0, 0}, {-1, 0},
        {(1LL << 40), -(1LL << 40)}, {INT64_MIN, INT64_MAX},
        {INT64_MAX, INT64_MAX}, {-7, -7}
    };
    const int nipairs = (int)(sizeof(ipairs) / sizeof(ipairs[0]));

    static const binop_builder int_ops[] = {
        woort_IR_LTI, woort_IR_GTI, woort_IR_LEI,
        woort_IR_GEI, woort_IR_EQI, woort_IR_NEI
    };
    static const char* const int_names[] = {
        "LTI", "GTI", "LEI", "GEI", "EQI", "NEI"
    };
    for (int op = 0; op < 6; ++op)
        for (int i = 0; i < nipairs; ++i)
            failures += diff_int_binop(int_ops[op], int_names[op], ipairs[i].a, ipairs[i].b);

    static const ipair_t lpairs[] = {
        {0, 0}, {0, 1}, {1, 0}, {1, 1}, {5, 0}, {0, 5}, {-3, 7}
    };
    const int nlpairs = (int)(sizeof(lpairs) / sizeof(lpairs[0]));
    for (int i = 0; i < nlpairs; ++i)
    {
        failures += diff_int_binop(woort_IR_LAND, "LAND", lpairs[i].a, lpairs[i].b);
        failures += diff_int_binop(woort_IR_LOR, "LOR", lpairs[i].a, lpairs[i].b);
    }
    {
        static const woort_Int lnot_vals[] = {0, 1, 5, -3};
        for (int i = 0; i < 4; ++i)
            failures += diff_int_unop(woort_IR_LNOT, "LNOT", lnot_vals[i]);
    }

    static const ipair_t dpairs[] = {
        {10, 3}, {10, -3}, {-10, 3}, {-10, -3}, {7, 1}, {100, 7},
        {INT64_MIN, 1}, {INT64_MIN, 2}, {1, 7}, {0, 5}
    };
    const int ndpairs = (int)(sizeof(dpairs) / sizeof(dpairs[0]));
    for (int i = 0; i < ndpairs; ++i)
    {
        failures += diff_compound_divmod(0, "CDIVI", dpairs[i].a, dpairs[i].b);
        failures += diff_compound_divmod(1, "CMODI", dpairs[i].a, dpairs[i].b);
    }

    woort_shutdown(NULL, NULL);

    if (failures == 0)
    {
        printf("test_jit_consistency: ALL PASS\n");
        return 0;
    }
    printf("test_jit_consistency: %d FAILURES\n", failures);
    return 1;
}
