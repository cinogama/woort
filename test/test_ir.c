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
        (void)printf("  [TEST] %-40s ", _test_name);

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

static woort_api capture_int(woort_vm vm, woort_value* args)
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
        woort_IRValue* v = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v != NULL);

        TEST_ASSERT(woort_IR_LOAD_CONST(f, v, c42));
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

        TEST_ASSERT(woort_IR_LOAD_CONST(f, a, const_a));
        TEST_ASSERT(woort_IR_LOAD_CONST(f, b, const_b));
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

        TEST_ASSERT(woort_IR_LOAD_CONST(f, v17, c17));
        TEST_ASSERT(woort_IR_LOAD_CONST(f, v5, c5));
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

        TEST_ASSERT(woort_IR_LOAD_CONST(f, v42, c42));
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

        (void)woort_IR_LOAD_CONST(f, va, ca);
        (void)woort_IR_LOAD_CONST(f, vb, cb);

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
        TEST_ASSERT(woort_IR_LOAD_CONST(f, vn, cn));
        TEST_ASSERT(woort_IR_LOAD_CONST(f, val0, c0));
        TEST_ASSERT(woort_IR_LOAD_CONST(f, val1, c1));

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

        TEST_ASSERT(woort_IR_LOAD_CONST(f_fib, v2, c2));
        TEST_ASSERT(woort_IR_LOAD_CONST(f_fib, v1, c1));

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

        TEST_ASSERT(woort_IR_LOAD_CONST(f_main, vn, cn));
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

        TEST_ASSERT(woort_IR_LOAD_CONST(f, v1, c1));
        TEST_ASSERT(woort_IR_LOAD_CONST(f, v0, c0));

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

        TEST_ASSERT(woort_IR_LOAD_CONST(f, a, ca));
        TEST_ASSERT(woort_IR_LOAD_CONST(f, b, cb));

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

        TEST_ASSERT(woort_IR_LOAD_CONST(f, v99, c99));

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
        woort_IRValue* v = woort_IRFunction_new_vreg(f);
        TEST_ASSERT(v != NULL);

        TEST_ASSERT(woort_IR_LOAD_CONST(f, v, c_val));
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

    (void)printf("\n=== Results: %d/%d passed ===\n", g_tests_passed, g_tests_run);

    woort_shutdown();

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
