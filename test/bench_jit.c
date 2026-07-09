/*
 * bench_jit.c — micro-benchmarks for the JIT code generator.
 *
 * Two kernels are built via the IR compiler, JIT-compiled, and timed:
 *
 *   1. "numeric_chain"  — a long straight-line chain of dependent integer
 *      additions (t = t + 1, repeated CHAIN times). Exercises A1 (TOS register
 *      cache): each ADD's result flows to the next without an SB reload.
 *
 *   2. "vec_unbox"      — indexes every element of a fixed dynamic-int vector
 *      and sums them. Exercises A3 (inlined woort_DynBox_unbox_no_check) per
 *      element, plus the ADD chain.
 *
 * Run in a Release build (NDEBUG) so the JIT is actually executed rather than
 * falling back to the interpreter. Compare the reported ns/op figures against
 * a baseline build (e.g. `git stash` these changes) to quantify the speedup.
 */

#include "woort.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define CHAIN_LEN   1000   /* dependent ADDIs per numeric_chain invocation */
#define VEC_ELEMS   16     /* dynamic-int elements summed per vec_unbox    */
#define ITERS       200000 /* invocations per timed kernel                 */

/* ------------------------------------------------------------------ */
/* portable monotonic-ish timer (C11)                                 */
/* ------------------------------------------------------------------ */
static double now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* kernel 1: numeric chain                                            */
/* ------------------------------------------------------------------ */
static int bench_numeric_chain(void)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_one  = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        const woort_IRValue* one = woort_IRFunction_fetch_const(f_main, c_one);

        woort_IRValue* acc = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MOV(f_main, acc, one);

        for (int i = 1; i < CHAIN_LEN; ++i)
        {
            woort_IRValue* nxt = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_ADDI(f_main, nxt, acc, one);
            acc = nxt;
        }

        (void)woort_IR_ret(f_main, acc);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_set_const_int(cenv, c_one, 1);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);

    /* verify */
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    const woort_Int got = woort_int(sv + 1);
    if (st != WOORT_VM_CALL_STATUS_NORMAL || got != CHAIN_LEN)
    {
        printf("numeric_chain: BAD result status=%d got=%lld\n", (int)st, (long long)got);
        woort_pop(2);
        (void)woort_VMRuntime_swap(NULL);
        woort_CodeEnv_drop(cenv);
        woort_VMRuntime_destroy(vm);
        woort_IRCompiler_close(irc);
        return 1;
    }

    /* timed */
    double t0 = now_seconds();
    for (int i = 0; i < ITERS; ++i)
        (void)woort_invoke(sv + 1, sv);
    double dt = now_seconds() - t0;

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    printf("numeric_chain : %8d iters, %7.3f ms total, %6.2f ns/invoke, %6.3f ns/ADDI (chain=%d)\n",
        ITERS, dt * 1e3, dt * 1e9 / ITERS, dt * 1e9 / ((double)ITERS * CHAIN_LEN), CHAIN_LEN);
    return 0;
}

/* ------------------------------------------------------------------ */
/* kernel 2: vector element sum (inlined unbox per element)           */
/* ------------------------------------------------------------------ */
static int bench_vec_unbox(void)
{
    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_idxs[VEC_ELEMS];
    woort_IRConstantIndex c_vals[VEC_ELEMS];
    for (int i = 0; i < VEC_ELEMS; ++i)
    {
        c_idxs[i] = woort_IRCompiler_add_constant(irc);
        c_vals[i] = woort_IRCompiler_add_constant(irc);
    }

    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        /* build the vector of dynamic ints */
        for (int i = 0; i < VEC_ELEMS; ++i)
        {
            woort_IRValue* boxed = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_BOXDYN(f_main, boxed, WOORT_BOX_VALUE_TYPE_INT,
                woort_IRFunction_fetch_const(f_main, c_vals[i]));
            (void)woort_IR_PUSHCHK(f_main, boxed);
        }
        woort_IRValue* v_vec = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_MKVEC(f_main, v_vec, VEC_ELEMS);

        /* sum every element: acc = acc + vec[i] (each LDIDVEC is an inlined unbox) */
        woort_IRValue* acc = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_LDIDVEC(f_main, acc, v_vec,
            woort_IRFunction_fetch_const(f_main, c_idxs[0]));
        for (int i = 1; i < VEC_ELEMS; ++i)
        {
            woort_IRValue* elem = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_LDIDVEC(f_main, elem, v_vec,
                woort_IRFunction_fetch_const(f_main, c_idxs[i]));
            woort_IRValue* nxt = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_ADDI(f_main, nxt, acc, elem);
            acc = nxt;
        }
        (void)woort_IR_ret(f_main, acc);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    for (int i = 0; i < VEC_ELEMS; ++i)
    {
        woort_CodeEnv_set_const_int(cenv, c_idxs[i], (woort_Int)i);
        woort_CodeEnv_set_const_int(cenv, c_vals[i], (woort_Int)(i + 1));
    }
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_jit(cenv);

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);

    /* verify */
    const woort_Int expect = (woort_Int)VEC_ELEMS * (VEC_ELEMS + 1) / 2;
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    const woort_Int got = woort_int(sv + 1);
    if (st != WOORT_VM_CALL_STATUS_NORMAL || got != expect)
    {
        printf("vec_unbox: BAD result status=%d got=%lld expect=%lld\n",
            (int)st, (long long)got, (long long)expect);
        woort_pop(2);
        (void)woort_VMRuntime_swap(NULL);
        woort_CodeEnv_drop(cenv);
        woort_VMRuntime_destroy(vm);
        woort_IRCompiler_close(irc);
        return 1;
    }

    /* timed */
    double t0 = now_seconds();
    for (int i = 0; i < ITERS; ++i)
        (void)woort_invoke(sv + 1, sv);
    double dt = now_seconds() - t0;

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    printf("vec_unbox     : %8d iters, %7.3f ms total, %6.2f ns/invoke, %6.3f ns/unbox (elems=%d)\n",
        ITERS, dt * 1e3, dt * 1e9 / ITERS, dt * 1e9 / ((double)ITERS * VEC_ELEMS), VEC_ELEMS);
    return 0;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    int failures = 0;
    failures += bench_numeric_chain();
    failures += bench_vec_unbox();

    woort_shutdown(NULL, NULL);

    if (failures != 0)
        printf("bench_jit: %d kernel(s) failed\n", failures);
    return failures ? 1 : 0;
}
