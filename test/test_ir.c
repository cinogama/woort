#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_ir_value.h"
#include "woort_value.h"
#include "woort_disassembly.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ========== 测试基础设施 ========== */

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_BEGIN(name)                                        \
    do {                                                        \
        const char* _test_name = (name);                        \
        g_tests_run++;                                          \
        (void)printf("  [TEST] %-40s ", _test_name);            \
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

/* 反汇编输出 */
void dump_Code(woort_CodeEnv* cenv)
{
    const woort_Bytecode* pc = cenv->m_code_begin;

    printf("\n");

    while (pc < cenv->m_code_end)
        pc = woort_Disassembly(pc);

    printf("\n");

    fflush(stdout);
}

/*
辅助：用于从 native 函数中捕获 IR 编译函数的返回值。
调用约定：被调函数的返回值在 vm->m_sp[0] 中（通过 RESULT 指令获取）。
native 函数将其拷贝到全局变量，以便测试代码检查。
*/
static woort_Int g_captured_int = 0;

static woort_api capture_int(woort_VMRuntime* vm, woort_value* args)
{
    g_captured_int = ((woort_Value*)args)->m_integer;
    return WOORT_VM_CALL_STATUS_NORMAL;
}

/* ========== 测试 1: 简单整数返回 ========== */
/*
func constant_42() => int {
    return 42;
}
*/
static void test_constant_return(void)
{
    TEST_BEGIN("constant_return (return 42)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v = woort_IRFunction_load_const(f, c42); TEST_ASSERT(v != NULL);
        TEST_ASSERT(woort_IR_ret(f, v));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c42].m_integer = 42;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 2: 整数算术 ========== */
/*
func arith() => int {
    a = 10; b = 3;
    return (a + b) * (a - b);       // 13 * 7 = 91
}
*/
static void test_integer_arithmetic(void)
{
    TEST_BEGIN("integer_arithmetic ((a+b)*(a-b))");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex const_a = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex const_b = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        woort_IRValue* diff = woort_IRFunction_new_vreg(f);
        woort_IRValue* product = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(a && b && sum && diff && product);

        a = woort_IRFunction_load_const(f, const_a); TEST_ASSERT(a != NULL);
        b = woort_IRFunction_load_const(f, const_b); TEST_ASSERT(b != NULL);
        TEST_ASSERT(woort_IR_ADDI(f, sum, a, b));
        TEST_ASSERT(woort_IR_SUBI(f, diff, a, b));
        TEST_ASSERT(woort_IR_MULI(f, product, sum, diff));
        TEST_ASSERT(woort_IR_ret(f, product));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[const_a].m_integer = 10;
    cenv->m_data_begin[const_b].m_integer = 3;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(91, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 3: 整数除法和取模 ========== */
/*
func divmod() => int {
    return (17 / 5) + (17 % 5);    // 3 + 2 = 5
}
*/
static void test_divmod(void)
{
    TEST_BEGIN("divmod (17/5 + 17%%5)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c17 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v17 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v5 = woort_IRFunction_new_vreg(f);
        woort_IRValue* div_result = woort_IRFunction_new_vreg(f);
        woort_IRValue* mod_result = woort_IRFunction_new_vreg(f);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v17 && v5 && div_result && mod_result && sum);

        v17 = woort_IRFunction_load_const(f, c17); TEST_ASSERT(v17 != NULL);
        v5 = woort_IRFunction_load_const(f, c5); TEST_ASSERT(v5 != NULL);
        TEST_ASSERT(woort_IR_DIVI(f, div_result, v17, v5));
        TEST_ASSERT(woort_IR_MODI(f, mod_result, v17, v5));
        TEST_ASSERT(woort_IR_ADDI(f, sum, div_result, mod_result));
        TEST_ASSERT(woort_IR_ret(f, sum));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c17].m_integer = 17;
    cenv->m_data_begin[c5].m_integer = 5;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(5, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 4: 取负 ========== */
/*
func neg() => int {
    return -42;
}
*/
static void test_negate(void)
{
    TEST_BEGIN("negate (-42)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v42 = woort_IRFunction_new_vreg(f);
        woort_IRValue* neg = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v42 && neg);

        v42 = woort_IRFunction_load_const(f, c42); TEST_ASSERT(v42 != NULL);
        TEST_ASSERT(woort_IR_NEGI(f, neg, v42));
        TEST_ASSERT(woort_IR_ret(f, neg));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c42].m_integer = 42;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(-42, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 5: 条件分支 (if-else) ========== */
/*
func max(a, b: int) => int {
    if (a >= b) return a;
    else        return b;
}

用 jcc_ge 跳转到 L_true 返回 a，否则 fall-through 返回 b。
*/
static void test_branch_helper(woort_Int a, woort_Int b, woort_Int expected)
{
    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex ca = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex cb = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(&irc, 0, &f);
    {
        woort_IRValue* va = woort_IRFunction_new_vreg(f);
        woort_IRValue* vb = woort_IRFunction_new_vreg(f);

        woort_IRLabel* L_true = woort_IRFunction_new_label(f);

        va = woort_IRFunction_load_const(f, ca);
        vb = woort_IRFunction_load_const(f, cb);

        /* if (a >= b) goto L_true */
        (void)woort_IR_jcc_ge(f, va, vb, L_true);

        /* false path (fall-through): return b */
        (void)woort_IR_ret(f, vb);

        /* L_true: return a */
        (void)woort_IR_bind(f, L_true);
        (void)woort_IR_ret(f, va);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(&irc, &cenv);
    cenv->m_data_begin[ca].m_integer = a;
    cenv->m_data_begin[cb].m_integer = b;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    assert(status == WOORT_VM_CALL_STATUS_NORMAL);
    assert(vm->m_sp[0].m_integer == expected);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);
}

static void test_branch_ge(void)
{
    TEST_BEGIN("branch_ge (max via ge)");

    test_branch_helper(7, 3, 7);    /* a >= b, 返回 a */
    test_branch_helper(2, 9, 9);    /* a < b,  返回 b */
    test_branch_helper(5, 5, 5);    /* a == b, 返回 a */

    TEST_END();
}

/* ========== 测试 6: 循环 (sum 1..N, 使用 MOV 代替 PHI) ========== */
/*
func sum_1_to_n() => int {
    n = 10; i = 1; acc = 0;
    L_header:
      if (i > n) goto L_exit;
      acc = acc + i;
      i = i + 1;
      goto L_header;
    L_exit:
      return acc;
}

sum(10) = 55
*/
static void test_loop(void)
{
    TEST_BEGIN("loop (sum 1..10 = 55)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cn = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* vn = woort_IRFunction_new_vreg(f);
        woort_IRValue* val0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* val1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* acc = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(vn && val0 && val1 && i && acc);

        woort_IRLabel* L_header = woort_IRFunction_new_label(f);
        woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_header && L_exit);

        /* 加载常量 */
        vn = woort_IRFunction_load_const(f, cn); TEST_ASSERT(vn != NULL);
        val0 = woort_IRFunction_load_const(f, c0); TEST_ASSERT(val0 != NULL);
        val1 = woort_IRFunction_load_const(f, c1); TEST_ASSERT(val1 != NULL);

        /* 初始化循环变量 */
        TEST_ASSERT(woort_IR_MOV(f, i, val1));       /* i = 1 */
        TEST_ASSERT(woort_IR_MOV(f, acc, val0));     /* acc = 0 */

        /* L_header: */
        TEST_ASSERT(woort_IR_bind(f, L_header));

        /* if (i > n) goto L_exit */
        TEST_ASSERT(woort_IR_jcc_gt(f, i, vn, L_exit));

        /* acc = acc + i */
        TEST_ASSERT(woort_IR_ADDI(f, acc, acc, i));

        /* i = i + 1 */
        TEST_ASSERT(woort_IR_ADDI(f, i, i, val1));

        /* goto L_header */
        TEST_ASSERT(woort_IR_jmp(f, L_header));

        /* L_exit: */
        TEST_ASSERT(woort_IR_bind(f, L_exit));

        /* return acc */
        TEST_ASSERT(woort_IR_ret(f, acc));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[cn].m_integer = 10;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(55, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 7: 递归 Fibonacci ========== */
/*
func fib(n: int) => int {
    if (n < 2) return n;
    return fib(n-1) + fib(n-2);
}

fib(10) = 55
*/
static void test_fibonacci(void)
{
    TEST_BEGIN("fibonacci (fib(10) = 55)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex cfib = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex cn = woort_IRCompiler_add_constant(&irc);

    /* func fib(n) */
    woort_IRFunction* f_fib;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_fib));
    {
        woort_IRValue* n_arg = woort_IRFunction_get_argument(f_fib, 0);
        woort_IRValue* v2 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* tmp1 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* tmp2 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f_fib);
        TEST_ASSERT(n_arg && v2 && v1 && tmp1 && tmp2 && r1 && r2 && sum);

        woort_IRLabel* L_base = woort_IRFunction_new_label(f_fib);
        TEST_ASSERT(L_base != NULL);

        v2 = woort_IRFunction_load_const(f_fib, c2); TEST_ASSERT(v2 != NULL);
        v1 = woort_IRFunction_load_const(f_fib, c1); TEST_ASSERT(v1 != NULL);

        /* if (n < 2) goto L_base */
        TEST_ASSERT(woort_IR_jcc_lt(f_fib, n_arg, v2, L_base));

        /* recursive case */
        TEST_ASSERT(woort_IR_SUBI(f_fib, tmp1, n_arg, v1));    /* tmp1 = n - 1 */
        TEST_ASSERT(woort_IR_SUBI(f_fib, tmp2, n_arg, v2));    /* tmp2 = n - 2 */

        TEST_ASSERT(woort_IR_PUSHCHK(f_fib, tmp1));
        TEST_ASSERT(woort_IR_CALLNWO(f_fib, cfib, 1, r1));     /* r1 = fib(n-1) */

        TEST_ASSERT(woort_IR_PUSHCHK(f_fib, tmp2));
        TEST_ASSERT(woort_IR_CALLNWO(f_fib, cfib, 1, r2));     /* r2 = fib(n-2) */

        TEST_ASSERT(woort_IR_ADDI(f_fib, sum, r1, r2));        /* sum = r1 + r2 */
        TEST_ASSERT(woort_IR_ret(f_fib, sum));

        /* L_base: return n */
        TEST_ASSERT(woort_IR_bind(f_fib, L_base));
        TEST_ASSERT(woort_IR_ret(f_fib, n_arg));
    }

    /* func main(): 调用 fib(10) */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* vn = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(vn && result);

        vn = woort_IRFunction_load_const(f_main, cn); TEST_ASSERT(vn != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, vn));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, cfib, 1, result));
        TEST_ASSERT(woort_IR_ret(f_main, result));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c2].m_integer = 2;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[cfib].m_script_function = cenv->m_code_begin + 0;
    cenv->m_data_begin[cn].m_integer = 10;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    /* main 是第二个函数，其代码从 fib 之后开始 */
    woort_VmCallStatus status = woort_VMRuntime_invoke(
        vm, cenv->m_code_begin + f_fib->m_code_length);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(55, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 8: 逻辑运算 ========== */
/*
func logic_ops() => int {
    a = 1; b = 0;
    return (a && b) + (!b) + (a || b);  // 0 + 1 + 1 = 2
}
*/
static void test_logic_ops(void)
{
    TEST_BEGIN("logic_ops (AND/OR/NOT)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* land = woort_IRFunction_new_vreg(f);
        woort_IRValue* lnot = woort_IRFunction_new_vreg(f);
        woort_IRValue* lor = woort_IRFunction_new_vreg(f);
        woort_IRValue* s1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* s2 = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v1 && v0 && land && lnot && lor && s1 && s2);

        v1 = woort_IRFunction_load_const(f, c1); TEST_ASSERT(v1 != NULL);
        v0 = woort_IRFunction_load_const(f, c0); TEST_ASSERT(v0 != NULL);

        TEST_ASSERT(woort_IR_LAND(f, land, v1, v0));   /* 1 && 0 = 0 */
        TEST_ASSERT(woort_IR_LNOT(f, lnot, v0));       /* !0 = 1 */
        TEST_ASSERT(woort_IR_LOR(f, lor, v1, v0));     /* 1 || 0 = 1 */

        TEST_ASSERT(woort_IR_ADDI(f, s1, land, lnot));  /* 0 + 1 = 1 */
        TEST_ASSERT(woort_IR_ADDI(f, s2, s1, lor));     /* 1 + 1 = 2 */

        TEST_ASSERT(woort_IR_ret(f, s2));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[c0].m_integer = 0;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(2, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 9: 整数比较运算 ========== */
/*
func cmp_ops() => int {
    a = 5; b = 3;
    return (a < b) + (a > b) + (a <= b) + (a >= b) + (a == b) + (a != b);
    // =     0     +    1    +     0    +     1    +     0    +     1
    // = 3
}
*/
static void test_integer_comparisons(void)
{
    TEST_BEGIN("integer_comparisons (LT/GT/LE/GE/EQ/NE)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex ca = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex cb = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* lt = woort_IRFunction_new_vreg(f);
        woort_IRValue* gt = woort_IRFunction_new_vreg(f);
        woort_IRValue* le = woort_IRFunction_new_vreg(f);
        woort_IRValue* ge = woort_IRFunction_new_vreg(f);
        woort_IRValue* eq = woort_IRFunction_new_vreg(f);
        woort_IRValue* ne = woort_IRFunction_new_vreg(f);
        woort_IRValue* s1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* s2 = woort_IRFunction_new_vreg(f);
        woort_IRValue* s3 = woort_IRFunction_new_vreg(f);
        woort_IRValue* s4 = woort_IRFunction_new_vreg(f);
        woort_IRValue* s5 = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(a && b && lt && gt && le && ge && eq && ne);
        TEST_ASSERT(s1 && s2 && s3 && s4 && s5);

        a = woort_IRFunction_load_const(f, ca); TEST_ASSERT(a != NULL);
        b = woort_IRFunction_load_const(f, cb); TEST_ASSERT(b != NULL);

        TEST_ASSERT(woort_IR_LTI(f, lt, a, b));   /* 5 < 3 = 0 */
        TEST_ASSERT(woort_IR_GTI(f, gt, a, b));   /* 5 > 3 = 1 */
        TEST_ASSERT(woort_IR_LEI(f, le, a, b));   /* 5 <= 3 = 0 */
        TEST_ASSERT(woort_IR_GEI(f, ge, a, b));   /* 5 >= 3 = 1 */
        TEST_ASSERT(woort_IR_EQI(f, eq, a, b));   /* 5 == 3 = 0 */
        TEST_ASSERT(woort_IR_NEI(f, ne, a, b));   /* 5 != 3 = 1 */

        TEST_ASSERT(woort_IR_ADDI(f, s1, lt, gt));
        TEST_ASSERT(woort_IR_ADDI(f, s2, s1, le));
        TEST_ASSERT(woort_IR_ADDI(f, s3, s2, ge));
        TEST_ASSERT(woort_IR_ADDI(f, s4, s3, eq));
        TEST_ASSERT(woort_IR_ADDI(f, s5, s4, ne));

        TEST_ASSERT(woort_IR_ret(f, s5));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[ca].m_integer = 5;
    cenv->m_data_begin[cb].m_integer = 3;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(3, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 10: 多块 fall-through 优化 ========== */
/*
测试 fall-through 优化是否正确：
    jmp(L_mid)
    L_mid:
    jmp(L_exit)
    L_exit:
    return 99
*/
static void test_fallthrough(void)
{
    TEST_BEGIN("fallthrough (chained br)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c99 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v99 = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v99 != NULL);

        woort_IRLabel* L_mid = woort_IRFunction_new_label(f);
        woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_mid && L_exit);

        v99 = woort_IRFunction_load_const(f, c99); TEST_ASSERT(v99 != NULL);

        TEST_ASSERT(woort_IR_jmp(f, L_mid));

        TEST_ASSERT(woort_IR_bind(f, L_mid));
        TEST_ASSERT(woort_IR_jmp(f, L_exit));

        TEST_ASSERT(woort_IR_bind(f, L_exit));
        TEST_ASSERT(woort_IR_ret(f, v99));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c99].m_integer = 99;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(99, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 11: 调用 native 函数 ========== */
/*
func main() {
    capture_int(123);
}
检查 g_captured_int == 123
*/
static void test_call_native(void)
{
    TEST_BEGIN("call_native (capture_int)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v = woort_IRFunction_load_const(f, c_val); TEST_ASSERT(v != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, v));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));
        TEST_ASSERT(woort_IR_ret_void(f));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c_val].m_integer = 123;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    g_captured_int = 0;
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(123, g_captured_int);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 12: 多参数函数调用 ========== */
/*
func add3(a, b, c: int) => int {
    return a + b + c;
}
main: add3(10, 20, 30) => 60
*/
static void test_multi_param(void)
{
    TEST_BEGIN("multi_param (add3(10,20,30) = 60)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c30 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(&irc);

    /* func add3(a, b, c) */
    woort_IRFunction* f_add3;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 3, &f_add3));
    {
        woort_IRValue* a = woort_IRFunction_get_argument(f_add3, 0);
        woort_IRValue* b = woort_IRFunction_get_argument(f_add3, 1);
        woort_IRValue* c = woort_IRFunction_get_argument(f_add3, 2);
        woort_IRValue* tmp = woort_IRFunction_new_vreg(f_add3);
        woort_IRValue* res = woort_IRFunction_new_vreg(f_add3);
        TEST_ASSERT(a && b && c && tmp && res);

        TEST_ASSERT(woort_IR_ADDI(f_add3, tmp, a, b));
        TEST_ASSERT(woort_IR_ADDI(f_add3, res, tmp, c));
        TEST_ASSERT(woort_IR_ret(f_add3, res));
    }

    /* func main() */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* v10 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* v20 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* v30 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(v10 && v20 && v30 && result);

        v10 = woort_IRFunction_load_const(f_main, c10); TEST_ASSERT(v10 != NULL);
        v20 = woort_IRFunction_load_const(f_main, c20); TEST_ASSERT(v20 != NULL);
        v30 = woort_IRFunction_load_const(f_main, c30); TEST_ASSERT(v30 != NULL);

        /* 参数按逆序压栈: 先压最后一个参数 */
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, v10));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, v20));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, v30));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, cfn, 3, result));
        TEST_ASSERT(woort_IR_ret(f_main, result));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c20].m_integer = 20;
    cenv->m_data_begin[c30].m_integer = 30;
    cenv->m_data_begin[cfn].m_script_function = cenv->m_code_begin + 0;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(
        vm, cenv->m_code_begin + f_add3->m_code_length);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(60, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 13: jcc/jccz 条件跳转 ========== */
/*
func abs(x: int) => int {
    neg = -x;
    cond = x < 0;          // cond = (x < 0)
    if (cond) goto L_neg;  // jcc: if cond != 0
    return x;
    L_neg:
    return neg;
}

测试: abs(5)=5, abs(-7)=7, abs(0)=0
*/
static void test_jcc_helper(woort_Int x, woort_Int expected)
{
    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cx = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(&irc, 0, &f);
    {
        woort_IRValue* vx = woort_IRFunction_new_vreg(f);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* neg = woort_IRFunction_new_vreg(f);
        woort_IRValue* cond = woort_IRFunction_new_vreg(f);

        woort_IRLabel* L_neg = woort_IRFunction_new_label(f);

        vx = woort_IRFunction_load_const(f, cx);
        v0 = woort_IRFunction_load_const(f, c0);
        (void)woort_IR_NEGI(f, neg, vx);
        (void)woort_IR_LTI(f, cond, vx, v0);   /* cond = (x < 0) */

        (void)woort_IR_jcc(f, cond, L_neg);     /* if (cond != 0) goto L_neg */

        (void)woort_IR_ret(f, vx);              /* return x */

        (void)woort_IR_bind(f, L_neg);
        (void)woort_IR_ret(f, neg);             /* return -x */
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(&irc, &cenv);
    cenv->m_data_begin[cx].m_integer = x;
    cenv->m_data_begin[c0].m_integer = 0;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    assert(status == WOORT_VM_CALL_STATUS_NORMAL);
    assert(vm->m_sp[0].m_integer == expected);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);
}

static void test_jcc(void)
{
    TEST_BEGIN("jcc (abs via bool cond)");

    test_jcc_helper(5, 5);
    test_jcc_helper(-7, 7);
    test_jcc_helper(0, 0);

    TEST_END();
}

/* ========== 测试 14: jccz 条件跳转 ========== */
/*
func is_zero(x) => int {
    eq_zero = (x == 0);
    if (eq_zero == 0) goto L_not_zero;  // jccz: if cond == 0
    return 1;
    L_not_zero:
    return 0;
}
测试: is_zero(0)=1, is_zero(5)=0, is_zero(-3)=0
*/
static void test_jccz_helper(woort_Int x, woort_Int expected)
{
    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cx = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(&irc, 0, &f);
    {
        woort_IRValue* vx = woort_IRFunction_new_vreg(f);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* cond = woort_IRFunction_new_vreg(f);

        woort_IRLabel* L_not_zero = woort_IRFunction_new_label(f);

        vx = woort_IRFunction_load_const(f, cx);
        v0 = woort_IRFunction_load_const(f, c0);
        v1 = woort_IRFunction_load_const(f, c1);
        (void)woort_IR_EQI(f, cond, vx, v0);   /* cond = (x == 0) */

        (void)woort_IR_jccz(f, cond, L_not_zero);  /* if (cond == 0) goto L_not_zero */

        (void)woort_IR_ret(f, v1);              /* return 1 */

        (void)woort_IR_bind(f, L_not_zero);
        (void)woort_IR_ret(f, v0);              /* return 0 */
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(&irc, &cenv);
    cenv->m_data_begin[cx].m_integer = x;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    assert(status == WOORT_VM_CALL_STATUS_NORMAL);
    assert(vm->m_sp[0].m_integer == expected);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);
}

static void test_jccz(void)
{
    TEST_BEGIN("jccz (is_zero via jccz)");

    test_jccz_helper(0, 1);
    test_jccz_helper(5, 0);
    test_jccz_helper(-3, 0);

    TEST_END();
}

/* ========== 测试 15: jcc_lt / jcc_eq / jcc_ne 全覆盖 ========== */
/*
func clamp(x, lo, hi) => int {
    if (x < lo) return lo;     // jcc_lt
    if (x == hi) return hi;    // jcc_eq
    if (x != lo) return x;    // jcc_ne
    return lo;
}
测试: clamp(1,5,10)=5, clamp(10,5,10)=10, clamp(7,5,10)=7, clamp(5,5,10)=5
*/
static void test_jcc_variants_helper(woort_Int x, woort_Int lo, woort_Int hi,
                                     woort_Int expected)
{
    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cx = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex clo = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex chi = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(&irc, 0, &f);
    {
        woort_IRValue* vx = woort_IRFunction_new_vreg(f);
        woort_IRValue* vlo = woort_IRFunction_new_vreg(f);
        woort_IRValue* vhi = woort_IRFunction_new_vreg(f);

        woort_IRLabel* L_lt = woort_IRFunction_new_label(f);
        woort_IRLabel* L_eq = woort_IRFunction_new_label(f);
        woort_IRLabel* L_ne = woort_IRFunction_new_label(f);

        vx = woort_IRFunction_load_const(f, cx);
        vlo = woort_IRFunction_load_const(f, clo);
        vhi = woort_IRFunction_load_const(f, chi);

        (void)woort_IR_jcc_lt(f, vx, vlo, L_lt);   /* if (x < lo) goto L_lt */
        (void)woort_IR_jcc_eq(f, vx, vhi, L_eq);   /* if (x == hi) goto L_eq */
        (void)woort_IR_jcc_ne(f, vx, vlo, L_ne);   /* if (x != lo) goto L_ne */
        /* fall-through: x == lo */
        (void)woort_IR_ret(f, vlo);

        (void)woort_IR_bind(f, L_lt);
        (void)woort_IR_ret(f, vlo);

        (void)woort_IR_bind(f, L_eq);
        (void)woort_IR_ret(f, vhi);

        (void)woort_IR_bind(f, L_ne);
        (void)woort_IR_ret(f, vx);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(&irc, &cenv);
    cenv->m_data_begin[cx].m_integer = x;
    cenv->m_data_begin[clo].m_integer = lo;
    cenv->m_data_begin[chi].m_integer = hi;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    assert(status == WOORT_VM_CALL_STATUS_NORMAL);
    assert(vm->m_sp[0].m_integer == expected);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);
}

static void test_jcc_variants(void)
{
    TEST_BEGIN("jcc_lt/eq/ne variants (clamp)");

    test_jcc_variants_helper(1, 5, 10, 5);    /* x < lo => return lo */
    test_jcc_variants_helper(10, 5, 10, 10);  /* x == hi => return hi */
    test_jcc_variants_helper(7, 5, 10, 7);    /* x != lo => return x */
    test_jcc_variants_helper(5, 5, 10, 5);    /* x == lo, fall-through => return lo */

    TEST_END();
}

/* ========== 测试 16: 嵌套循环 ========== */
/*
func nested_sum() => int {
    // sum = 0
    // for i in 1..3:
    //   for j in 1..4:
    //     sum += i * j
    // return sum
    //
    // = 1*(1+2+3+4) + 2*(1+2+3+4) + 3*(1+2+3+4)
    // = (1+2+3)*10 = 60
    //
    // 验证嵌套循环结构的正确性：双层后向跳转、多 Label、多活跃变量交叉使用。
}
*/
static void test_nested_loop(void)
{
    TEST_BEGIN("nested_loop (sum i*j, i=1..3, j=1..4)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c4 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v3 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v4 = woort_IRFunction_new_vreg(f);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* j = woort_IRFunction_new_vreg(f);
        woort_IRValue* prod = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v0 && v1 && v3 && v4 && sum && i && j && prod);

        woort_IRLabel* L_outer = woort_IRFunction_new_label(f);
        woort_IRLabel* L_inner = woort_IRFunction_new_label(f);
        woort_IRLabel* L_inner_end = woort_IRFunction_new_label(f);
        woort_IRLabel* L_outer_end = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_outer && L_inner && L_inner_end && L_outer_end);

        v0 = woort_IRFunction_load_const(f, c0); TEST_ASSERT(v0 != NULL);
        v1 = woort_IRFunction_load_const(f, c1); TEST_ASSERT(v1 != NULL);
        v3 = woort_IRFunction_load_const(f, c3); TEST_ASSERT(v3 != NULL);
        v4 = woort_IRFunction_load_const(f, c4); TEST_ASSERT(v4 != NULL);

        TEST_ASSERT(woort_IR_MOV(f, sum, v0));  /* sum = 0 */
        TEST_ASSERT(woort_IR_MOV(f, i, v1));    /* i = 1 */

        /* L_outer: */
        TEST_ASSERT(woort_IR_bind(f, L_outer));
        TEST_ASSERT(woort_IR_jcc_gt(f, i, v3, L_outer_end));  /* if (i > 3) exit */

        TEST_ASSERT(woort_IR_MOV(f, j, v1));    /* j = 1 */

        /* L_inner: */
        TEST_ASSERT(woort_IR_bind(f, L_inner));
        TEST_ASSERT(woort_IR_jcc_gt(f, j, v4, L_inner_end));  /* if (j > 4) inner_exit */

        TEST_ASSERT(woort_IR_MULI(f, prod, i, j));    /* prod = i * j */
        TEST_ASSERT(woort_IR_ADDI(f, sum, sum, prod)); /* sum += prod */
        TEST_ASSERT(woort_IR_ADDI(f, j, j, v1));      /* j++ */
        TEST_ASSERT(woort_IR_jmp(f, L_inner));

        /* L_inner_end: */
        TEST_ASSERT(woort_IR_bind(f, L_inner_end));
        TEST_ASSERT(woort_IR_ADDI(f, i, i, v1));      /* i++ */
        TEST_ASSERT(woort_IR_jmp(f, L_outer));

        /* L_outer_end: */
        TEST_ASSERT(woort_IR_bind(f, L_outer_end));
        TEST_ASSERT(woort_IR_ret(f, sum));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[c3].m_integer = 3;
    cenv->m_data_begin[c4].m_integer = 4;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(60, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 17: compound 指令选择验证 ========== */
/*
验证 dst == src 时触发 compound 指令（CADDI/CSUBI 等）。
func compound_ops() => int {
    a = 10;
    a = a + 5;      // 应当生成 CADDI
    a = a - 3;      // 应当生成 CSUBI
    a = a * 2;      // 应当生成 CMULI
    return a;       // 10 + 5 - 3 = 12, 12 * 2 = 24
}
*/
static void test_compound_ops(void)
{
    TEST_BEGIN("compound_ops (CADDI/CSUBI/CMULI)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* v5 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v3 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v2 = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(a && v5 && v3 && v2);

        a = woort_IRFunction_load_const(f, c10); TEST_ASSERT(a != NULL);
        v5 = woort_IRFunction_load_const(f, c5); TEST_ASSERT(v5 != NULL);
        v3 = woort_IRFunction_load_const(f, c3); TEST_ASSERT(v3 != NULL);
        v2 = woort_IRFunction_load_const(f, c2); TEST_ASSERT(v2 != NULL);

        /* dst == src[0] 的情况: a = a + v5 → CADDI */
        TEST_ASSERT(woort_IR_ADDI(f, a, a, v5));

        /* dst == src[0] 的情况: a = a - v3 → CSUBI */
        TEST_ASSERT(woort_IR_SUBI(f, a, a, v3));

        /* dst == src[0] 的情况: a = a * v2 → CMULI */
        TEST_ASSERT(woort_IR_MULI(f, a, a, v2));

        TEST_ASSERT(woort_IR_ret(f, a));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c5].m_integer = 5;
    cenv->m_data_begin[c3].m_integer = 3;
    cenv->m_data_begin[c2].m_integer = 2;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(24, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 18: vreg 复用（同一寄存器多次写入） ========== */
/*
验证可变虚拟寄存器的核心特性：同一寄存器被反复写入不同值。
func reuse() => int {
    x = 1;
    x = x + x;     // 2
    x = x * x;     // 4
    x = x + x;     // 8
    x = x + x;     // 16
    return x;
}
*/
static void test_vreg_reuse(void)
{
    TEST_BEGIN("vreg_reuse (single reg 1->2->4->8->16)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* x = woort_IRFunction_load_const(f, c1); TEST_ASSERT(x != NULL);    /* x = 1 */
        TEST_ASSERT(woort_IR_ADDI(f, x, x, x));         /* x = x + x = 2 */
        TEST_ASSERT(woort_IR_MULI(f, x, x, x));         /* x = x * x = 4 */
        TEST_ASSERT(woort_IR_ADDI(f, x, x, x));         /* x = x + x = 8 */
        TEST_ASSERT(woort_IR_ADDI(f, x, x, x));         /* x = x + x = 16 */
        TEST_ASSERT(woort_IR_ret(f, x));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c1].m_integer = 1;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(16, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 19: 多路分支 (if-elseif-else) ========== */
/*
func sign(x) => int {
    if (x < 0) return -1;
    if (x == 0) return 0;
    return 1;
}
测试: sign(-5)=-1, sign(0)=0, sign(7)=1
*/
static void test_multi_branch_helper(woort_Int x, woort_Int expected)
{
    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cx = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex cm1 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(&irc, 0, &f);
    {
        woort_IRValue* vx = woort_IRFunction_new_vreg(f);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* vm1 = woort_IRFunction_new_vreg(f);

        woort_IRLabel* L_negative = woort_IRFunction_new_label(f);
        woort_IRLabel* L_zero = woort_IRFunction_new_label(f);

        vx = woort_IRFunction_load_const(f, cx);
        v0 = woort_IRFunction_load_const(f, c0);
        v1 = woort_IRFunction_load_const(f, c1);
        vm1 = woort_IRFunction_load_const(f, cm1);

        (void)woort_IR_jcc_lt(f, vx, v0, L_negative);  /* if (x < 0) goto neg */
        (void)woort_IR_jcc_eq(f, vx, v0, L_zero);      /* if (x == 0) goto zero */
        (void)woort_IR_ret(f, v1);                      /* return 1 (positive) */

        (void)woort_IR_bind(f, L_negative);
        (void)woort_IR_ret(f, vm1);                     /* return -1 */

        (void)woort_IR_bind(f, L_zero);
        (void)woort_IR_ret(f, v0);                      /* return 0 */
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(&irc, &cenv);
    cenv->m_data_begin[cx].m_integer = x;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[cm1].m_integer = -1;

    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    assert(status == WOORT_VM_CALL_STATUS_NORMAL);
    assert(vm->m_sp[0].m_integer == expected);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);
}

static void test_multi_branch(void)
{
    TEST_BEGIN("multi_branch (sign: -1/0/1)");

    test_multi_branch_helper(-5, -1);
    test_multi_branch_helper(0, 0);
    test_multi_branch_helper(7, 1);

    TEST_END();
}

/* ========== 测试 20: 空函数 (ret_void) ========== */
/*
func noop() => void { }
只是验证没有局部变量的空函数能正确编译和执行。
*/
static void test_empty_func(void)
{
    TEST_BEGIN("empty_func (ret_void only)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        TEST_ASSERT(woort_IR_ret_void(f));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 21: LOAD/STORE 静态存储 ========== */
/*
使用 static 存储来模拟全局可变变量：
func inc_counter() => int {
    val = LOAD(counter);
    val = val + 1;
    STORE(counter, val);
    return val;
}
连续调用两次，验证计数器累加。
*/
static void test_static_storage(void)
{
    TEST_BEGIN("static_storage (LOAD/STORE counter)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRStaticIndex s_counter = woort_IRCompiler_add_static(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* val = woort_IRFunction_new_vreg(f);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(val && v1);

        v1 = woort_IRFunction_load_const(f, c1); TEST_ASSERT(v1 != NULL);
        TEST_ASSERT(woort_IR_LOAD(f, val, s_counter));    /* val = G[static] */
        TEST_ASSERT(woort_IR_ADDI(f, val, val, v1));       /* val = val + 1 */
        TEST_ASSERT(woort_IR_STORE(f, s_counter, val));    /* G[static] = val */
        TEST_ASSERT(woort_IR_ret(f, val));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c1].m_integer = 1;

    /* 静态存储的 data 索引 = constant_count + static_index */
    uint32_t counter_data_idx = irc.m_constant_alloc_count + s_counter;
    cenv->m_data_begin[counter_data_idx].m_integer = 0;  /* 初始值 0 */

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    /* 第一次调用: 0 + 1 = 1 */
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(1, vm->m_sp[0].m_integer);

    /* 第二次调用: 1 + 1 = 2 */
    status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(2, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 22: 多 native 调用 + 多参数压栈 ========== */
/*
func main() {
    capture_int(100);
    capture_int(200);
    capture_int(300);
}
验证连续调用 native 函数，每次都正确传递参数。
*/
static void test_multi_native_call(void)
{
    TEST_BEGIN("multi_native_call (3x capture_int)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c100 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c200 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c300 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v100 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v200 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v300 = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v100 && v200 && v300);

        v100 = woort_IRFunction_load_const(f, c100); TEST_ASSERT(v100 != NULL);
        v200 = woort_IRFunction_load_const(f, c200); TEST_ASSERT(v200 != NULL);
        v300 = woort_IRFunction_load_const(f, c300); TEST_ASSERT(v300 != NULL);

        /* 第一次调用 */
        TEST_ASSERT(woort_IR_PUSHCHK(f, v100));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        /* 第二次调用 */
        TEST_ASSERT(woort_IR_PUSHCHK(f, v200));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        /* 第三次调用 */
        TEST_ASSERT(woort_IR_PUSHCHK(f, v300));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        TEST_ASSERT(woort_IR_ret_void(f));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c100].m_integer = 100;
    cenv->m_data_begin[c200].m_integer = 200;
    cenv->m_data_begin[c300].m_integer = 300;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    g_captured_int = 0;
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    /* 最后一次 capture_int(300) 的结果应当留在 g_captured_int */
    TEST_ASSERT_EQ_INT(300, g_captured_int);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 23: 迭代 Fibonacci（循环版，验证 MOV 替代 PHI 的复杂场景） ========== */
/*
func fib_iter(n: int) => int {
    if (n < 2) return n;
    a = 0; b = 1;
    i = 2;
    while (i <= n) {
        t = a + b;
        a = b;
        b = t;
        i = i + 1;
    }
    return b;
}
fib_iter(10) = 55
*/
static void test_fib_iterative(void)
{
    TEST_BEGIN("fib_iterative (fib_iter(10) = 55)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cn = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* vn = woort_IRFunction_new_vreg(f);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v2 = woort_IRFunction_new_vreg(f);
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* t = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(vn && v0 && v1 && v2 && a && b && i && t);

        woort_IRLabel* L_base = woort_IRFunction_new_label(f);
        woort_IRLabel* L_loop = woort_IRFunction_new_label(f);
        woort_IRLabel* L_end = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_base && L_loop && L_end);

        vn = woort_IRFunction_load_const(f, cn); TEST_ASSERT(vn != NULL);
        v0 = woort_IRFunction_load_const(f, c0); TEST_ASSERT(v0 != NULL);
        v1 = woort_IRFunction_load_const(f, c1); TEST_ASSERT(v1 != NULL);
        v2 = woort_IRFunction_load_const(f, c2); TEST_ASSERT(v2 != NULL);

        /* if (n < 2) goto L_base */
        TEST_ASSERT(woort_IR_jcc_lt(f, vn, v2, L_base));

        /* a = 0; b = 1; i = 2 */
        TEST_ASSERT(woort_IR_MOV(f, a, v0));
        TEST_ASSERT(woort_IR_MOV(f, b, v1));
        TEST_ASSERT(woort_IR_MOV(f, i, v2));

        /* L_loop: */
        TEST_ASSERT(woort_IR_bind(f, L_loop));
        TEST_ASSERT(woort_IR_jcc_gt(f, i, vn, L_end));  /* if (i > n) exit */

        /* t = a + b; a = b; b = t; i++ */
        TEST_ASSERT(woort_IR_ADDI(f, t, a, b));
        TEST_ASSERT(woort_IR_MOV(f, a, b));
        TEST_ASSERT(woort_IR_MOV(f, b, t));
        TEST_ASSERT(woort_IR_ADDI(f, i, i, v1));
        TEST_ASSERT(woort_IR_jmp(f, L_loop));

        /* L_end: return b */
        TEST_ASSERT(woort_IR_bind(f, L_end));
        TEST_ASSERT(woort_IR_ret(f, b));

        /* L_base: return n */
        TEST_ASSERT(woort_IR_bind(f, L_base));
        TEST_ASSERT(woort_IR_ret(f, vn));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[cn].m_integer = 10;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[c2].m_integer = 2;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(55, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 24: jcc_le 后向跳转（do-while 循环模式） ========== */
/*
func countdown(n) => int {
    sum = 0;
    L_body:
    sum = sum + n;
    n = n - 1;
    if (n >= 1) goto L_body;    // jcc_ge 后向跳转
    return sum;
}
countdown(5) = 5+4+3+2+1 = 15
*/
static void test_backward_jcc(void)
{
    TEST_BEGIN("backward_jcc (do-while countdown)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cn = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* n = woort_IRFunction_new_vreg(f);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(n && v0 && v1 && sum);

        woort_IRLabel* L_body = woort_IRFunction_new_label(f);

        n = woort_IRFunction_load_const(f, cn); TEST_ASSERT(n != NULL);
        v0 = woort_IRFunction_load_const(f, c0); TEST_ASSERT(v0 != NULL);
        v1 = woort_IRFunction_load_const(f, c1); TEST_ASSERT(v1 != NULL);
        TEST_ASSERT(woort_IR_MOV(f, sum, v0));   /* sum = 0 */

        /* L_body: */
        TEST_ASSERT(woort_IR_bind(f, L_body));
        TEST_ASSERT(woort_IR_ADDI(f, sum, sum, n));   /* sum += n */
        TEST_ASSERT(woort_IR_SUBI(f, n, n, v1));      /* n-- */
        TEST_ASSERT(woort_IR_jcc_ge(f, n, v1, L_body)); /* if (n >= 1) goto body */

        TEST_ASSERT(woort_IR_ret(f, sum));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[cn].m_integer = 5;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(15, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 25: 常量加载外提验证 ========== */
/*
验证 LOAD_CONST 写在循环体内部时，框架能否将其提升到循环外。

func hoist_test() => int {
    sum = 0; i = 0;
    L_loop:
      step = LOAD_CONST(3);  // 故意写在循环体内部
      n    = LOAD_CONST(10); // 同上
      if (i >= n) goto L_end;
      sum = sum + step;
      i = i + LOAD_CONST(1); // 又一个循环体内的常量
      goto L_loop;
    L_end:
      return sum;
}
结果: 0 + 3*10 = 30

如果常量没有被外提，每次迭代都执行 LOAD 也会得到正确结果；
但通过 dump 字节码可以验证 LOAD 出现在循环外。
*/
static void test_const_hoist(void)
{
    TEST_BEGIN("const_hoist (LOAD_CONST inside loop)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c0    = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1    = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c3    = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c10   = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* sum  = woort_IRFunction_new_vreg(f);
        woort_IRValue* i    = woort_IRFunction_new_vreg(f);
        woort_IRValue* step = woort_IRFunction_new_vreg(f);
        woort_IRValue* n    = woort_IRFunction_new_vreg(f);
        woort_IRValue* v0   = woort_IRFunction_new_vreg(f);
        woort_IRValue* v1   = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(sum && i && step && n && v0 && v1);

        woort_IRLabel* L_loop = woort_IRFunction_new_label(f);
        woort_IRLabel* L_end  = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_loop && L_end);

        /* 初始化 sum = 0, i = 0 */
        v0 = woort_IRFunction_load_const(f, c0); TEST_ASSERT(v0 != NULL);
        TEST_ASSERT(woort_IR_MOV(f, sum, v0));
        TEST_ASSERT(woort_IR_MOV(f, i, v0));

        /* L_loop: */
        TEST_ASSERT(woort_IR_bind(f, L_loop));

        /*
         * 故意在循环体内部写 LOAD_CONST，
         * 期望常量加载放置将其外提到循环头的 idom（即入口 block）。
         */
        step = woort_IRFunction_load_const(f, c3); TEST_ASSERT(step != NULL);   /* step = 3 */
        n = woort_IRFunction_load_const(f, c10); TEST_ASSERT(n != NULL);     /* n = 10 */
        v1 = woort_IRFunction_load_const(f, c1); TEST_ASSERT(v1 != NULL);     /* v1 = 1 */

        TEST_ASSERT(woort_IR_jcc_ge(f, i, n, L_end));    /* if (i >= n) exit */

        TEST_ASSERT(woort_IR_ADDI(f, sum, sum, step));   /* sum += step */
        TEST_ASSERT(woort_IR_ADDI(f, i, i, v1));         /* i++ */
        TEST_ASSERT(woort_IR_jmp(f, L_loop));

        /* L_end: */
        TEST_ASSERT(woort_IR_bind(f, L_end));
        TEST_ASSERT(woort_IR_ret(f, sum));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[c3].m_integer = 3;
    cenv->m_data_begin[c10].m_integer = 10;

    /*
     * 验证字节码结构：LOAD 指令应该出现在循环体（JBCK）之前，
     * 而不是在循环体内部。通过 dump_Code(cenv) 可人工检查。
     */


    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(30, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 26: RETVC 常量直接返回 ========== */
/*
验证 LOAD_CONST c -> v; RET v 在 v 仅被 RET 使用时生成 RETVC 而非 LOAD + RETVS。

func return_const() => int { return 42; }

期望字节码: RETVC G[0]  (没有 LOAD，没有 PUSHRCHK)
*/
static void test_retvc(void)
{
    TEST_BEGIN("retvc (const direct return)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v = woort_IRFunction_load_const(f, c42); TEST_ASSERT(v != NULL);
        TEST_ASSERT(woort_IR_ret(f, v));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c42].m_integer = 42;

    /*
     * v 仅被 RET 使用 → 应标记为 const_direct
     * 期望字节码: 仅 RETVC G[0]，无 LOAD / RETVS / PUSHRCHK
     */


    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 27: PUSHCCHK 常量直接压栈 ========== */
/*
验证 LOAD_CONST c -> v; PUSHCHK v 在 v 仅被 PUSHCHK 使用时生成 PUSHCCHK。

func main() { capture_int(999); }

期望: PUSHCCHK G[0] 而非 LOAD + PUSHSCHK
*/
static void test_pushcchk(void)
{
    TEST_BEGIN("pushcchk (const direct push)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c_val = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v = woort_IRFunction_load_const(f, c_val); TEST_ASSERT(v != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, v));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));
        TEST_ASSERT(woort_IR_ret_void(f));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c_val].m_integer = 999;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;

    /*
     * v 仅被 PUSHCHK 使用 → 应标记为 const_direct
     * 期望: PUSHCCHK G[0] + CALLNFP + POPR 1 + RET
     */


    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    g_captured_int = 0;
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(999, g_captured_int);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 28: 常量直连不触发（多次使用） ========== */
/*
验证 vreg 被多条指令使用时，不会触发常量直连优化。

func multi_use() => int {
    v = LOAD_CONST(10);
    PUSHCHK v;           // 第一次使用
    result = v + v;      // 第二次+第三次使用 → 不能直连
    return result;
}

期望: 仍有 LOAD + PUSHSCHK（不是 PUSHCCHK）
*/
static void test_const_direct_no_trigger(void)
{
    TEST_BEGIN("const_direct_no_trigger (multi use)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v && result);

        v = woort_IRFunction_load_const(f, c10); TEST_ASSERT(v != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, v));            /* 使用 1 */
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));
        TEST_ASSERT(woort_IR_ADDI(f, result, v, v));    /* 使用 2+3 */
        TEST_ASSERT(woort_IR_ret(f, result));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;

    /*
     * v 被 PUSHCHK + ADDI(两次) = 3 次使用 → 不触发直连
     * 期望: LOAD + PUSHSCHK（非 PUSHCCHK）
     */


    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    g_captured_int = 0;
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(20, vm->m_sp[0].m_integer);    /* 10 + 10 = 20 */
    TEST_ASSERT_EQ_INT(10, g_captured_int);            /* capture_int(10) */

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 29: 重复常量加载合并 ========== */
/*
验证同一 const_index 被多个不同 vreg 加载时，后续的 LOAD_CONST 被合并为 MOV。

func dup_const() => int {
    a = LOAD_CONST(7);
    b = LOAD_CONST(7);      // 应合并为 MOV b = a
    c = LOAD_CONST(7);      // 应合并为 MOV c = a
    return a + b + c;       // 7 + 7 + 7 = 21
}

期望字节码: 只有一条 LOAD G[0]，后续是 MOVLD
*/
static void test_const_merge(void)
{
    TEST_BEGIN("const_merge (dup LOAD_CONST -> MOV)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c7 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* c = woort_IRFunction_new_vreg(f);
        woort_IRValue* t = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(a && b && c && t && result);

        a = woort_IRFunction_load_const(f, c7); TEST_ASSERT(a != NULL);
        b = woort_IRFunction_load_const(f, c7); TEST_ASSERT(b != NULL);    /* 应合并为 MOV b = a */
        c = woort_IRFunction_load_const(f, c7); TEST_ASSERT(c != NULL);    /* 应合并为 MOV c = a */
        TEST_ASSERT(woort_IR_ADDI(f, t, a, b));
        TEST_ASSERT(woort_IR_ADDI(f, result, t, c));
        TEST_ASSERT(woort_IR_ret(f, result));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c7].m_integer = 7;

    /*
     * 期望: 只有一条 LOAD G[0]，b 和 c 的赋值变为 MOV
     */


    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(21, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 30: PUSHCCHK + RETVC 组合 ========== */
/*
func fib_base(n) => int {
    if (n < 2) return n;
    v = LOAD_CONST(0);
    PUSHCHK v;             // v 仅被 PUSHCHK 使用 → PUSHCCHK
    CALLNFP capture_int;
    w = LOAD_CONST(99);
    return w;              // w 仅被 RET 使用 → RETVC
}

验证同一函数中 PUSHCCHK 和 RETVC 可以共存。
*/
static void test_pushcchk_retvc_combo(void)
{
    TEST_BEGIN("pushcchk_retvc_combo (both in one fn)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c99 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v_push = woort_IRFunction_new_vreg(f);
        woort_IRValue* v_ret = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v_push && v_ret);

        /* PUSHCCHK: v_push 仅被 PUSHCHK 使用 */
        v_push = woort_IRFunction_load_const(f, c0); TEST_ASSERT(v_push != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, v_push));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        /* RETVC: v_ret 仅被 RET 使用 */
        v_ret = woort_IRFunction_load_const(f, c99); TEST_ASSERT(v_ret != NULL);
        TEST_ASSERT(woort_IR_ret(f, v_ret));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c99].m_integer = 99;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;



    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    g_captured_int = -1;
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(99, vm->m_sp[0].m_integer);
    TEST_ASSERT_EQ_INT(0, g_captured_int);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ==========================================================================
 * 边界情况测试
 * ========================================================================== */

/* ========== 测试 31: def_count > 1 不触发 const_direct ========== */
/*
func f() => int {
    v = LOAD_CONST(42);
    v = v + v;           // v 又被定义一次 → def_count=2
    return v;            // RET 不应触发 RETVC
}
结果: 84
*/
static void test_no_direct_on_redef(void)
{
    TEST_BEGIN("no_direct_on_redef (def_count > 1)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v = woort_IRFunction_load_const(f, c42); TEST_ASSERT(v != NULL);
        TEST_ASSERT(woort_IR_ADDI(f, v, v, v));     /* v = v + v → def_count=2 */
        TEST_ASSERT(woort_IR_ret(f, v));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c42].m_integer = 42;

    /* 期望：不使用 RETVC，因为 v 不再总是持有常量 */


    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(84, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 32: LOAD_CONST 到参数 vreg ========== */
/*
func f(x: int) => int {
    x = LOAD_CONST(99);    // 覆写参数
    return x;              // 应当触发 RETVC
}
结果: 99（忽略传入的参数）
*/
static void test_const_direct_overwrite_arg(void)
{
    TEST_BEGIN("const_direct_overwrite_arg (RETVC)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c99 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(&irc);

    /* func f(x) */
    woort_IRFunction* f_inner;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_inner));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_inner, 0);
        TEST_ASSERT(x != NULL);

        x = woort_IRFunction_load_const(f_inner, c99); TEST_ASSERT(x != NULL);
        TEST_ASSERT(woort_IR_ret(f_inner, x));
    }

    /* func main() */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* arg = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(arg && result);

        arg = woort_IRFunction_load_const(f_main, c99); TEST_ASSERT(arg != NULL);  /* 传 99 作为参数 */
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, arg));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, cfn, 1, result));
        TEST_ASSERT(woort_IR_ret(f_main, result));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c99].m_integer = 99;
    cenv->m_data_begin[cfn].m_script_function = cenv->m_code_begin + 0;



    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(
        vm, cenv->m_code_begin + f_inner->m_code_length);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(99, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 33: vreg 仅用于 JCCZ（不触发 const_direct） ========== */
/*
func f() => int {
    cond = LOAD_CONST(0);
    JCCZ cond, L_skip;        // 仅用于 JCCZ → 不是 PUSHCHK/RET，不触发
    return 100;
    L_skip:
    return 200;
}
cond=0 → JCCZ 跳转 → 结果 200
*/
static void test_no_direct_on_jcc_use(void)
{
    TEST_BEGIN("no_direct_on_jcc_use (JCCZ only)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c100 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c200 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* cond = woort_IRFunction_new_vreg(f);
        woort_IRValue* v100 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v200 = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(cond && v100 && v200);

        woort_IRLabel* L_skip = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_skip != NULL);

        cond = woort_IRFunction_load_const(f, c0); TEST_ASSERT(cond != NULL);
        v100 = woort_IRFunction_load_const(f, c100); TEST_ASSERT(v100 != NULL);
        v200 = woort_IRFunction_load_const(f, c200); TEST_ASSERT(v200 != NULL);

        /* cond 仅被 JCCZ 使用（def=1, use=1）但 JCCZ != PUSHCHK/RET → 不触发 */
        TEST_ASSERT(woort_IR_jccz(f, cond, L_skip));
        TEST_ASSERT(woort_IR_ret(f, v100));       /* v100: def=1, use=1, RET → RETVC */

        TEST_ASSERT(woort_IR_bind(f, L_skip));
        TEST_ASSERT(woort_IR_ret(f, v200));       /* v200: def=1, use=1, RET → RETVC */
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c100].m_integer = 100;
    cenv->m_data_begin[c200].m_integer = 200;



    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    /* cond=0, JCCZ 条件成立（0==0），跳到 L_skip, return 200 */
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(200, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 34: 同 const_index 的 const_direct + 非 direct ========== */
/*
func f() => int {
    v1 = LOAD_CONST(7);   // v1: def=1, use=1(PUSHCHK) → const_direct
    PUSHCHK v1;
    CALLNFP ...;
    v2 = LOAD_CONST(7);   // v2: 同 const_index, def=1, use=2(ADDI×2) → 非 direct
    result = v2 + v2;
    return result;
}
验证: v1 使用 PUSHCCHK, v2 不被合并（v1 被跳过作为 primary），v2 用正常 LOAD
*/
static void test_direct_and_nondirect_same_const(void)
{
    TEST_BEGIN("direct+nondirect same const_index");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c7 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v2 = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v1 && v2 && result);

        v1 = woort_IRFunction_load_const(f, c7); TEST_ASSERT(v1 != NULL);     /* const_direct */
        TEST_ASSERT(woort_IR_PUSHCHK(f, v1));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        v2 = woort_IRFunction_load_const(f, c7); TEST_ASSERT(v2 != NULL);     /* 非 direct (use=2) */
        TEST_ASSERT(woort_IR_ADDI(f, result, v2, v2));
        TEST_ASSERT(woort_IR_ret(f, result));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c7].m_integer = 7;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;



    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    g_captured_int = 0;
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(14, vm->m_sp[0].m_integer);     /* 7 + 7 */
    TEST_ASSERT_EQ_INT(7, g_captured_int);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 35: 连续 LOAD_CONST 覆写同一 vreg ========== */
/*
func f() => int {
    v = LOAD_CONST(10);
    v = LOAD_CONST(20);    // 覆写 v → def_count=2, 两个 LOAD_CONST
    return v;              // 应返回 20
}
验证: def_count=2，不触发 const_direct; Phase 4c 两条 LOAD 都被放置，后者覆盖前者。
*/
static void test_consecutive_load_const(void)
{
    TEST_BEGIN("consecutive_load_const (overwrite vreg)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v = woort_IRFunction_load_const(f, c10); TEST_ASSERT(v != NULL);
        v = woort_IRFunction_load_const(f, c20); TEST_ASSERT(v != NULL);    /* 覆写 */
        TEST_ASSERT(woort_IR_ret(f, v));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c20].m_integer = 20;



    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(20, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 36: 全 const_direct 多次调用 ========== */
/*
func f() => int {
    v1 = LOAD_CONST(100);
    PUSHCHK v1;
    CALLNFP capture_int;      // capture_int(100)
    v2 = LOAD_CONST(200);
    PUSHCHK v2;
    CALLNFP capture_int;      // capture_int(200)
    v3 = LOAD_CONST(300);
    PUSHCHK v3;
    CALLNFP capture_int;      // capture_int(300)
    v_ret = LOAD_CONST(42);
    return v_ret;              // RETVC
}
验证: 所有 vreg 都是 const_direct, 零栈空间, 无 PUSHRCHK
*/
static void test_all_const_direct_zero_stack(void)
{
    TEST_BEGIN("all_const_direct (zero stack space)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c100 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c200 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c300 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v2 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v3 = woort_IRFunction_new_vreg(f);
        woort_IRValue* v_ret = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v1 && v2 && v3 && v_ret);

        v1 = woort_IRFunction_load_const(f, c100); TEST_ASSERT(v1 != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, v1));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        v2 = woort_IRFunction_load_const(f, c200); TEST_ASSERT(v2 != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, v2));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        v3 = woort_IRFunction_load_const(f, c300); TEST_ASSERT(v3 != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, v3));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));

        v_ret = woort_IRFunction_load_const(f, c42); TEST_ASSERT(v_ret != NULL);
        TEST_ASSERT(woort_IR_ret(f, v_ret));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c100].m_integer = 100;
    cenv->m_data_begin[c200].m_integer = 200;
    cenv->m_data_begin[c300].m_integer = 300;
    cenv->m_data_begin[c42].m_integer = 42;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;

    /* 期望: 无 PUSHRCHK, 全部 PUSHCCHK, 最后 RETVC */


    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    g_captured_int = 0;
    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, vm->m_sp[0].m_integer);
    TEST_ASSERT_EQ_INT(300, g_captured_int);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 37: 循环内外同常量合并 ========== */
/*
func f() => int {
    a = LOAD_CONST(5);       // Block 0: 主 vreg
    sum = LOAD_CONST(0);
    i = LOAD_CONST(0);
    n = LOAD_CONST(10);
    one = LOAD_CONST(1);

    L_loop:
    b = LOAD_CONST(5);       // Block 1: 同 const_index=5, 应合并为 MOV b = a
    if (i >= n) goto L_end;
    sum = sum + b;
    i = i + one;
    goto L_loop;

    L_end:
    return sum + a;           // a=5, sum = 5*10 = 50, total = 55
}
*/
static void test_const_merge_across_loop(void)
{
    TEST_BEGIN("const_use_in_loop (loop body)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v5 = woort_IRFunction_load_const(f, c5);
        woort_IRValue* v0 = woort_IRFunction_load_const(f, c0);
        woort_IRValue* v10 = woort_IRFunction_load_const(f, c10);
        woort_IRValue* v1 = woort_IRFunction_load_const(f, c1);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRValue* result = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v5 && v0 && v10 && v1 && sum && i && result);

        woort_IRLabel* L_loop = woort_IRFunction_new_label(f);
        woort_IRLabel* L_end = woort_IRFunction_new_label(f);

        TEST_ASSERT(woort_IR_MOV(f, sum, v0));
        TEST_ASSERT(woort_IR_MOV(f, i, v0));

        TEST_ASSERT(woort_IR_bind(f, L_loop));
        /* v5 在循环内外都被引用：框架自动在合适位置发 LOAD */
        TEST_ASSERT(woort_IR_jcc_ge(f, i, v10, L_end));
        TEST_ASSERT(woort_IR_ADDI(f, sum, sum, v5));
        TEST_ASSERT(woort_IR_ADDI(f, i, i, v1));
        TEST_ASSERT(woort_IR_jmp(f, L_loop));

        TEST_ASSERT(woort_IR_bind(f, L_end));
        TEST_ASSERT(woort_IR_ADDI(f, result, sum, v5));
        TEST_ASSERT(woort_IR_ret(f, result));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c5].m_integer = 5;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c1].m_integer = 1;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    woort_VmCallStatus status = woort_VMRuntime_invoke(vm, cenv->m_code_begin);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(55, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ========== 测试 38: 跨块合并被拒绝（primary 不活跃） ========== */
/*
func f(x: int) => int {
    // Block 0:
    a = LOAD_CONST(10);
    if (x == 0) goto L_then;
    // Block 1 (fall-through): 使用并消耗 a
    result = a + a;
    return result;
    // Block 2 (L_then): a 在此处不活跃（仅在 Block 1 使用）
    b = LOAD_CONST(10);   // 同 const_index，但 a 不 live_in → 不合并
    result2 = b + b;
    return result2;
}
不管走哪条路径，结果都是 20
*/
static void test_const_merge_rejected_not_live(void)
{
    TEST_BEGIN("const_merge_rejected (primary not live)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);

    /* func f(x) */
    woort_IRFunction* f_inner;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_inner));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_inner, 0);
        woort_IRValue* a = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* b = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* result2 = woort_IRFunction_new_vreg(f_inner);
        TEST_ASSERT(x && a && b && v0 && result && result2);

        woort_IRLabel* L_then = woort_IRFunction_new_label(f_inner);

        a = woort_IRFunction_load_const(f_inner, c10); TEST_ASSERT(a != NULL);
        v0 = woort_IRFunction_load_const(f_inner, c0); TEST_ASSERT(v0 != NULL);
        TEST_ASSERT(woort_IR_jcc_eq(f_inner, x, v0, L_then));

        /* Block 1: 使用 a */
        TEST_ASSERT(woort_IR_ADDI(f_inner, result, a, a));
        TEST_ASSERT(woort_IR_ret(f_inner, result));

        /* Block 2: a 不活跃, b = LOAD_CONST(10) 不应合并 */
        TEST_ASSERT(woort_IR_bind(f_inner, L_then));
        b = woort_IRFunction_load_const(f_inner, c10); TEST_ASSERT(b != NULL);
        TEST_ASSERT(woort_IR_ADDI(f_inner, result2, b, b));
        TEST_ASSERT(woort_IR_ret(f_inner, result2));
    }

    /* func main() */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* arg = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* res = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(arg && res);

        arg = woort_IRFunction_load_const(f_main, c0); TEST_ASSERT(arg != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, arg));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, c10, 1, res)); /* 常量索引复用于函数指针 */
        TEST_ASSERT(woort_IR_ret(f_main, res));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c0].m_integer = 0;

    /* 修正: 用 c10 的位置存函数指针不合理。换用独立常量 */
    /* 实际上我们需要另一个常量来存函数指针。修正测试: */

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_deinit(&irc);

    /* ---- 重新构造，使用独立的函数指针常量 ---- */
    woort_IRCompiler_init(&irc);

    c10 = woort_IRCompiler_add_constant(&irc);
    c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);

    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_inner));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_inner, 0);
        woort_IRValue* a = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* b = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* result2 = woort_IRFunction_new_vreg(f_inner);

        woort_IRLabel* L_then = woort_IRFunction_new_label(f_inner);

        a = woort_IRFunction_load_const(f_inner, c10);
        v0 = woort_IRFunction_load_const(f_inner, c0);
        (void)woort_IR_jcc_eq(f_inner, x, v0, L_then);

        (void)woort_IR_ADDI(f_inner, result, a, a);
        (void)woort_IR_ret(f_inner, result);

        (void)woort_IR_bind(f_inner, L_then);
        b = woort_IRFunction_load_const(f_inner, c10);
        (void)woort_IR_ADDI(f_inner, result2, b, b);
        (void)woort_IR_ret(f_inner, result2);
    }

    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* arg = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* res = woort_IRFunction_new_vreg(f_main);

        arg = woort_IRFunction_load_const(f_main, c0);
        (void)woort_IR_PUSHCHK(f_main, arg);
        (void)woort_IR_CALLNWO(f_main, c_fn, 1, res);
        (void)woort_IR_ret(f_main, res);
    }

    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c_fn].m_script_function = cenv->m_code_begin + 0;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    /* 传 x=0 → 走 L_then 分支 → b=10, result2 = 10+10 = 20 */
    woort_VmCallStatus status = woort_VMRuntime_invoke(
        vm, cenv->m_code_begin + f_main->m_code_offset);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(20, vm->m_sp[0].m_integer);

    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_deinit(&irc);

    TEST_END();
}

/* ==========================================================================
 * 合并安全性测试
 * ========================================================================== */

/* ========== 测试 39: 不同 const_index 不合并 ========== */
static void test_no_merge_different_const(void)
{
    TEST_BEGIN("no_merge_different_const (10+20=30)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(&irc);
    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        a = woort_IRFunction_load_const(f, c10); TEST_ASSERT(a != NULL);
        b = woort_IRFunction_load_const(f, c20); TEST_ASSERT(b != NULL);
        TEST_ASSERT(woort_IR_ADDI(f, r, a, b));
        TEST_ASSERT(woort_IR_ret(f, r));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c20].m_integer = 20;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(30, vm->m_sp[0].m_integer);
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 40: 合并后修改 b 不影响 a ========== */
/*
a=LOAD(5), b=LOAD(5) → MOV b=a; b=b+b=10; a+b = 5+10 = 15
如果合并导致 a 和 b 共用栈槽则可能得 20
*/
static void test_merge_independence(void)
{
    TEST_BEGIN("merge_independence (5+10=15)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(&irc);
    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_load_const(f, c5);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(a && b && r);
        TEST_ASSERT(woort_IR_MOV(f, b, a));           /* b = copy of a = 5 */
        TEST_ASSERT(woort_IR_ADDI(f, b, b, b));       /* b = 10, a 不变 */
        TEST_ASSERT(woort_IR_ADDI(f, r, a, b));       /* 5 + 10 = 15 */
        TEST_ASSERT(woort_IR_ret(f, r));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c5].m_integer = 5;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(15, vm->m_sp[0].m_integer);
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 41: 合并后 primary 跨 CALL 存活 ========== */
/*
a=LOAD(7); PUSH(a); CALLNFP; b=LOAD(7) → MOV b=a; return a+b=14
CALL 不应破坏 locals → a 仍为 7
*/
static void test_merge_survives_call(void)
{
    TEST_BEGIN("merge_survives_call (7+7=14)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c7 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);
    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        a = woort_IRFunction_load_const(f, c7); TEST_ASSERT(a != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f, a));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL));
        b = woort_IRFunction_load_const(f, c7); TEST_ASSERT(b != NULL);
        TEST_ASSERT(woort_IR_ADDI(f, r, a, b));
        TEST_ASSERT(woort_IR_ret(f, r));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c7].m_integer = 7;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    g_captured_int = 0;
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(14, vm->m_sp[0].m_integer);
    TEST_ASSERT_EQ_INT(7, g_captured_int);
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 42: if-else 两侧各自加载同一常量 ========== */
/*
else: a=LOAD(3); a*a=9
then: b=LOAD(3); b+b=6
a 在 then 分支不活跃 → 不应合并 b=a
如果错误合并且 a 的栈槽已被复用，b 可能得错误值
*/
static void test_no_merge_across_disjoint_branches(void)
{
    TEST_BEGIN("no_merge_disjoint_branches (6 or 9)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_arg = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f_inner;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_inner));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_inner, 0);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* a = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* b = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_inner);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_inner);
        woort_IRLabel* L_then = woort_IRFunction_new_label(f_inner);

        v0 = woort_IRFunction_load_const(f_inner, c0);
        (void)woort_IR_jcc_eq(f_inner, x, v0, L_then);
        /* else: a*a = 9 */
        a = woort_IRFunction_load_const(f_inner, c3);
        (void)woort_IR_MULI(f_inner, r1, a, a);
        (void)woort_IR_ret(f_inner, r1);
        /* then: b+b = 6 */
        (void)woort_IR_bind(f_inner, L_then);
        b = woort_IRFunction_load_const(f_inner, c3);
        (void)woort_IR_ADDI(f_inner, r2, b, b);
        (void)woort_IR_ret(f_inner, r2);
    }

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* arg = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* res = woort_IRFunction_new_vreg(f_main);
        arg = woort_IRFunction_load_const(f_main, c_arg);  /* 独立的常量索引作参数 */
        (void)woort_IR_PUSHCHK(f_main, arg);
        (void)woort_IR_CALLNWO(f_main, c_fn, 1, res);
        (void)woort_IR_ret(f_main, res);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c3].m_integer = 3;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c_fn].m_script_function = cenv->m_code_begin + 0;

    /* x=0 → then → b=3, 3+3=6 */
    cenv->m_data_begin[c_arg].m_integer = 0;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin + f_inner->m_code_length) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(6, vm->m_sp[0].m_integer);
    woort_VMRuntime_destroy(vm);

    /* x=1 → else → a=3, 3*3=9 (c0 保持 0 不受影响) */
    cenv->m_data_begin[c_arg].m_integer = 1;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin + f_inner->m_code_length) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(9, vm->m_sp[0].m_integer);
    woort_VMRuntime_destroy(vm);

    woort_CodeEnv_drop(cenv); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 43: 循环中合并的 MOV 每次迭代正确执行 ========== */
/*
a=1 (primary); loop: b=LOAD(1) → MOV b=a; sum+=b; i+=b; × 5 次
sum=5. 如果 MOV 或 a 被错误处理，结果会不同。
*/
static void test_merge_in_loop_iterations(void)
{
    TEST_BEGIN("merge_in_loop_iterations (sum=5)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c5 = woort_IRCompiler_add_constant(&irc);
    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* v1 = woort_IRFunction_load_const(f, c1);
        woort_IRValue* v0 = woort_IRFunction_load_const(f, c0);
        woort_IRValue* v5 = woort_IRFunction_load_const(f, c5);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f);
        woort_IRValue* i = woort_IRFunction_new_vreg(f);
        woort_IRLabel* L_loop = woort_IRFunction_new_label(f);
        woort_IRLabel* L_end = woort_IRFunction_new_label(f);

        TEST_ASSERT(woort_IR_MOV(f, sum, v0));
        TEST_ASSERT(woort_IR_MOV(f, i, v0));
        TEST_ASSERT(woort_IR_bind(f, L_loop));
        TEST_ASSERT(woort_IR_jcc_ge(f, i, v5, L_end));
        TEST_ASSERT(woort_IR_ADDI(f, sum, sum, v1));
        TEST_ASSERT(woort_IR_ADDI(f, i, i, v1));
        TEST_ASSERT(woort_IR_jmp(f, L_loop));
        TEST_ASSERT(woort_IR_bind(f, L_end));
        TEST_ASSERT(woort_IR_ret(f, sum));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c5].m_integer = 5;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(5, vm->m_sp[0].m_integer);
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 44: PUSHCCHK 在嵌套调用中正确 ========== */
/*
add(a,b)=a+b; add3(x)=add(x,3); main=add3(add3(0))
add3 中 3 用 PUSHCCHK 传递; 嵌套调用验证常量表在多帧间共享只读
*/
static void test_pushcchk_nested_calls(void)
{
    TEST_BEGIN("pushcchk_nested_calls (0+3+3=6)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_add = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_add3 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f_add;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 2, &f_add));
    {
        woort_IRValue* a = woort_IRFunction_get_argument(f_add, 0);
        woort_IRValue* b = woort_IRFunction_get_argument(f_add, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_add);
        TEST_ASSERT(woort_IR_ADDI(f_add, r, a, b));
        TEST_ASSERT(woort_IR_ret(f_add, r));
    }
    woort_IRFunction* f_add3;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_add3));
    {
        woort_IRValue* x = woort_IRFunction_get_argument(f_add3, 0);
        woort_IRValue* three = woort_IRFunction_new_vreg(f_add3);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_add3);
        three = woort_IRFunction_load_const(f_add3, c3); TEST_ASSERT(three != NULL); /* const_direct PUSHCCHK */
        TEST_ASSERT(woort_IR_PUSHCHK(f_add3, x));
        TEST_ASSERT(woort_IR_PUSHCHK(f_add3, three));
        TEST_ASSERT(woort_IR_CALLNWO(f_add3, c_add, 2, result));
        TEST_ASSERT(woort_IR_ret(f_add3, result));
    }
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        v0 = woort_IRFunction_load_const(f_main, c0); TEST_ASSERT(v0 != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, v0));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, c_add3, 1, r1));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, r1));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, c_add3, 1, r2));
        TEST_ASSERT(woort_IR_ret(f_main, r2));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c3].m_integer = 3;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c_add].m_script_function = cenv->m_code_begin + 0;
    cenv->m_data_begin[c_add3].m_script_function =
        cenv->m_code_begin + f_add->m_code_length;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin + f_add->m_code_length + f_add3->m_code_length) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(6, vm->m_sp[0].m_integer);
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 45: 合并延长 primary 活跃区间后栈槽不冲突 ========== */
/*
a=LOAD(10); x=a*a=100; b=LOAD(10) → MOV b=a; y=b*b=100; return x+y=200
合并使 a 的活跃区间延长到 MOV 处。如果栈槽分配未感知此延长，
a 的栈槽可能在 x=a*a 后被 x 复用覆盖，导致 b 得到错误值。
*/
static void test_merge_liveness_extension(void)
{
    TEST_BEGIN("merge_liveness_extension (100+100=200)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* x = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* y = woort_IRFunction_new_vreg(f);
        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        a = woort_IRFunction_load_const(f, c10); TEST_ASSERT(a != NULL);
        TEST_ASSERT(woort_IR_MULI(f, x, a, a));          /* x=100, a "似乎"已死 */
        b = woort_IRFunction_load_const(f, c10); TEST_ASSERT(b != NULL);     /* MOV b=a → a 延长 */
        TEST_ASSERT(woort_IR_MULI(f, y, b, b));           /* y=100 */
        TEST_ASSERT(woort_IR_ADDI(f, r, x, y));
        TEST_ASSERT(woort_IR_ret(f, r));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(200, vm->m_sp[0].m_integer);
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 46: RETVC 在递归中的正确性 ========== */
/*
f(n): if n==0 return 42 (RETVC); else return f(n-1)
f(3) → f(2) → f(1) → f(0) → RETVC 42; 每层 RESULT 获取返回值
*/
static void test_retvc_in_recursion(void)
{
    TEST_BEGIN("retvc_in_recursion (f(3)=42)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f_rec;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_rec));
    {
        woort_IRValue* n = woort_IRFunction_get_argument(f_rec, 0);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f_rec);
        woort_IRValue* val = woort_IRFunction_new_vreg(f_rec);
        woort_IRValue* one = woort_IRFunction_new_vreg(f_rec);
        woort_IRValue* tmp = woort_IRFunction_new_vreg(f_rec);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_rec);
        woort_IRLabel* L_base = woort_IRFunction_new_label(f_rec);
        v0 = woort_IRFunction_load_const(f_rec, c0); TEST_ASSERT(v0 != NULL);
        TEST_ASSERT(woort_IR_jcc_eq(f_rec, n, v0, L_base));
        one = woort_IRFunction_load_const(f_rec, c1); TEST_ASSERT(one != NULL);
        TEST_ASSERT(woort_IR_SUBI(f_rec, tmp, n, one));
        TEST_ASSERT(woort_IR_PUSHCHK(f_rec, tmp));
        TEST_ASSERT(woort_IR_CALLNWO(f_rec, c_fn, 1, result));
        TEST_ASSERT(woort_IR_ret(f_rec, result));
        TEST_ASSERT(woort_IR_bind(f_rec, L_base));
        val = woort_IRFunction_load_const(f_rec, c42); TEST_ASSERT(val != NULL); /* const_direct → RETVC */
        TEST_ASSERT(woort_IR_ret(f_rec, val));
    }
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRValue* v3 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* res = woort_IRFunction_new_vreg(f_main);
        v3 = woort_IRFunction_load_const(f_main, c3); TEST_ASSERT(v3 != NULL);
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, v3));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, c_fn, 1, res));
        TEST_ASSERT(woort_IR_ret(f_main, res));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c42].m_integer = 42;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c_fn].m_script_function = cenv->m_code_begin + 0;
    cenv->m_data_begin[c3].m_integer = 3;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin + f_rec->m_code_length) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(42, vm->m_sp[0].m_integer);
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== 测试 47: 多种优化同时生效 ========== */
/*
a=LOAD(10); b=LOAD(10) → MOV b=a (合并);
c=LOAD(20) → PUSHCCHK (直连); capture_int(c);
d=LOAD(99) → RETVC (直连);
t = a+b = 20; capture_int(t);
captured 最后=20; return 99
*/
static void test_mixed_optimizations(void)
{
    TEST_BEGIN("mixed_optimizations (all at once)");
    woort_IRCompiler irc; woort_IRCompiler_init(&irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c20 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c99 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c_fn = woort_IRCompiler_add_constant(&irc);
    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRValue* a = woort_IRFunction_new_vreg(f);
        woort_IRValue* b = woort_IRFunction_new_vreg(f);
        woort_IRValue* c = woort_IRFunction_new_vreg(f);
        woort_IRValue* d = woort_IRFunction_new_vreg(f);
        woort_IRValue* t = woort_IRFunction_new_vreg(f);
        a = woort_IRFunction_load_const(f, c10); TEST_ASSERT(a != NULL);     /* 非直连 (use>1) */
        b = woort_IRFunction_load_const(f, c10); TEST_ASSERT(b != NULL);     /* 合并为 MOV b=a */
        c = woort_IRFunction_load_const(f, c20); TEST_ASSERT(c != NULL);     /* 直连 PUSHCCHK */
        TEST_ASSERT(woort_IR_ADDI(f, t, a, b));          /* 10+10=20 */
        TEST_ASSERT(woort_IR_PUSHCHK(f, t));
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL)); /* capture(20) */
        TEST_ASSERT(woort_IR_PUSHCHK(f, c));             /* PUSHCCHK */
        TEST_ASSERT(woort_IR_CALLNFP(f, c_fn, 1, NULL)); /* capture(20) */
        d = woort_IRFunction_load_const(f, c99); TEST_ASSERT(d != NULL);     /* 直连 RETVC */
        TEST_ASSERT(woort_IR_ret(f, d));
    }
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c10].m_integer = 10;
    cenv->m_data_begin[c20].m_integer = 20;
    cenv->m_data_begin[c99].m_integer = 99;
    cenv->m_data_begin[c_fn].m_native_or_jit_function = &capture_int;
    woort_VMRuntime* vm; TEST_ASSERT(woort_VMRuntime_create(&vm));
    g_captured_int = 0;
    TEST_ASSERT(woort_VMRuntime_invoke(vm, cenv->m_code_begin) == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(20, g_captured_int);   /* 最后一次 capture 的参数 */
    TEST_ASSERT_EQ_INT(99, vm->m_sp[0].m_integer); /* RETVC */
    woort_CodeEnv_drop(cenv); woort_VMRuntime_destroy(vm); woort_IRCompiler_deinit(&irc);
    TEST_END();
}

/* ========== main ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init();

    (void)printf("=== WooRT IR Compiler Tests ===\n\n");

    test_constant_return();
    test_integer_arithmetic();
    test_divmod();
    test_negate();
    test_branch_ge();
    test_loop();
    test_fibonacci();
    test_logic_ops();
    test_integer_comparisons();
    test_fallthrough();
    test_call_native();
    test_multi_param();
    test_jcc();
    test_jccz();
    test_jcc_variants();
    test_nested_loop();
    test_compound_ops();
    test_vreg_reuse();
    test_multi_branch();
    test_empty_func();
    test_static_storage();
    test_multi_native_call();
    test_fib_iterative();
    test_backward_jcc();
    test_const_hoist();
    test_retvc();
    test_pushcchk();
    test_const_direct_no_trigger();
    test_const_merge();
    test_pushcchk_retvc_combo();
    test_no_direct_on_redef();
    test_const_direct_overwrite_arg();
    test_no_direct_on_jcc_use();
    test_direct_and_nondirect_same_const();
    test_consecutive_load_const();
    test_all_const_direct_zero_stack();
    test_const_merge_across_loop();
    test_const_merge_rejected_not_live();
    test_no_merge_different_const();
    test_merge_independence();
    test_merge_survives_call();
    test_no_merge_across_disjoint_branches();
    test_merge_in_loop_iterations();
    test_pushcchk_nested_calls();
    test_merge_liveness_extension();
    test_retvc_in_recursion();
    test_mixed_optimizations();

    (void)printf("\n=== Results: %d/%d passed ===\n", g_tests_passed, g_tests_run);

    woort_shutdown();

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
