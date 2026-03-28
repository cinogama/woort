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
    return 42;      // 使用 RETVC（常量返回）
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* v = woort_IRFuntion_load_constant(f, c42);
        TEST_ASSERT(v != NULL);

        woort_IRBlock_ret(entry, v);
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
func arith(a: int, b: int) => int {
    return (a + b) * (a - b);       // (10+3) * (10-3) = 13 * 7 = 91
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* a = woort_IRFuntion_load_constant(f, const_a);
        woort_IRValue* b = woort_IRFuntion_load_constant(f, const_b);
        TEST_ASSERT(a != NULL && b != NULL);

        woort_IRValue* sum = woort_IRBlock_ADDI(entry, a, b);
        woort_IRValue* diff = woort_IRBlock_SUBI(entry, a, b);
        TEST_ASSERT(sum != NULL && diff != NULL);

        woort_IRValue* product = woort_IRBlock_MULI(entry, sum, diff);
        TEST_ASSERT(product != NULL);

        woort_IRBlock_ret(entry, product);
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* v17 = woort_IRFuntion_load_constant(f, c17);
        woort_IRValue* v5 = woort_IRFuntion_load_constant(f, c5);
        TEST_ASSERT(v17 != NULL && v5 != NULL);

        woort_IRValue* div_result = woort_IRBlock_DIVI(entry, v17, v5);
        woort_IRValue* mod_result = woort_IRBlock_MODI(entry, v17, v5);
        TEST_ASSERT(div_result != NULL && mod_result != NULL);

        woort_IRValue* sum = woort_IRBlock_ADDI(entry, div_result, mod_result);
        TEST_ASSERT(sum != NULL);

        woort_IRBlock_ret(entry, sum);
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* v42 = woort_IRFuntion_load_constant(f, c42);
        TEST_ASSERT(v42 != NULL);

        woort_IRValue* neg = woort_IRBlock_NEGI(entry, v42);
        TEST_ASSERT(neg != NULL);

        woort_IRBlock_ret(entry, neg);
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

用 a=7, b=3 测试 -> 期望 7
用 a=2, b=9 测试 -> 期望 9
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        woort_IRBlock* block_true = woort_IRFuntion_add_block(f);
        woort_IRBlock* block_false = woort_IRFuntion_add_block(f);

        woort_IRValue* va = woort_IRFuntion_load_constant(f, ca);
        woort_IRValue* vb = woort_IRFuntion_load_constant(f, cb);

        /* if (a >= b) goto block_true else goto block_false */
        (void)woort_IRBlock_br_ge(entry, va, vb, block_true, block_false);

        /* block_true: return a */
        woort_IRBlock_ret(block_true, va);

        /* block_false: return b */
        woort_IRBlock_ret(block_false, vb);
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

/* ========== 测试 6: 循环 + PHI 节点 (sum 1..N) ========== */
/*
func sum_1_to_n(n: int) => int {
    i = 1;
    acc = 0;
    while (i <= n) {
        acc = acc + i;
        i = i + 1;
    }
    return acc;
}

sum(10) = 55
*/
static void test_loop_phi(void)
{
    TEST_BEGIN("loop_phi (sum 1..10 = 55)");

    woort_IRCompiler irc;
    woort_IRCompiler_init(&irc);

    woort_IRConstantIndex cn = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(&irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(&irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f));
    {
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        woort_IRBlock* loop_header = woort_IRFuntion_add_block(f);
        woort_IRBlock* loop_body = woort_IRFuntion_add_block(f);
        woort_IRBlock* exit_block = woort_IRFuntion_add_block(f);
        TEST_ASSERT(entry && loop_header && loop_body && exit_block);

        woort_IRValue* vn = woort_IRFuntion_load_constant(f, cn);
        woort_IRValue* v0 = woort_IRFuntion_load_constant(f, c0);
        woort_IRValue* v1 = woort_IRFuntion_load_constant(f, c1);
        TEST_ASSERT(vn && v0 && v1);

        /* entry -> loop_header */
        TEST_ASSERT(woort_IRBlock_br(entry, loop_header));

        /* loop_header: PHI(i, acc) */
        woort_IRPhi* phi_i;
        woort_IRValue* i_val = woort_IRBlock_PHI(loop_header, &phi_i);
        TEST_ASSERT(i_val != NULL);

        woort_IRPhi* phi_acc;
        woort_IRValue* acc_val = woort_IRBlock_PHI(loop_header, &phi_acc);
        TEST_ASSERT(acc_val != NULL);

        /* i 来自 entry 时为 1，来自 loop_body 时为 i+1 */
        TEST_ASSERT(woort_IRPhi_from(phi_i, entry, v1));
        /* acc 来自 entry 时为 0，来自 loop_body 时为 acc+i */
        TEST_ASSERT(woort_IRPhi_from(phi_acc, entry, v0));

        /* if (i <= n) goto loop_body else goto exit_block */
        TEST_ASSERT(woort_IRBlock_br_le(
            loop_header, i_val, vn, loop_body, exit_block));

        /* loop_body: acc' = acc + i; i' = i + 1; goto loop_header */
        woort_IRValue* new_acc = woort_IRBlock_ADDI(loop_body, acc_val, i_val);
        woort_IRValue* new_i = woort_IRBlock_ADDI(loop_body, i_val, v1);
        TEST_ASSERT(new_acc && new_i);

        TEST_ASSERT(woort_IRPhi_from(phi_i, loop_body, new_i));
        TEST_ASSERT(woort_IRPhi_from(phi_acc, loop_body, new_acc));

        TEST_ASSERT(woort_IRBlock_br(loop_body, loop_header));

        /* exit_block: return acc */
        woort_IRBlock_ret(exit_block, acc_val);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[cn].m_integer = 10;
    cenv->m_data_begin[c0].m_integer = 0;
    cenv->m_data_begin[c1].m_integer = 1;

    dump_Code(cenv);

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

    /* func fib(n): 参数通过压栈传递，arg0 在 [SB+3] */
    woort_IRFunction* f_fib;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 1, &f_fib));
    {
        woort_IRBlock* entry = woort_IRFunction_entry_block(f_fib);
        woort_IRBlock* base_case = woort_IRFuntion_add_block(f_fib);
        woort_IRBlock* recursive = woort_IRFuntion_add_block(f_fib);
        TEST_ASSERT(entry && base_case && recursive);

        woort_IRValue* n_arg = woort_IRFunction_get_argument(f_fib, 0);
        woort_IRValue* v2 = woort_IRFuntion_load_constant(f_fib, c2);
        woort_IRValue* v1 = woort_IRFuntion_load_constant(f_fib, c1);
        TEST_ASSERT(n_arg && v2 && v1);

        /* if (n < 2) goto base_case else goto recursive */
        TEST_ASSERT(woort_IRBlock_br_lt(entry, n_arg, v2, base_case, recursive));

        /* base_case: return n */
        woort_IRBlock_ret(base_case, n_arg);

        /* recursive: return fib(n-1) + fib(n-2) */
        woort_IRValue* n_minus_1 = woort_IRBlock_SUBI(recursive, n_arg, v1);
        woort_IRValue* n_minus_2 = woort_IRBlock_SUBI(recursive, n_arg, v2);
        TEST_ASSERT(n_minus_1 && n_minus_2);

        /* call fib(n-1) */
        woort_IRBlock_PUSHCHK(recursive, n_minus_1);
        woort_IRValue* r1;
        woort_IRBlock_CALLNWO(recursive, cfib, 1, &r1);

        /* call fib(n-2) */
        woort_IRBlock_PUSHCHK(recursive, n_minus_2);
        woort_IRValue* r2;
        woort_IRBlock_CALLNWO(recursive, cfib, 1, &r2);

        woort_IRValue* sum = woort_IRBlock_ADDI(recursive, r1, r2);
        TEST_ASSERT(sum != NULL);

        woort_IRBlock_ret(recursive, sum);
    }

    /* func main(): 调用 fib(10)，不需要捕获返回值 —— 直接用 invoke */
    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(&irc, 0, &f_main));
    {
        woort_IRBlock* entry = woort_IRFunction_entry_block(f_main);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* vn = woort_IRFuntion_load_constant(f_main, cn);
        TEST_ASSERT(vn != NULL);

        woort_IRBlock_PUSHCHK(entry, vn);

        woort_IRValue* result;
        woort_IRBlock_CALLNWO(entry, cfib, 1, &result);

        woort_IRBlock_ret(entry, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(&irc, &cenv));
    cenv->m_data_begin[c2].m_integer = 2;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[cfib].m_script_function = cenv->m_code_begin + 0;
    cenv->m_data_begin[cn].m_integer = 10;

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));

    /*
    main 是第二个函数，其代码紧跟 fib 之后。
    由于我们不知道 fib 的字节码长度，需要从 irc 获取。
    但 finish 后 irc.m_commited_codes 已有全部代码。
    fib 的字节码占据 [0, fib_len)，main 从 fib_len 开始。

    通过遍历 fib 的所有块来统计总长度。
    */
    size_t fib_code_len = 0;
    {
        woort_IRBlock* b;
        for (b = woort_linklist_iter(&f_fib->m_ir_blocks);
             b != NULL;
             b = woort_linklist_next(b))
        {
            fib_code_len += b->m_bytecodes_in_block.m_size;
        }
    }

    woort_VmCallStatus status = woort_VMRuntime_invoke(
        vm, cenv->m_code_begin + fib_code_len);
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* v1 = woort_IRFuntion_load_constant(f, c1);
        woort_IRValue* v0 = woort_IRFuntion_load_constant(f, c0);
        TEST_ASSERT(v1 && v0);

        woort_IRValue* land = woort_IRBlock_LAND(entry, v1, v0);   /* 1 && 0 = 0 */
        woort_IRValue* lnot = woort_IRBlock_LNOT(entry, v0);      /* !0 = 1 */
        woort_IRValue* lor = woort_IRBlock_LOR(entry, v1, v0);    /* 1 || 0 = 1 */
        TEST_ASSERT(land && lnot && lor);

        woort_IRValue* s1 = woort_IRBlock_ADDI(entry, land, lnot);
        woort_IRValue* s2 = woort_IRBlock_ADDI(entry, s1, lor);
        TEST_ASSERT(s1 && s2);

        woort_IRBlock_ret(entry, s2);
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* a = woort_IRFuntion_load_constant(f, ca);
        woort_IRValue* b = woort_IRFuntion_load_constant(f, cb);
        TEST_ASSERT(a && b);

        woort_IRValue* lt = woort_IRBlock_LTI(entry, a, b);   /* 5 < 3 = 0 */
        woort_IRValue* gt = woort_IRBlock_GTI(entry, a, b);   /* 5 > 3 = 1 */
        woort_IRValue* le = woort_IRBlock_LEI(entry, a, b);   /* 5 <= 3 = 0 */
        woort_IRValue* ge = woort_IRBlock_GEI(entry, a, b);   /* 5 >= 3 = 1 */
        woort_IRValue* eq = woort_IRBlock_EQI(entry, a, b);   /* 5 == 3 = 0 */
        woort_IRValue* ne = woort_IRBlock_NEI(entry, a, b);   /* 5 != 3 = 1 */
        TEST_ASSERT(lt && gt && le && ge && eq && ne);

        woort_IRValue* s1 = woort_IRBlock_ADDI(entry, lt, gt);
        woort_IRValue* s2 = woort_IRBlock_ADDI(entry, s1, le);
        woort_IRValue* s3 = woort_IRBlock_ADDI(entry, s2, ge);
        woort_IRValue* s4 = woort_IRBlock_ADDI(entry, s3, eq);
        woort_IRValue* s5 = woort_IRBlock_ADDI(entry, s4, ne);
        TEST_ASSERT(s1 && s2 && s3 && s4 && s5);

        woort_IRBlock_ret(entry, s5);
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
    entry: goto middle
    middle: goto exit
    exit: return 99

所有分支都应该被优化为 fall-through（无跳转指令）。
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        woort_IRBlock* middle = woort_IRFuntion_add_block(f);
        woort_IRBlock* exit_b = woort_IRFuntion_add_block(f);
        TEST_ASSERT(entry && middle && exit_b);

        woort_IRValue* v99 = woort_IRFuntion_load_constant(f, c99);
        TEST_ASSERT(v99 != NULL);

        TEST_ASSERT(woort_IRBlock_br(entry, middle));
        TEST_ASSERT(woort_IRBlock_br(middle, exit_b));
        woort_IRBlock_ret(exit_b, v99);
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
        woort_IRBlock* entry = woort_IRFunction_entry_block(f);
        TEST_ASSERT(entry != NULL);

        woort_IRValue* v = woort_IRFuntion_load_constant(f, c_val);
        TEST_ASSERT(v != NULL);

        woort_IRBlock_PUSHCHK(entry, v);
        woort_IRBlock_CALLNFP(entry, c_fn, 1, NULL);

        woort_IRBlock_ret_void(entry);
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
    test_loop_phi();
    test_fibonacci();
    test_logic_ops();
    test_integer_comparisons();
    test_fallthrough();
    test_call_native();
    

    (void)printf("\n=== Results: %d/%d passed ===\n", g_tests_passed, g_tests_run);

    woort_shutdown();

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
