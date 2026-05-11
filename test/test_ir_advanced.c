#include "woort.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ========== Test Infrastructure ========== */

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_BEGIN(name)                                        \
    do {                                                        \
        const char* _test_name = (name);                        \
        g_tests_run++;                                          \
        (void)printf("  [TEST] %-45s ", _test_name);            \
        fflush(stdout);

#define TEST_END()                                              \
        (void)printf("PASS\n");                                 \
        g_tests_passed++;                                       \
    } while(0)

#define TEST_ASSERT(cond)                                       \
    do {                                                        \
        if (!(cond)) {                                          \
            (void)printf("FAIL\n");                             \
            (void)printf("    assert failed: %s\n", #cond);     \
            (void)printf("    at %s:%d\n", __FILE__, __LINE__); \
            return;                                             \
        }                                                       \
    } while(0)

#define TEST_ASSERT_EQ_INT(expected, actual)                     \
    do {                                                        \
        woort_Int _e = (expected);                              \
        woort_Int _a = (actual);                                \
        if (_e != _a) {                                         \
            (void)printf("FAIL\n");                             \
            (void)printf("    expected: %lld, actual: %lld\n",  \
                (long long)_e, (long long)_a);                  \
            (void)printf("    at %s:%d\n", __FILE__, __LINE__); \
            return;                                             \
        }                                                       \
    } while(0)

#define TEST_ASSERT_EQ_REAL(expected, actual)                     \
    do {                                                        \
        woort_Real _e = (expected);                              \
        woort_Real _a = (actual);                                \
        if (_e != _a) {                                         \
            (void)printf("FAIL\n");                             \
            (void)printf("    expected: %f, actual: %f\n",  \
                (double)_e, (double)_a);                        \
            (void)printf("    at %s:%d\n", __FILE__, __LINE__); \
            return;                                             \
        }                                                       \
    } while(0)

/* ========== Helpers ========== */

static woort_Int g_captured_int = 0;

static woort_api capture_int_fn(void)
{
    g_captured_int = woort_int(0);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

/* ========== Test 1: Loop with conditional break ========== */
/*
  acc = 0; i = 1;
  while (i <= 10) {
      acc += i;
      if (acc >= 15) break;
      i++;
  }
  return acc;  // 1+2+3+4+5 = 15
*/
static void test_loop_with_break(void)
{
    TEST_BEGIN("loop_with_break (sum until >=15)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c15 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        const woort_IRValue* v10 = woort_IRFunction_fetch_const(f, c10);
        const woort_IRValue* v15 = woort_IRFunction_fetch_const(f, c15);
        woort_IRValue* acc = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v0 && v1 && v10 && v15 && acc && i);

        woort_IRLabel* L_header = woort_IRFunction_new_label(f);
        woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
        woort_IRLabel* L_break = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_header && L_exit && L_break);

        (void)woort_IR_MOV(f, acc, v0);
        (void)woort_IR_MOV(f, i, v1);

        (void)woort_IR_bind(f, L_header);
        (void)woort_IR_jcc_gt(f, i, v10, L_exit);
        (void)woort_IR_ADDI(f, acc, acc, i);
        (void)woort_IR_jcc_ge(f, acc, v15, L_break);
        (void)woort_IR_ADDI(f, i, i, v1);
        (void)woort_IR_jmp(f, L_header);

        (void)woort_IR_bind(f, L_break);
        (void)woort_IR_bind(f, L_exit);
        (void)woort_IR_ret(f, acc);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c15, 15);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(15, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 2: Diamond if-else control flow ========== */
/*
  x = 3;
  if (x > 2) { x = x * x; }   // 9
  else        { x = x + 1; }
  return x + 1;  // 10
*/
static void test_diamond_if_else(void)
{
    TEST_BEGIN("diamond_if_else (3>2 -> 3*3+1=10)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        woort_IRValue* x = woort_IRFunction_new_vreg(f);
        woort_IRValue* tmp = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(x && tmp && result);

        const woort_IRValue* v3 = woort_IRFunction_fetch_const(f, c3);
        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f, c2);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        (void)woort_IR_MOV(f, x, v3);

        woort_IRLabel* L_then = woort_IRFunction_new_label(f);
        woort_IRLabel* L_else = woort_IRFunction_new_label(f);
        woort_IRLabel* L_join = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_then && L_else && L_join);

        (void)woort_IR_jcc_gt(f, x, v2, L_then);

        /* else: x = x + 1 */
        (void)woort_IR_ADDI(f, tmp, x, v1);
        (void)woort_IR_MOV(f, x, tmp);
        (void)woort_IR_jmp(f, L_join);

        /* then: x = x * x */
        (void)woort_IR_bind(f, L_then);
        (void)woort_IR_MULI(f, tmp, x, x);
        (void)woort_IR_MOV(f, x, tmp);

        /* join: return x + 1 */
        (void)woort_IR_bind(f, L_join);
        (void)woort_IR_ADDI(f, result, x, v1);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c3, 3);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(10, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 3: Switch-like dispatch via chained jcc_eq ========== */
/*
  x = 3;
  switch (x):
    1 -> 10, 2 -> 20, 3 -> 30, 4 -> 40, default -> 99
  result = 30
*/
static void test_switch_chain(void)
{
    TEST_BEGIN("switch_chain (x=3 -> 30)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cx = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c4 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c30 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c40 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c99 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* x = woort_IRFunction_fetch_const(f, cx);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f, c2);
        const woort_IRValue* v3 = woort_IRFunction_fetch_const(f, c3);
        const woort_IRValue* v4 = woort_IRFunction_fetch_const(f, c4);
        const woort_IRValue* r10 = woort_IRFunction_fetch_const(f, c10);
        const woort_IRValue* r20 = woort_IRFunction_fetch_const(f, c20);
        const woort_IRValue* r30 = woort_IRFunction_fetch_const(f, c30);
        const woort_IRValue* r40 = woort_IRFunction_fetch_const(f, c40);
        const woort_IRValue* r99 = woort_IRFunction_fetch_const(f, c99);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(x && v1 && v2 && v3 && v4);
        TEST_ASSERT(r10 && r20 && r30 && r40 && r99 && result);

        woort_IRLabel* L_case1 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_case2 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_case3 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_case4 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_default = woort_IRFunction_new_label(f);
        woort_IRLabel* L_end = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_case1 && L_case2 && L_case3 && L_case4 && L_default && L_end);

        (void)woort_IR_jcc_eq(f, x, v1, L_case1);
        (void)woort_IR_jcc_eq(f, x, v2, L_case2);
        (void)woort_IR_jcc_eq(f, x, v3, L_case3);
        (void)woort_IR_jcc_eq(f, x, v4, L_case4);
        (void)woort_IR_jmp(f, L_default);

        (void)woort_IR_bind(f, L_case1);
        (void)woort_IR_MOV(f, result, r10);
        (void)woort_IR_jmp(f, L_end);

        (void)woort_IR_bind(f, L_case2);
        (void)woort_IR_MOV(f, result, r20);
        (void)woort_IR_jmp(f, L_end);

        (void)woort_IR_bind(f, L_case3);
        (void)woort_IR_MOV(f, result, r30);
        (void)woort_IR_jmp(f, L_end);

        (void)woort_IR_bind(f, L_case4);
        (void)woort_IR_MOV(f, result, r40);
        (void)woort_IR_jmp(f, L_end);

        (void)woort_IR_bind(f, L_default);
        (void)woort_IR_MOV(f, result, r99);

        (void)woort_IR_bind(f, L_end);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, cx, 3);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_int(cenv, c3, 3);
    woort_CodeEnv_set_const_int(cenv, c4, 4);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c20, 20);
    woort_CodeEnv_set_const_int(cenv, c30, 30);
    woort_CodeEnv_set_const_int(cenv, c40, 40);
    woort_CodeEnv_set_const_int(cenv, c99, 99);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(30, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 4: Search loop with early exit ========== */
/*
  Find first i in [1..20] where i % 7 == 0.
  result = 7
*/
static void test_search_loop(void)
{
    TEST_BEGIN("search_loop (first div by 7 in 1..20)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c7 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_neg1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        const woort_IRValue* v7 = woort_IRFunction_fetch_const(f, c7);
        const woort_IRValue* v20 = woort_IRFunction_fetch_const(f, c20);
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        const woort_IRValue* v_neg1 = woort_IRFunction_fetch_const(f, c_neg1);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* mod = woort_IRFunction_new_vreg(f);
        woort_IRValue* found = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v1 && v7 && v20 && v0 && v_neg1 && i && mod && found);

        woort_IRLabel* L_header = woort_IRFunction_new_label(f);
        woort_IRLabel* L_found = woort_IRFunction_new_label(f);
        woort_IRLabel* L_notfound = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_header && L_found && L_notfound);

        (void)woort_IR_MOV(f, i, v1);
        (void)woort_IR_MOV(f, found, v_neg1);

        (void)woort_IR_bind(f, L_header);
        (void)woort_IR_jcc_gt(f, i, v20, L_notfound);
        (void)woort_IR_MODI(f, mod, i, v7);
        (void)woort_IR_jcc_eq(f, mod, v0, L_found);
        (void)woort_IR_ADDI(f, i, i, v1);
        (void)woort_IR_jmp(f, L_header);

        (void)woort_IR_bind(f, L_found);
        (void)woort_IR_MOV(f, found, i);

        (void)woort_IR_bind(f, L_notfound);
        (void)woort_IR_ret(f, found);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c7, 7);
    woort_CodeEnv_set_const_int(cenv, c20, 20);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_int(cenv, c_neg1, -1);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(7, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 5: Nested if-else inside a loop ========== */
/*
  sum = 0
  for i in 0..5:
    if (i % 2 == 0) sum += i
    else            sum -= i
  i: 0  1  2  3  4  5
  s: 0 -1  1 -2  2 -3
  return sum = -3
*/
static void test_nested_if_in_loop(void)
{
    TEST_BEGIN("nested_if_in_loop (even+/odd- 0..5 = -3)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c6 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        const woort_IRValue* v6 = woort_IRFunction_fetch_const(f, c6);
        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f, c2);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* mod = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v0 && v1 && v6 && v2 && sum && i && mod);

        woort_IRLabel* L_header = woort_IRFunction_new_label(f);
        woort_IRLabel* L_even = woort_IRFunction_new_label(f);
        woort_IRLabel* L_next = woort_IRFunction_new_label(f);
        woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_header && L_even && L_next && L_exit);

        (void)woort_IR_MOV(f, sum, v0);
        (void)woort_IR_MOV(f, i, v0);

        (void)woort_IR_bind(f, L_header);
        (void)woort_IR_jcc_ge(f, i, v6, L_exit);

        (void)woort_IR_MODI(f, mod, i, v2);
        (void)woort_IR_jcc_eq(f, mod, v0, L_even);

        /* odd: sum -= i */
        (void)woort_IR_SUBI(f, sum, sum, i);
        (void)woort_IR_jmp(f, L_next);

        /* even: sum += i */
        (void)woort_IR_bind(f, L_even);
        (void)woort_IR_ADDI(f, sum, sum, i);

        (void)woort_IR_bind(f, L_next);
        (void)woort_IR_ADDI(f, i, i, v1);
        (void)woort_IR_jmp(f, L_header);

        (void)woort_IR_bind(f, L_exit);
        (void)woort_IR_ret(f, sum);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c6, 6);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(-3, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 6: ITOR loop with RTOI roundtrip ========== */
/*
  acc_r = 0.0
  for i in 0..4:
    acc_r += ITOR(i)
  return RTOI(acc_r)  // 0.0+1.0+2.0+3.0+4.0 = 10.0 -> 10
*/
static void test_itor_loop_roundtrip(void)
{
    TEST_BEGIN("itor_loop_roundtrip (ITOR+ADDR loop, RTOI=10)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_r0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* vr0 = woort_IRFunction_fetch_const(f, c_r0);
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        const woort_IRValue* v5 = woort_IRFunction_fetch_const(f, c5);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        woort_IRValue* acc = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* i_real = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(vr0 && v0 && v5 && v1 && acc && i && i_real && result);

        woort_IRLabel* L_header = woort_IRFunction_new_label(f);
        woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_header && L_exit);

        (void)woort_IR_MOV(f, acc, vr0);
        (void)woort_IR_MOV(f, i, v0);

        (void)woort_IR_bind(f, L_header);
        (void)woort_IR_jcc_ge(f, i, v5, L_exit);
        (void)woort_IR_ITOR(f, i_real, i);
        (void)woort_IR_ADDR(f, acc, acc, i_real);
        (void)woort_IR_ADDI(f, i, i, v1);
        (void)woort_IR_jmp(f, L_header);

        (void)woort_IR_bind(f, L_exit);
        (void)woort_IR_RTOI(f, result, acc);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_real(cenv, c_r0, 0.0);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_int(cenv, c5, 5);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(10, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 7: Mixed type arithmetic (ITOR+DIVR+RTOI) ========== */
/*
  a = 10; b = 3;
  div = RTOI(DIVR(ITOR(a), ITOR(b)))  // (10/3) truncated = 3
  return div * b  // 3 * 3 = 9
*/
static void test_mixed_type_arithmetic(void)
{
    TEST_BEGIN("mixed_type_arithmetic (ITOR/DIVR/RTOI -> 9)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* a = woort_IRFunction_fetch_const(f, c10);
        const woort_IRValue* b = woort_IRFunction_fetch_const(f, c3);
        woort_IRValue* a_r = woort_IRFunction_new_vreg(f);
        woort_IRValue* b_r = woort_IRFunction_new_vreg(f);
        woort_IRValue* div_r = woort_IRFunction_new_vreg(f);
        woort_IRValue* div_i = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(a && b && a_r && b_r && div_r && div_i && result);

        (void)woort_IR_ITOR(f, a_r, a);
        (void)woort_IR_ITOR(f, b_r, b);
        (void)woort_IR_DIVR(f, div_r, a_r, b_r);
        (void)woort_IR_RTOI(f, div_i, div_r);
        (void)woort_IR_MULI(f, result, div_i, b);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c3, 3);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(9, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 8: MKVEC IR instruction ========== */
/*
  Create an empty vector with MKVEC(0), return success code 42.
*/
static void test_ir_mkvec(void)
{
    TEST_BEGIN("ir_mkvec (0 elements, returns 42)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_ok = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v_ok = woort_IRFunction_fetch_const(f, c_ok);
        woort_IRValue* vec = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v_ok && vec);

        (void)woort_IR_MKVEC(f, vec, 0);
        (void)woort_IR_ret(f, v_ok);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c_ok, 42);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 9: MKMAP IR instruction with multiple entries ========== */
/*
  Push 2 key-value pairs, create map, return success code 42.
*/
static void test_ir_mkmap_multi(void)
{
    TEST_BEGIN("ir_mkmap_multi (2 kv-pairs, returns 42)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_k1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_v1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_k2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_v2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_ok = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* k1 = woort_IRFunction_fetch_const(f, c_k1);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c_v1);
        const woort_IRValue* k2 = woort_IRFunction_fetch_const(f, c_k2);
        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f, c_v2);
        const woort_IRValue* ok = woort_IRFunction_fetch_const(f, c_ok);
        woort_IRValue* m = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(k1 && v1 && k2 && v2 && ok && m);

        (void)woort_IR_PUSHCHK(f, k1);
        (void)woort_IR_PUSHCHK(f, v1);
        (void)woort_IR_PUSHCHK(f, k2);
        (void)woort_IR_PUSHCHK(f, v2);
        (void)woort_IR_MKMAP(f, m, 2);
        (void)woort_IR_ret(f, ok);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_string(cenv, c_k1, "alpha");
    woort_CodeEnv_set_const_box_int(cenv, c_v1, 100);
    woort_CodeEnv_set_const_string(cenv, c_k2, "beta");
    woort_CodeEnv_set_const_box_int(cenv, c_v2, 200);
    woort_CodeEnv_set_const_int(cenv, c_ok, 42);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 10: BOXDYN + UNBOXDYN roundtrip ========== */
/*
  val = 42
  boxed = BOXDYN(INT, val)
  unboxed = UNBOXDYN(INT, boxed)
  return unboxed  // 42
*/
static void test_ir_boxdyn_unboxdyn(void)
{
    TEST_BEGIN("ir_boxdyn_unboxdyn (box 42, unbox -> 42)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* val = woort_IRFunction_fetch_const(f, c42);
        woort_IRValue* boxed = woort_IRFunction_new_vreg(f);
        woort_IRValue* unboxed = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(val && boxed && unboxed);

        (void)woort_IR_BOXDYN(f, boxed, WOORT_BOX_VALUE_TYPE_INT, val);
        (void)woort_IR_UNBOXDYN(f, unboxed, WOORT_BOX_VALUE_TYPE_INT, boxed);
        (void)woort_IR_ret(f, unboxed);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c42, 42);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 11: Three-function call chain ========== */
/*
  A(x) = x + 1
  B(x) = A(x) + 10
  main() = B(5) = A(5) + 10 = 16
*/
static void test_three_function_chain(void)
{
    TEST_BEGIN("three_function_chain (A->B->main = 16)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cA = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cB = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    /* A(x) = x + 1 */
    woort_IRFunction* f_A;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, 0, &f_A));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_A, 0);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f_A, c1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_A);
        TEST_ASSERT(x && v1 && r);
        (void)woort_IR_ADDI(f_A, r, x, v1);
        (void)woort_IR_ret(f_A, r);
    }

    /* B(x) = A(x) + 10 */
    woort_IRFunction* f_B;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, 0, &f_B));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_B, 0);
        const woort_IRValue* v10 = woort_IRFunction_fetch_const(f_B, c10);
        woort_IRValue* rA = woort_IRFunction_new_vreg(f_B);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_B);
        TEST_ASSERT(x && v10 && rA && r);
        (void)woort_IR_PUSHCHK(f_B, x);
        (void)woort_IR_CALLNWO(f_B, cA, 1, rA);
        (void)woort_IR_ADDI(f_B, r, rA, v10);
        (void)woort_IR_ret(f_B, r);
    }

    /* main() = B(5) */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f_main));
    {
        const woort_IRValue* v5 = woort_IRFunction_fetch_const(f_main, c5);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(v5 && r);
        (void)woort_IR_PUSHCHK(f_main, v5);
        (void)woort_IR_CALLNWO(f_main, cB, 1, r);
        (void)woort_IR_ret(f_main, r);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr_A;
    const woort_Bytecode* addr_B;
    const woort_Bytecode* addr_main;
    woort_CodeEnv_query_function(cenv, f_A, &addr_A);
    woort_CodeEnv_query_function(cenv, f_B, &addr_B);
    woort_CodeEnv_query_function(cenv, f_main, &addr_main);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c5, 5);
    woort_CodeEnv_set_const_script_function(cenv, cA, addr_A);
    woort_CodeEnv_set_const_script_function(cenv, cB, addr_B);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr_main);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(16, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 12: Function with 5 parameters ========== */
/*
  f(a,b,c,d,e) = a + b*2 + c*3 + d*4 + e*5
  main() = f(1,2,3,4,5) = 1+4+9+16+25 = 55
*/
static void test_function_5_params(void)
{
    TEST_BEGIN("function_5_params (1+4+9+16+25=55)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c4 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cv1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cv2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cv3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cv4 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cv5 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    /* f(a,b,c,d,e) */
    woort_IRFunction* f_calc;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 5, 0, &f_calc));
    {
        woort_IRValue* a = woort_IRFunction_get_argument(f_calc, 0);
        woort_IRValue* b = woort_IRFunction_get_argument(f_calc, 1);
        woort_IRValue* c = woort_IRFunction_get_argument(f_calc, 2);
        woort_IRValue* d = woort_IRFunction_get_argument(f_calc, 3);
        woort_IRValue* e = woort_IRFunction_get_argument(f_calc, 4);
        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f_calc, c2);
        const woort_IRValue* v3 = woort_IRFunction_fetch_const(f_calc, c3);
        const woort_IRValue* v4 = woort_IRFunction_fetch_const(f_calc, c4);
        const woort_IRValue* v5 = woort_IRFunction_fetch_const(f_calc, c5);
        woort_IRValue* t1 = woort_IRFunction_new_vreg(f_calc);
        woort_IRValue* t2 = woort_IRFunction_new_vreg(f_calc);
        woort_IRValue* t3 = woort_IRFunction_new_vreg(f_calc);
        woort_IRValue* t4 = woort_IRFunction_new_vreg(f_calc);
        woort_IRValue* s1 = woort_IRFunction_new_vreg(f_calc);
        woort_IRValue* s2 = woort_IRFunction_new_vreg(f_calc);
        woort_IRValue* s3 = woort_IRFunction_new_vreg(f_calc);
        woort_IRValue* s4 = woort_IRFunction_new_vreg(f_calc);
        TEST_ASSERT(a && b && c && d && e && v2 && v3 && v4 && v5);
        TEST_ASSERT(t1 && t2 && t3 && t4 && s1 && s2 && s3 && s4);

        (void)woort_IR_MULI(f_calc, t1, b, v2);
        (void)woort_IR_MULI(f_calc, t2, c, v3);
        (void)woort_IR_MULI(f_calc, t3, d, v4);
        (void)woort_IR_MULI(f_calc, t4, e, v5);

        (void)woort_IR_ADDI(f_calc, s1, a, t1);
        (void)woort_IR_ADDI(f_calc, s2, s1, t2);
        (void)woort_IR_ADDI(f_calc, s3, s2, t3);
        (void)woort_IR_ADDI(f_calc, s4, s3, t4);
        (void)woort_IR_ret(f_calc, s4);
    }

    /* main() */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f_main));
    {
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f_main, cv1);
        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f_main, cv2);
        const woort_IRValue* v3 = woort_IRFunction_fetch_const(f_main, cv3);
        const woort_IRValue* v4 = woort_IRFunction_fetch_const(f_main, cv4);
        const woort_IRValue* v5 = woort_IRFunction_fetch_const(f_main, cv5);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(v1 && v2 && v3 && v4 && v5 && r);

        (void)woort_IR_PUSHCHK(f_main, v5);
        (void)woort_IR_PUSHCHK(f_main, v4);
        (void)woort_IR_PUSHCHK(f_main, v3);
        (void)woort_IR_PUSHCHK(f_main, v2);
        (void)woort_IR_PUSHCHK(f_main, v1);
        (void)woort_IR_CALLNWO(f_main, cfn, 5, r);
        (void)woort_IR_ret(f_main, r);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr_calc;
    const woort_Bytecode* addr_main;
    woort_CodeEnv_query_function(cenv, f_calc, &addr_calc);
    woort_CodeEnv_query_function(cenv, f_main, &addr_main);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_int(cenv, c3, 3);
    woort_CodeEnv_set_const_int(cenv, c4, 4);
    woort_CodeEnv_set_const_int(cenv, c5, 5);
    woort_CodeEnv_set_const_int(cenv, cv1, 1);
    woort_CodeEnv_set_const_int(cenv, cv2, 2);
    woort_CodeEnv_set_const_int(cenv, cv3, 3);
    woort_CodeEnv_set_const_int(cenv, cv4, 4);
    woort_CodeEnv_set_const_int(cenv, cv5, 5);
    woort_CodeEnv_set_const_script_function(cenv, cfn, addr_calc);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr_main);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(55, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 13: POPR to discard stack items ========== */
/*
  Push 3 values, then POPR(3) to discard them, return success code 42.
  Verifies POPR compiles and runs correctly.
*/
static void test_popr_cleanup(void)
{
    TEST_BEGIN("popr_cleanup (push 3, popr 3, return 42)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c30 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_ok = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v10 = woort_IRFunction_fetch_const(f, c10);
        const woort_IRValue* v20 = woort_IRFunction_fetch_const(f, c20);
        const woort_IRValue* v30 = woort_IRFunction_fetch_const(f, c30);
        const woort_IRValue* v_ok = woort_IRFunction_fetch_const(f, c_ok);
        TEST_ASSERT(v10 && v20 && v30 && v_ok);

        (void)woort_IR_PUSHCHK(f, v10);
        (void)woort_IR_PUSHCHK(f, v20);
        (void)woort_IR_PUSHCHK(f, v30);
        (void)woort_IR_POPR(f, 3);
        (void)woort_IR_ret(f, v_ok);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c20, 20);
    woort_CodeEnv_set_const_int(cenv, c30, 30);
    woort_CodeEnv_set_const_int(cenv, c_ok, 42);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 14: Many distinct constants (register pressure) ========== */
/*
  Sum 10 distinct integer constants: 1+2+3+...+10 = 55
*/
static void test_many_constants(void)
{
    TEST_BEGIN("many_constants (sum 1..10 = 55)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex consts[10];
    for (int i = 0; i < 10; i++)
        consts[i] = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* vals[10];
        for (int i = 0; i < 10; i++) {
            vals[i] = woort_IRFunction_fetch_const(f, consts[i]);
            TEST_ASSERT(vals[i] != NULL);
        }
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        woort_IRValue* tmp = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(sum && tmp);

        (void)woort_IR_ADDI(f, sum, vals[0], vals[1]);
        for (int i = 2; i < 10; i++) {
            (void)woort_IR_ADDI(f, tmp, sum, vals[i]);
            (void)woort_IR_MOV(f, sum, tmp);
        }
        (void)woort_IR_ret(f, sum);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    for (int i = 0; i < 10; i++)
        woort_CodeEnv_set_const_int(cenv, consts[i], i + 1);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(55, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 15: Different constants in branches ========== */
/*
  x = 5; three = 3;
  if (x > 3) result = 100
  else        result = 200
  return result  // 100
*/
static void test_branch_different_constants(void)
{
    TEST_BEGIN("branch_different_constants (5>3 -> 100)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cx = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c100 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c200 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* x = woort_IRFunction_fetch_const(f, cx);
        const woort_IRValue* v3 = woort_IRFunction_fetch_const(f, c3);
        const woort_IRValue* v100 = woort_IRFunction_fetch_const(f, c100);
        const woort_IRValue* v200 = woort_IRFunction_fetch_const(f, c200);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(x && v3 && v100 && v200 && result);

        woort_IRLabel* L_then = woort_IRFunction_new_label(f);
        woort_IRLabel* L_end = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_then && L_end);

        (void)woort_IR_jcc_gt(f, x, v3, L_then);
        (void)woort_IR_MOV(f, result, v200);
        (void)woort_IR_jmp(f, L_end);

        (void)woort_IR_bind(f, L_then);
        (void)woort_IR_MOV(f, result, v100);

        (void)woort_IR_bind(f, L_end);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, cx, 5);
    woort_CodeEnv_set_const_int(cenv, c3, 3);
    woort_CodeEnv_set_const_int(cenv, c100, 100);
    woort_CodeEnv_set_const_int(cenv, c200, 200);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(100, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 16: Multiple function calls in sequence ========== */
/*
  add1(x) = x + 1
  main():
    r1 = add1(10)  // 11
    r2 = add1(r1)  // 12
    r3 = add1(r2)  // 13
    return r1 + r2 + r3  // 11+12+13 = 36
*/
static void test_multi_call_sequence(void)
{
    TEST_BEGIN("multi_call_sequence (add1 x3 = 36)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    /* add1(x) */
    woort_IRFunction* f_add1;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, 0, &f_add1));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_add1, 0);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f_add1, c1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_add1);
        TEST_ASSERT(x && v1 && r);
        (void)woort_IR_ADDI(f_add1, r, x, v1);
        (void)woort_IR_ret(f_add1, r);
    }

    /* main() */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f_main));
    {
        const woort_IRValue* v10 = woort_IRFunction_fetch_const(f_main, c10);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* r3 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* s1 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* s2 = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(v10 && r1 && r2 && r3 && s1 && s2);

        (void)woort_IR_PUSHCHK(f_main, v10);
        (void)woort_IR_CALLNWO(f_main, cfn, 1, r1);

        (void)woort_IR_PUSHCHK(f_main, r1);
        (void)woort_IR_CALLNWO(f_main, cfn, 1, r2);

        (void)woort_IR_PUSHCHK(f_main, r2);
        (void)woort_IR_CALLNWO(f_main, cfn, 1, r3);

        (void)woort_IR_ADDI(f_main, s1, r1, r2);
        (void)woort_IR_ADDI(f_main, s2, s1, r3);
        (void)woort_IR_ret(f_main, s2);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr_add1;
    const woort_Bytecode* addr_main;
    woort_CodeEnv_query_function(cenv, f_add1, &addr_add1);
    woort_CodeEnv_query_function(cenv, f_main, &addr_main);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_script_function(cenv, cfn, addr_add1);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr_main);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(36, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 17: Accumulator loop with compound ops ========== */
/*
  sum = 0
  for i = 1..10: sum += i*2
  sum = 1+4+6+8+10+12+14+16+18+20 = 110
*/
static void test_accumulator_loop(void)
{
    TEST_BEGIN("accumulator_loop (sum i*2, i=1..10 = 110)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        const woort_IRValue* v2 = woort_IRFunction_fetch_const(f, c2);
        const woort_IRValue* v10 = woort_IRFunction_fetch_const(f, c10);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* prod = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v0 && v1 && v2 && v10 && sum && i && prod);

        woort_IRLabel* L_header = woort_IRFunction_new_label(f);
        woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_header && L_exit);

        (void)woort_IR_MOV(f, sum, v0);
        (void)woort_IR_MOV(f, i, v1);

        (void)woort_IR_bind(f, L_header);
        (void)woort_IR_jcc_gt(f, i, v10, L_exit);
        (void)woort_IR_MULI(f, prod, i, v2);
        (void)woort_IR_ADDI(f, sum, sum, prod);
        (void)woort_IR_ADDI(f, i, i, v1);
        (void)woort_IR_jmp(f, L_header);

        (void)woort_IR_bind(f, L_exit);
        (void)woort_IR_ret(f, sum);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(110, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 18: Vector C API full lifecycle ========== */
static void test_vector_c_api_detailed(void)
{
    TEST_BEGIN("vector_c_api_detailed (push/get/set/resize/clear)");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(5, &sv));

    woort_set_vec(sv);
    TEST_ASSERT(woort_vec_len(sv) == 0);

    woort_set_box_int(sv + 1, 10);
    woort_set_box_int(sv + 2, 20);
    woort_set_box_int(sv + 3, 30);
    (void)woort_vec_push(sv, sv + 1);
    (void)woort_vec_push(sv, sv + 2);
    (void)woort_vec_push(sv, sv + 3);
    TEST_ASSERT(woort_vec_len(sv) == 3);

    (void)woort_vec_get(sv + 4, sv, 1);
    TEST_ASSERT(woort_unbox_int(sv + 4) == 20);

    woort_set_box_int(sv + 4, 99);
    (void)woort_vec_set(sv, 1, sv + 4);
    (void)woort_vec_get(sv + 4, sv, 1);
    TEST_ASSERT(woort_unbox_int(sv + 4) == 99);

    (void)woort_vec_pop(sv);
    TEST_ASSERT(woort_vec_len(sv) == 2);

    (void)woort_vec_resize(sv, 5);
    TEST_ASSERT(woort_vec_len(sv) == 5);

    (void)woort_vec_clear(sv);
    TEST_ASSERT(woort_vec_len(sv) == 0);

    woort_pop(5);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== Test 19: Map C API with int keys ========== */
static void test_map_c_api_int_keys(void)
{
    TEST_BEGIN("map_c_api_int_keys (set/get/contains/erase)");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(3, &sv));

    woort_set_map(sv);
    TEST_ASSERT(woort_map_len(sv) == 0);

    woort_set_box_int(sv + 1, 100);
    TEST_ASSERT(woort_map_set_by_int(sv, 1, sv + 1));
    woort_set_box_int(sv + 2, 200);
    TEST_ASSERT(woort_map_set_by_int(sv, 2, sv + 2));
    TEST_ASSERT(woort_map_len(sv) == 2);

    TEST_ASSERT(woort_map_contains_int(sv, 1));
    TEST_ASSERT(woort_map_contains_int(sv, 2));
    TEST_ASSERT(!woort_map_contains_int(sv, 3));

    TEST_ASSERT(woort_map_get_by_int(sv + 1, sv, 2));
    TEST_ASSERT_EQ_INT(200, woort_unbox_int(sv + 1));

    TEST_ASSERT(woort_map_erase_by_int(sv, 1));
    TEST_ASSERT(!woort_map_contains_int(sv, 1));
    TEST_ASSERT(woort_map_len(sv) == 1);

    woort_pop(3);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== Test 20: String comparison in if-else ========== */
/*
  s1 = "hello"; s2 = "world"
  if (s1 == s2) return 1 else return 0
  EQS returns 0 (not equal), so we go to else -> 0
*/
static void test_string_in_if_else(void)
{
    TEST_BEGIN("string_in_if_else (hello==world -> 0)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_s1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_s2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* s1 = woort_IRFunction_fetch_const(f, c_s1);
        const woort_IRValue* s2 = woort_IRFunction_fetch_const(f, c_s2);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        woort_IRValue* cmp = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(s1 && s2 && v1 && v0 && cmp && result);

        woort_IRLabel* L_then = woort_IRFunction_new_label(f);
        woort_IRLabel* L_end = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_then && L_end);

        (void)woort_IR_EQS(f, cmp, s1, s2);
        (void)woort_IR_jcc(f, cmp, L_then);
        (void)woort_IR_MOV(f, result, v0);
        (void)woort_IR_jmp(f, L_end);

        (void)woort_IR_bind(f, L_then);
        (void)woort_IR_MOV(f, result, v1);

        (void)woort_IR_bind(f, L_end);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_string(cenv, c_s1, "hello");
    woort_CodeEnv_set_const_string(cenv, c_s2, "world");
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(0, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 21: Deeply nested conditionals ========== */
/*
  x = 15;
  if (x > 10) {
    if (x > 20) return 1;
    else return 2;   // x=15 > 10 but not > 20
  } else {
    if (x > 5) return 3;
    else return 4;
  }
  result = 2
*/
static void test_deeply_nested_if(void)
{
    TEST_BEGIN("deeply_nested_if (15>10, 15<=20 -> 2)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cx = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cr1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cr2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cr3 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cr4 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* x = woort_IRFunction_fetch_const(f, cx);
        const woort_IRValue* v10 = woort_IRFunction_fetch_const(f, c10);
        const woort_IRValue* v20 = woort_IRFunction_fetch_const(f, c20);
        const woort_IRValue* v5 = woort_IRFunction_fetch_const(f, c5);
        const woort_IRValue* r1 = woort_IRFunction_fetch_const(f, cr1);
        const woort_IRValue* r2 = woort_IRFunction_fetch_const(f, cr2);
        const woort_IRValue* r3 = woort_IRFunction_fetch_const(f, cr3);
        const woort_IRValue* r4 = woort_IRFunction_fetch_const(f, cr4);
        TEST_ASSERT(x && v10 && v20 && v5 && r1 && r2 && r3 && r4);

        woort_IRLabel* L_outer_then = woort_IRFunction_new_label(f);
        woort_IRLabel* L_outer_else = woort_IRFunction_new_label(f);
        woort_IRLabel* L_inner_then1 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_inner_else1 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_inner_then2 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_inner_else2 = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_outer_then && L_outer_else);
        TEST_ASSERT(L_inner_then1 && L_inner_else1);
        TEST_ASSERT(L_inner_then2 && L_inner_else2);

        (void)woort_IR_jcc_gt(f, x, v10, L_outer_then);

        /* outer else: x <= 10 */
        (void)woort_IR_jcc_gt(f, x, v5, L_inner_then2);
        (void)woort_IR_ret(f, r4);
        (void)woort_IR_bind(f, L_inner_then2);
        (void)woort_IR_ret(f, r3);

        /* outer then: x > 10 */
        (void)woort_IR_bind(f, L_outer_then);
        (void)woort_IR_jcc_gt(f, x, v20, L_inner_then1);
        (void)woort_IR_ret(f, r2);
        (void)woort_IR_bind(f, L_inner_then1);
        (void)woort_IR_ret(f, r1);

        (void)woort_IR_bind(f, L_outer_else);
        (void)woort_IR_bind(f, L_inner_else1);
        (void)woort_IR_bind(f, L_inner_else2);
        (void)woort_IR_ret(f, r4);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, cx, 15);
    woort_CodeEnv_set_const_int(cenv, c10, 10);
    woort_CodeEnv_set_const_int(cenv, c20, 20);
    woort_CodeEnv_set_const_int(cenv, c5, 5);
    woort_CodeEnv_set_const_int(cenv, cr1, 1);
    woort_CodeEnv_set_const_int(cenv, cr2, 2);
    woort_CodeEnv_set_const_int(cenv, cr3, 3);
    woort_CodeEnv_set_const_int(cenv, cr4, 4);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(2, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 22: Real arithmetic loop ========== */
/*
  acc = 1.0
  for i in 0..4:
    acc = acc * 2.0
  return RTOI(acc)  // 1.0 * 2^5 = 32.0 -> 32
*/
static void test_real_arithmetic_loop(void)
{
    TEST_BEGIN("real_arithmetic_loop (1.0 * 2^5 -> RTOI=32)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_r1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_r2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* r1 = woort_IRFunction_fetch_const(f, c_r1);
        const woort_IRValue* r2 = woort_IRFunction_fetch_const(f, c_r2);
        const woort_IRValue* v0 = woort_IRFunction_fetch_const(f, c0);
        const woort_IRValue* v5 = woort_IRFunction_fetch_const(f, c5);
        const woort_IRValue* v1 = woort_IRFunction_fetch_const(f, c1);
        woort_IRValue* acc = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(r1 && r2 && v0 && v5 && v1 && acc && i && result);

        woort_IRLabel* L_header = woort_IRFunction_new_label(f);
        woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_header && L_exit);

        (void)woort_IR_MOV(f, acc, r1);
        (void)woort_IR_MOV(f, i, v0);

        (void)woort_IR_bind(f, L_header);
        (void)woort_IR_jcc_ge(f, i, v5, L_exit);
        (void)woort_IR_MULR(f, acc, acc, r2);
        (void)woort_IR_ADDI(f, i, i, v1);
        (void)woort_IR_jmp(f, L_header);

        (void)woort_IR_bind(f, L_exit);
        (void)woort_IR_RTOI(f, result, acc);
        (void)woort_IR_ret(f, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    woort_CodeEnv_query_function(cenv, f, &addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_real(cenv, c_r1, 1.0);
    woort_CodeEnv_set_const_real(cenv, c_r2, 2.0);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_set_const_int(cenv, c5, 5);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr);
    woort_CodeEnv_unlock(cenv);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(32, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Main ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    (void)printf("\n=== Advanced IR Tests ===\n\n");

    test_loop_with_break();
    test_diamond_if_else();
    test_switch_chain();
    test_search_loop();
    test_nested_if_in_loop();
    test_itor_loop_roundtrip();
    test_mixed_type_arithmetic();
    test_ir_mkvec();
    test_ir_mkmap_multi();
    test_ir_boxdyn_unboxdyn();
    test_three_function_chain();
    test_function_5_params();
    test_popr_cleanup();
    test_many_constants();
    test_branch_different_constants();
    test_multi_call_sequence();
    test_accumulator_loop();
    test_vector_c_api_detailed();
    test_map_c_api_int_keys();
    test_string_in_if_else();
    test_deeply_nested_if();
    test_real_arithmetic_loop();

    (void)printf("\n  %d/%d tests passed.\n\n", g_tests_passed, g_tests_run);

    woort_shutdown();
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
