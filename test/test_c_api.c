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

/* ========== Native 函数辅助 ========== */

static woort_Int g_captured_int = 0;
static woort_Real g_captured_real = 0;
static bool g_captured_bool = false;
static woort_U8CString g_captured_string = NULL;

static woort_api capture_int_fn(void)
{
    g_captured_int = woort_int(0);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api capture_real_fn(void)
{
    g_captured_real = woort_real(0);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api capture_bool_fn(void)
{
    g_captured_bool = woort_bool(0);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api capture_string_fn(void)
{
    g_captured_string = woort_string(0);
    return WOORT_VM_CALL_STATUS_NORMAL;
}

static woort_api return_int_fn(void)
{
    return woort_ret_int(12345);
}

static woort_api return_real_fn(void)
{
    return woort_ret_real(3.14159);
}

static woort_api return_bool_fn(void)
{
    return woort_ret_bool(true);
}

static woort_api sumThree_fn(void)
{
    woort_Int a = woort_int(0);
    woort_Int b = woort_int(1);
    woort_Int c = woort_int(2);
    return woort_ret_int(a + b + c);
}

static woort_api identity_int_fn(void)
{
    woort_Int val = woort_int(0);
    return woort_ret_int(val);
}

static woort_api identity_real_fn(void)
{
    woort_Real val = woort_real(0);
    return woort_ret_real(val);
}

/* ========== 测试 1: Runtime 创建/销毁/Swap ========== */
static void test_vm_runtime_lifecycle(void)
{
    TEST_BEGIN("vm_runtime_lifecycle");

    woort_VMRuntime* vm1;
    TEST_ASSERT(woort_VMRuntime_create(&vm1));
    TEST_ASSERT(vm1 != NULL);

    woort_VMRuntime* prev = woort_VMRuntime_swap(vm1);
    TEST_ASSERT(prev == NULL);

    woort_VMRuntime* vm2;
    TEST_ASSERT(woort_VMRuntime_create(&vm2));

    prev = woort_VMRuntime_swap(vm2);
    TEST_ASSERT(prev == vm1);

    prev = woort_VMRuntime_swap(NULL);
    TEST_ASSERT(prev == vm2);

    woort_VMRuntime_destroy(vm2);
    woort_VMRuntime_destroy(vm1);

    TEST_END();
}

/* ========== 测试 2: IRCompiler add_static ========== */
static void test_ir_add_static(void)
{
    TEST_BEGIN("ir_add_static");

    woort_IRCompiler* irc = woort_IRCompiler_create();
    TEST_ASSERT(irc != NULL);

    woort_IRStaticIndex s0 = woort_IRCompiler_add_static(irc);
    woort_IRStaticIndex s1 = woort_IRCompiler_add_static(irc);
    woort_IRStaticIndex s2 = woort_IRCompiler_add_static(irc);

    TEST_ASSERT(s0 == 0);
    TEST_ASSERT(s1 == 1);
    TEST_ASSERT(s2 == 2);

    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 3: IRFunction new_vreg/get_argument ========== */
static void test_ir_vreg_and_argument(void)
{
    TEST_BEGIN("ir_vreg_and_argument");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 3, &f));

    woort_IRValue* arg0 = woort_IRFunction_get_argument(f, 0);
    woort_IRValue* arg1 = woort_IRFunction_get_argument(f, 1);
    woort_IRValue* arg2 = woort_IRFunction_get_argument(f, 2);

    TEST_ASSERT(arg0 != NULL);
    TEST_ASSERT(arg1 != NULL);
    TEST_ASSERT(arg2 != NULL);

    woort_IRValue* vreg0 = woort_IRFunction_new_vreg(f);
    woort_IRValue* vreg1 = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(vreg0 != NULL);
    TEST_ASSERT(vreg1 != NULL);
    TEST_ASSERT(vreg0 != vreg1);

    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 4: IRFunction new_label/bind ========== */
static void test_ir_label_and_bind(void)
{
    TEST_BEGIN("ir_label_and_bind");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRLabel* L1 = woort_IRFunction_new_label(f);
    woort_IRLabel* L2 = woort_IRFunction_new_label(f);
    TEST_ASSERT(L1 != NULL);
    TEST_ASSERT(L2 != NULL);
    TEST_ASSERT(L1 != L2);

    woort_IRValue* v0 = (woort_IRValue*)woort_IRFunction_load_const(f, c0);
    TEST_ASSERT(v0 != NULL);

    TEST_ASSERT(woort_IR_jmp(f, L1));
    TEST_ASSERT(woort_IR_bind(f, L2));
    TEST_ASSERT(woort_IR_ret(f, v0));
    TEST_ASSERT(woort_IR_bind(f, L1));
    TEST_ASSERT(woort_IR_jmp(f, L2));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c0].m_integer = 42;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

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

/* ========== 测试 5: SourceLocation push/pop ========== */
static void test_ir_srcloc_push_pop(void)
{
    TEST_BEGIN("ir_srcloc_push_pop");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    const char* path = woort_IRCompiler_intern_string(irc, "test.woo");
    TEST_ASSERT(path != NULL);

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, &f));

    woort_IRValue* arg = woort_IRFunction_get_argument(f, 0);
    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(arg != NULL && r != NULL);

    TEST_ASSERT(woort_IRFunction_push_srcloc(f, path, 1, 1, 1, 10));
    const woort_IRValue* v0 = woort_IRFunction_load_const(f, c0);
    TEST_ASSERT(v0 != NULL);
    TEST_ASSERT(woort_IR_ADDI(f, r, arg, v0));
    woort_IRFunction_pop_srcloc(f);

    TEST_ASSERT(woort_IRFunction_push_srcloc(f, path, 2, 1, 2, 15));
    TEST_ASSERT(woort_IR_ret(f, r));
    woort_IRFunction_pop_srcloc(f);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c0, 1);
    woort_CodeEnv_unlock(cenv);

    TEST_ASSERT(cenv->m_source_map.m_entry_count >= 2);

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 6: MOV/LOAD/STORE 指令 ========== */
static void test_ir_mov_load_store(void)
{
    TEST_BEGIN("ir_mov_load_store");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRStaticIndex s0 = woort_IRCompiler_add_static(irc);
    woort_IRConstantIndex c10 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* v10 = (woort_IRValue*)woort_IRFunction_load_const(f, c10);
    woort_IRValue* r1 = woort_IRFunction_new_vreg(f);
    woort_IRValue* r2 = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(v10 != NULL && r1 != NULL && r2 != NULL);

    TEST_ASSERT(woort_IR_STORE(f, s0, v10));
    TEST_ASSERT(woort_IR_LOAD(f, r1, s0));
    TEST_ASSERT(woort_IR_MOV(f, r2, r1));
    TEST_ASSERT(woort_IR_ret(f, r2));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c10].m_integer = 10;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

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

/* ========== 测试 7: PUSHCHK/POP/POPR 指令 ========== */
static void test_ir_stack_ops(void)
{
    TEST_BEGIN("ir_stack_ops_pushchk_pop_popr");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_add;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 2, &f_add));
    {
        woort_IRValue* a = woort_IRFunction_get_argument(f_add, 0);
        woort_IRValue* b = woort_IRFunction_get_argument(f_add, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_add);
        TEST_ASSERT(woort_IR_ADDI(f_add, r, a, b));
        TEST_ASSERT(woort_IR_ret(f_add, r));
    }

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f_main));
    {
        woort_IRValue* v1 = (woort_IRValue*)woort_IRFunction_load_const(f_main, c1);
        woort_IRValue* v2 = (woort_IRValue*)woort_IRFunction_load_const(f_main, c2);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);

        TEST_ASSERT(woort_IR_PUSHCHK(f_main, v1));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, v2));
        TEST_ASSERT(woort_IR_CALLNWO(f_main, cfn, 2, r));
        TEST_ASSERT(woort_IR_ret(f_main, r));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c1].m_integer = 10;
    cenv->m_data_begin[c2].m_integer = 20;
    cenv->m_data_begin[cfn].m_script_function = cenv->m_code_begin;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin + f_add->m_code_length);

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

/* ========== 测试 8: 类型转换 ITOR/RTOI/ITOS/RTOS ========== */
static void test_ir_type_conversions(void)
{
    TEST_BEGIN("ir_type_conversions_itor_rtoi_itos_rtos");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_int_val = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_real_val = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* v_int = (woort_IRValue*)woort_IRFunction_load_const(f, c_int_val);
    woort_IRValue* v_real = (woort_IRValue*)woort_IRFunction_load_const(f, c_real_val);
    woort_IRValue* r1 = woort_IRFunction_new_vreg(f);
    woort_IRValue* r2 = woort_IRFunction_new_vreg(f);
    woort_IRValue* r3 = woort_IRFunction_new_vreg(f);
    woort_IRValue* r4 = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(v_int && v_real && r1 && r2 && r3 && r4);

    TEST_ASSERT(woort_IR_ITOR(f, r1, v_int));
    TEST_ASSERT(woort_IR_RTOI(f, r2, v_real));
    TEST_ASSERT(woort_IR_ITOS(f, r3, v_int));
    TEST_ASSERT(woort_IR_RTOS(f, r4, v_real));

    TEST_ASSERT(woort_IR_ret(f, r2));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c_int_val].m_integer = 42;
    cenv->m_data_begin[c_real_val].m_real = 9.87;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

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

/* ========== 测试 9: 实数运算 ADD/SUB/MUL/DIV/MOD/NEG ========== */
static void test_ir_real_arithmetic(void)
{
    TEST_BEGIN("ir_real_arithmetic_add_sub_mul_div_mod_neg");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* a = (woort_IRValue*)woort_IRFunction_load_const(f, c_a);
    woort_IRValue* b = (woort_IRValue*)woort_IRFunction_load_const(f, c_b);
    woort_IRValue* r_add = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_sub = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_mul = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_div = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_neg = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(a && b && r_add && r_sub && r_mul && r_div && r_neg);

    TEST_ASSERT(woort_IR_ADDR(f, r_add, a, b));
    TEST_ASSERT(woort_IR_SUBR(f, r_sub, a, b));
    TEST_ASSERT(woort_IR_MULR(f, r_mul, a, b));
    TEST_ASSERT(woort_IR_DIVR(f, r_div, a, b));
    TEST_ASSERT(woort_IR_NEGR(f, r_neg, a));

    TEST_ASSERT(woort_IR_ret(f, r_div));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c_a].m_real = 10.0;
    cenv->m_data_begin[c_b].m_real = 3.0;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_REAL(10.0 / 3.0, woort_real(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 10: 实数比较 LT/GT/LE/GE/EQ/NE ========== */
static void test_ir_real_comparisons(void)
{
    TEST_BEGIN("ir_real_comparisons_lt_gt_le_ge_eq_ne");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* a = (woort_IRValue*)woort_IRFunction_load_const(f, c_a);
    woort_IRValue* b = (woort_IRValue*)woort_IRFunction_load_const(f, c_b);
    woort_IRValue* r_lt = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_gt = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_le = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_ge = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_eq = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_ne = woort_IRFunction_new_vreg(f);
    woort_IRValue* sum = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(a && b && r_lt && r_gt && r_le && r_ge && r_eq && r_ne && sum);

    TEST_ASSERT(woort_IR_LTR(f, r_lt, a, b));
    TEST_ASSERT(woort_IR_GTR(f, r_gt, a, b));
    TEST_ASSERT(woort_IR_LER(f, r_le, a, b));
    TEST_ASSERT(woort_IR_GER(f, r_ge, a, b));
    TEST_ASSERT(woort_IR_EQR(f, r_eq, a, b));
    TEST_ASSERT(woort_IR_NER(f, r_ne, a, b));

    TEST_ASSERT(woort_IR_ADDI(f, sum, r_lt, r_gt));
    TEST_ASSERT(woort_IR_ADDI(f, sum, sum, r_le));
    TEST_ASSERT(woort_IR_ADDI(f, sum, sum, r_ge));
    TEST_ASSERT(woort_IR_ADDI(f, sum, sum, r_eq));
    TEST_ASSERT(woort_IR_ADDI(f, sum, sum, r_ne));

    TEST_ASSERT(woort_IR_ret(f, sum));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c_a].m_real = 5.0;
    cenv->m_data_begin[c_b].m_real = 3.0;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(3, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 11: 字符串比较 LTS/GTS/LES/GES/EQS/NES ========== */
static void test_ir_string_comparisons(void)
{
    TEST_BEGIN("ir_string_comparisons_lt_gt_le_ge_eq_ne");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_s1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_s2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* s1 = (woort_IRValue*)woort_IRFunction_load_const(f, c_s1);
    woort_IRValue* s2 = (woort_IRValue*)woort_IRFunction_load_const(f, c_s2);
    woort_IRValue* r_lt = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_gt = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_eq = woort_IRFunction_new_vreg(f);
    woort_IRValue* sum = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(s1 && s2 && r_lt && r_gt && r_eq && sum);

    TEST_ASSERT(woort_IR_LTS(f, r_lt, s1, s2));
    TEST_ASSERT(woort_IR_GTS(f, r_gt, s1, s2));
    TEST_ASSERT(woort_IR_EQS(f, r_eq, s1, s2));

    TEST_ASSERT(woort_IR_ADDI(f, sum, r_lt, r_gt));
    TEST_ASSERT(woort_IR_ADDI(f, sum, sum, r_eq));

    TEST_ASSERT(woort_IR_ret(f, sum));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_string(cenv, c_s1, "apple");
    woort_CodeEnv_set_const_string(cenv, c_s2, "banana");
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(1, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 12: 字符串连接 ADDS ========== */
static void test_ir_string_concat(void)
{
    TEST_BEGIN("ir_string_concat_adds");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_s1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_s2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* s1 = (woort_IRValue*)woort_IRFunction_load_const(f, c_s1);
    woort_IRValue* s2 = (woort_IRValue*)woort_IRFunction_load_const(f, c_s2);
    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(s1 && s2 && r);

    TEST_ASSERT(woort_IR_ADDS(f, r, s1, s2));
    TEST_ASSERT(woort_IR_ret(f, r));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_string(cenv, c_s1, "Hello, ");
    woort_CodeEnv_set_const_string(cenv, c_s2, "World!");
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    const char* result = woort_string(sv + 1);
    TEST_ASSERT(result != NULL && strcmp(result, "Hello, World!") == 0);
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 13: 逻辑运算 LAND/LOR/LNOT ========== */
static void test_ir_logical_ops(void)
{
    TEST_BEGIN("ir_logical_ops_land_lor_lnot");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_t = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* t = (woort_IRValue*)woort_IRFunction_load_const(f, c_t);
    woort_IRValue* fv = (woort_IRValue*)woort_IRFunction_load_const(f, c_f);
    woort_IRValue* r_land = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_lor = woort_IRFunction_new_vreg(f);
    woort_IRValue* r_lnot = woort_IRFunction_new_vreg(f);
    woort_IRValue* sum = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(t && fv && r_land && r_lor && r_lnot && sum);

    TEST_ASSERT(woort_IR_LAND(f, r_land, t, fv));
    TEST_ASSERT(woort_IR_LOR(f, r_lor, t, fv));
    TEST_ASSERT(woort_IR_LNOT(f, r_lnot, fv));

    TEST_ASSERT(woort_IR_ADDI(f, sum, r_land, r_lor));
    TEST_ASSERT(woort_IR_ADDI(f, sum, sum, r_lnot));

    TEST_ASSERT(woort_IR_ret(f, sum));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c_t].m_integer = 1;
    cenv->m_data_begin[c_f].m_integer = 0;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

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

/* ========== 测试 14: 控制流 jcc/jccz ========== */
static void test_ir_jcc_jccz(void)
{
    TEST_BEGIN("ir_jcc_jccz_conditional_branch");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_x = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* x = (woort_IRValue*)woort_IRFunction_load_const(f, c_x);
    woort_IRValue* neg = woort_IRFunction_new_vreg(f);
    woort_IRValue* cond = woort_IRFunction_new_vreg(f);
    woort_IRLabel* L_neg = woort_IRFunction_new_label(f);
    TEST_ASSERT(x && neg && cond && L_neg);

    TEST_ASSERT(woort_IR_NEGI(f, neg, x));
    TEST_ASSERT(woort_IR_LTI(f, cond, x, woort_IRFunction_load_const(f, woort_IRCompiler_add_constant(irc))));

    woort_IRValue* zero = (woort_IRValue*)woort_IRFunction_load_const(f, c_x);
    (void)woort_IR_LTI(f, cond, x, zero);
    (void)woort_IR_jcc(f, cond, L_neg);
    (void)woort_IR_ret(f, x);
    (void)woort_IR_bind(f, L_neg);
    (void)woort_IR_ret(f, neg);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c_x].m_integer = 0;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

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

/* ========== 测试 15: jcc_lt/le/gt/ge/eq/ne ========== */
static void test_ir_jcc_variants(void)
{
    TEST_BEGIN("ir_jcc_lt_le_gt_ge_eq_ne");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* a = (woort_IRValue*)woort_IRFunction_load_const(f, c_a);
    woort_IRValue* b = (woort_IRValue*)woort_IRFunction_load_const(f, c_b);
    woort_IRLabel* L_exit = woort_IRFunction_new_label(f);
    TEST_ASSERT(a && b && L_exit);

    (void)woort_IR_jcc_gt(f, a, b, L_exit);
    (void)woort_IR_ret(f, b);

    (void)woort_IR_bind(f, L_exit);
    (void)woort_IR_ret(f, a);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c_a].m_integer = 5;
    cenv->m_data_begin[c_b].m_integer = 3;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(5, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 16: WOORT_VM_CALL_STATUS 相关 ========== */
static void test_vm_call_status(void)
{
    TEST_BEGIN("vm_call_status_yield_abort_resume");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f_main));
    {
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, woort_IRCompiler_add_constant(irc))));
        TEST_ASSERT(woort_IR_CALLNFP(f_main, cfn, 1, r));
        TEST_ASSERT(woort_IR_ret(f_main, r));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[cfn].m_native_function = &return_int_fn;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(12345, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 17: CodeEnv query_function/lock/unlock ========== */
static void test_codeenv_query_and_lock(void)
{
    TEST_BEGIN("codeenv_query_function_lock_unlock");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f1;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f1));
    {
        woort_IRValue* v = woort_IRFunction_new_vreg(f1);
        TEST_ASSERT(v != NULL);
        TEST_ASSERT(woort_IR_ret(f1, (woort_IRValue*)woort_IRFunction_load_const(f1, woort_IRCompiler_add_constant(irc))));
    }

    woort_IRFunction* f2;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f2));
    {
        woort_IRValue* v = woort_IRFunction_new_vreg(f2);
        TEST_ASSERT(v != NULL);
        (void)woort_IR_ret(f2, (woort_IRValue*)woort_IRFunction_load_const(f2, woort_IRCompiler_add_constant(irc)));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr1;
    TEST_ASSERT(woort_CodeEnv_query_function(cenv, f1, &addr1));
    TEST_ASSERT(addr1 != NULL);

    const woort_Bytecode* addr2;
    TEST_ASSERT(woort_CodeEnv_query_function(cenv, f2, &addr2));
    TEST_ASSERT(addr2 != NULL);
    TEST_ASSERT(addr1 != addr2);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, addr1);
    woort_CodeEnv_unlock(cenv);

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 18: Box 类型 (set_box_int/real) ========== */
static void test_box_types(void)
{
    TEST_BEGIN("box_types_const_box_int_real");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex ci = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cr = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* vi = (woort_IRValue*)woort_IRFunction_load_const(f, ci);
    woort_IRValue* vr = (woort_IRValue*)woort_IRFunction_load_const(f, cr);
    woort_IRValue* v1 = (woort_IRValue*)woort_IRFunction_load_const(f, c1);
    TEST_ASSERT(vi && vr && v1);

    woort_IRValue* r1 = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(r1 != NULL);

    TEST_ASSERT(woort_IR_MOV(f, r1, v1));
    TEST_ASSERT(woort_IR_ret(f, r1));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_box_int(cenv, ci, 42);
    woort_CodeEnv_set_const_box_real(cenv, cr, 3.14);
    woort_CodeEnv_set_const_int(cenv, c1, 12345);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(12345, woort_int(sv + 1));

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 19: Struct 常量 ========== */
static void test_struct_operations(void)
{
    TEST_BEGIN("struct_construction_via_codeenv");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_struct = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* st = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(st != NULL);

    (void)woort_IR_ret(f, st);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 10);
    woort_CodeEnv_set_const_int(cenv, c2, 20);
    woort_IRConstantIndex members[2] = { c1, c2 };
    woort_CodeEnv_set_const_struct(cenv, c_struct, members, 2);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 20: Vector 操作 (基础) ========== */
static void test_vector_operations(void)
{
    TEST_BEGIN("vector_operations_vm_api");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(2, &sv));

    woort_set_vec(sv);
    size_t len = woort_vec_len(sv);
    TEST_ASSERT(len == 0);

    woort_vec_resize(sv, 3);
    len = woort_vec_len(sv);
    TEST_ASSERT(len == 3);

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 21: Map 操作 (基础) ========== */
static void test_map_operations(void)
{
    TEST_BEGIN("map_operations_vm_api");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(3, &sv));

    woort_set_map(sv);
    size_t len = woort_map_len(sv);
    TEST_ASSERT(len == 0);

    woort_map_reserve(sv, 10);

    woort_pop(3);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 22: 调用约定 CALLNWO/CALLNFP ========== */
static void test_call_conventions(void)
{
    TEST_BEGIN("call_conventions_native_fn");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f_main));
    {
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c1)));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c2)));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c3)));
        TEST_ASSERT(woort_IR_CALLNFP(f_main, cfn, 3, r));
        TEST_ASSERT(woort_IR_ret(f_main, r));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_int(cenv, c3, 3);
    woort_CodeEnv_unlock(cenv);
    cenv->m_data_begin[cfn].m_native_function = &sumThree_fn;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(6, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 23: extern 函数调用 ========== */
static void test_extern_function_call(void)
{
    TEST_BEGIN("extern_function_call");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cfn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c3 = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f_main));
    {
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c1)));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c2)));
        TEST_ASSERT(woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, c3)));
        TEST_ASSERT(woort_IR_CALLNFP(f_main, cfn, 3, r));
        TEST_ASSERT(woort_IR_ret(f_main, r));
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_set_const_int(cenv, c2, 2);
    woort_CodeEnv_set_const_int(cenv, c3, 3);
    woort_CodeEnv_unlock(cenv);
    cenv->m_data_begin[cfn].m_native_function = &sumThree_fn;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(6, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 24: 浮点类型 set_float ========== */
static void test_float_type(void)
{
    TEST_BEGIN("float_type_set_float");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cr = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* vr = (woort_IRValue*)woort_IRFunction_load_const(f, cr);
    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(vr && r);

    TEST_ASSERT(woort_IR_ITOR(f, r, vr));
    TEST_ASSERT(woort_IR_ret(f, r));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[cr].m_integer = 42;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    woort_Real result = woort_real(sv + 1);
    TEST_ASSERT(result == 42.0);
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 25: woort_push_reserve/woort_pop ========== */
static void test_stack_reserve_pop(void)
{
    TEST_BEGIN("stack_reserve_pop");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_int = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* r1 = woort_IRFunction_new_vreg(f);
    woort_IRValue* r2 = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(r1 && r2);

    (void)woort_IR_ret(f, r1);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c_int, 99);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(5, &sv));
    woort_set_int(sv, 10);
    woort_set_int(sv + 1, 20);
    woort_set_int(sv + 2, 30);

    TEST_ASSERT_EQ_INT(10, woort_int(sv));
    TEST_ASSERT_EQ_INT(20, woort_int(sv + 1));
    TEST_ASSERT_EQ_INT(30, woort_int(sv + 2));

    woort_pop(3);
    woort_set_int(sv, 40);
    TEST_ASSERT_EQ_INT(40, woort_int(sv));

    woort_pop(1);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 26: woort_set_nil/value/int/real/bool/string ========== */
static void test_stack_setters(void)
{
    TEST_BEGIN("stack_setters_set_nil_int_real_bool_string");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(10, &sv));

    woort_set_nil(sv);
    TEST_ASSERT(woort_int(sv) == 0);

    woort_set_int(sv, 12345);
    TEST_ASSERT_EQ_INT(12345, woort_int(sv));

    woort_set_real(sv, 3.14159);
    TEST_ASSERT_EQ_REAL(3.14159, woort_real(sv));

    woort_set_float(sv, 2.5f);
    float f = woort_float(sv);
    TEST_ASSERT(f > 2.49f && f < 2.51f);

    woort_set_bool(sv, true);
    TEST_ASSERT(woort_bool(sv) == true);

    woort_set_bool(sv, false);
    TEST_ASSERT(woort_bool(sv) == false);

    woort_set_string(sv, "test string");
    TEST_ASSERT(strcmp(woort_string(sv), "test string") == 0);

    woort_pop(10);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 27: woort_set_vec/map ========== */
static void test_heap_type_setters(void)
{
    TEST_BEGIN("heap_type_setters_set_vec_map");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(3, &sv));

    woort_set_vec(sv);
    size_t len = woort_vec_len(sv);
    TEST_ASSERT(len == 0);

    woort_set_map(sv + 1);
    size_t maplen = woort_map_len(sv + 1);
    TEST_ASSERT(maplen == 0);

    woort_pop(3);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 28: woort_set_union_* ========== */
static void test_union_setters(void)
{
    TEST_BEGIN("union_setters_set_union_without_value_value_nil");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(5, &sv));

    woort_set_union_without_value(sv, 0);
    woort_Int id = woort_union_get(sv + 1, sv);
    TEST_ASSERT(id == 0);

    woort_set_union_value(sv, 1, sv);
    id = woort_union_get(sv + 2, sv);
    TEST_ASSERT(id == 1);

    woort_set_union_nil(sv, 2);
    id = woort_union_get(sv + 3, sv);
    TEST_ASSERT(id == 2);

    woort_set_union_int(sv, 3, 42);
    id = woort_union_get(sv + 4, sv);
    TEST_ASSERT(id == 3);

    woort_pop(5);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 29: woort_set_option_* ========== */
static void test_option_setters(void)
{
    TEST_BEGIN("option_setters_set_option_none_value");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(4, &sv));

    woort_set_option_none(sv);
    woort_Int id = woort_union_get(sv + 1, sv);
    TEST_ASSERT(id == 1);

    woort_set_option_value(sv, sv);
    id = woort_union_get(sv + 2, sv);
    TEST_ASSERT(id == 0);

    woort_set_option_int(sv, 123);
    id = woort_union_get(sv + 3, sv);
    TEST_ASSERT(id == 0);

    woort_pop(4);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 30: woort_set_result_* ========== */
static void test_result_setters(void)
{
    TEST_BEGIN("result_setters_set_result_ok_err");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(4, &sv));

    woort_set_result_ok_int(sv, 42);
    woort_Int id = woort_union_get(sv + 1, sv);
    TEST_ASSERT(id == 0);

    woort_set_result_err_int(sv, -1);
    id = woort_union_get(sv + 2, sv);
    TEST_ASSERT(id == 1);

    woort_pop(4);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 31: ret 宏 (return values) ========== */
static void test_return_macros(void)
{
    TEST_BEGIN("return_macros_ret_int_ret_real_ret_bool");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cfn_int = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfn_real = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfn_bool = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f_main));
    {
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* r3 = woort_IRFunction_new_vreg(f_main);

        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, woort_IRCompiler_add_constant(irc)));
        (void)woort_IR_CALLNFP(f_main, cfn_int, 1, r1);

        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, woort_IRCompiler_add_constant(irc)));
        (void)woort_IR_CALLNFP(f_main, cfn_real, 1, r2);

        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, woort_IRCompiler_add_constant(irc)));
        (void)woort_IR_CALLNFP(f_main, cfn_bool, 1, r3);

        (void)woort_IR_ret(f_main, r1);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[cfn_int].m_native_function = &return_int_fn;
    cenv->m_data_begin[cfn_real].m_native_function = &return_real_fn;
    cenv->m_data_begin[cfn_bool].m_native_function = &return_bool_fn;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(12345, woort_int(sv + 1));
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 32: unbox 操作 ========== */
static void test_unbox_operations(void)
{
    TEST_BEGIN("unbox_operations_vm_stack");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(3, &sv));

    woort_set_box_int(sv, 42);
    woort_BoxValueType type = woort_unbox_type(sv);
    TEST_ASSERT(type == WOORT_BOX_VALUE_TYPE_INT);
    woort_Int unboxed = woort_unbox_int(sv);
    TEST_ASSERT_EQ_INT(42, unboxed);

    woort_set_box_real(sv + 1, 3.14);
    type = woort_unbox_type(sv + 1);
    TEST_ASSERT(type == WOORT_BOX_VALUE_TYPE_REAL);
    woort_Real unboxed_real = woort_unbox_real(sv + 1);
    TEST_ASSERT(unboxed_real > 3.13 && unboxed_real < 3.15);

    woort_set_box_bool(sv + 2, true);
    type = woort_unbox_type(sv + 2);
    TEST_ASSERT(type == WOORT_BOX_VALUE_TYPE_BOOL);
    bool unboxed_bool = woort_unbox_bool(sv + 2);
    TEST_ASSERT(unboxed_bool == true);

    woort_pop(3);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 33: 递归函数调用 ========== */
static void test_recursive_fibonacci(void)
{
    TEST_BEGIN("recursive_fibonacci_fib(10)=55");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cfib = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex cn = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_fib;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, &f_fib));
    {
        woort_IRValue* n = woort_IRFunction_get_argument(f_fib, 0);
        woort_IRValue* v2 = (woort_IRValue*)woort_IRFunction_load_const(f_fib, c2);
        woort_IRValue* v1 = (woort_IRValue*)woort_IRFunction_load_const(f_fib, c1);
        woort_IRValue* tmp1 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* tmp2 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_fib);
        woort_IRValue* sum = woort_IRFunction_new_vreg(f_fib);
        woort_IRLabel* L_base = woort_IRFunction_new_label(f_fib);
        TEST_ASSERT(n && v2 && v1 && tmp1 && tmp2 && r1 && r2 && sum && L_base);

        (void)woort_IR_jcc_lt(f_fib, n, v2, L_base);

        (void)woort_IR_SUBI(f_fib, tmp1, n, v1);
        (void)woort_IR_SUBI(f_fib, tmp2, n, v2);
        (void)woort_IR_PUSHCHK(f_fib, tmp1);
        (void)woort_IR_CALLNWO(f_fib, cfib, 1, r1);
        (void)woort_IR_PUSHCHK(f_fib, tmp2);
        (void)woort_IR_CALLNWO(f_fib, cfib, 1, r2);
        (void)woort_IR_ADDI(f_fib, sum, r1, r2);
        (void)woort_IR_ret(f_fib, sum);

        (void)woort_IR_bind(f_fib, L_base);
        (void)woort_IR_ret(f_fib, n);
    }

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f_main));
    {
        woort_IRValue* vn = (woort_IRValue*)woort_IRFunction_load_const(f_main, cn);
        woort_IRValue* result = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_PUSHCHK(f_main, vn);
        (void)woort_IR_CALLNWO(f_main, cfib, 1, result);
        (void)woort_IR_ret(f_main, result);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[c2].m_integer = 2;
    cenv->m_data_begin[c1].m_integer = 1;
    cenv->m_data_begin[cfib].m_script_function = cenv->m_code_begin;
    cenv->m_data_begin[cn].m_integer = 10;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin + f_fib->m_code_length);

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

/* ========== 测试 34: WOORT_VM_CALL_STATUS_RESYNC ========== */
static void test_vm_status_resync(void)
{
    TEST_BEGIN("vm_status_resync");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* r1 = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(r1 != NULL);

    woort_IRConstantIndex c_int = woort_IRCompiler_add_constant(irc);
    (void)woort_IR_ret(f, (woort_IRValue*)woort_IRFunction_load_const(f, c_int));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c_int, 12345);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(3, &sv));

    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(12345, woort_int(sv + 1));

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 35: import_value (跨 VM stack 复制) ========== */
static void test_import_value(void)
{
    TEST_BEGIN("import_value_cross_vm_stack_copy");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_int = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));
    (void)woort_IR_ret(f, (woort_IRValue*)woort_IRFunction_load_const(f, c_int));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c_int, 42);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm1;
    TEST_ASSERT(woort_VMRuntime_create(&vm1));
    (void)woort_VMRuntime_swap(vm1);

    woort_StackValue sv1;
    TEST_ASSERT(woort_push_reserve(3, &sv1));
    woort_set_int(sv1, 100);

    woort_VMRuntime* vm2;
    TEST_ASSERT(woort_VMRuntime_create(&vm2));
    (void)woort_VMRuntime_swap(vm2);

    woort_StackValue sv2;
    TEST_ASSERT(woort_push_reserve(2, &sv2));

    woort_import_value(sv2, vm1, sv1);
    TEST_ASSERT_EQ_INT(100, woort_int(sv2));

    woort_pop(3);
    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    (void)woort_VMRuntime_swap(vm1);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm2);
    woort_VMRuntime_destroy(vm1);
    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 36: WOORT_RET_ABORT / WOORT_RET_YIELD ========== */
static void test_abort_and_yield(void)
{
    TEST_BEGIN("abort_and_yield");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex cfn_abort = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f_main;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f_main));
    {
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_load_const(f_main, woort_IRCompiler_add_constant(irc)));
        (void)woort_IR_CALLNFP(f_main, cfn_abort, 1, r);
        (void)woort_IR_ret(f_main, r);
    }

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    cenv->m_data_begin[cfn_abort].m_native_function = &capture_int_fn;
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv + 1, cenv, c_entry);

    g_captured_int = 0;
    woort_set_int(sv, 999);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv + 1);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);
    TEST_ASSERT_EQ_INT(999, g_captured_int);

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 37: CodeEnv constant struct ========== */
static void test_const_struct(void)
{
    TEST_BEGIN("const_struct");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_struct = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* st = woort_IRFunction_new_vreg(f);
    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(st && r);

    (void)woort_IR_ret(f, st);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 10);
    woort_CodeEnv_set_const_int(cenv, c2, 20);
    woort_IRConstantIndex members[2] = { c1, c2 };
    woort_CodeEnv_set_const_struct(cenv, c_struct, members, 2);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 38: Vector 详细操作 (push/pop/insert/erase) ========== */
static void test_vector_detailed_ops(void)
{
    TEST_BEGIN("vector_detailed_ops_push_pop");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(4, &sv));

    woort_set_vec(sv);
    woort_set_int(sv + 1, 1);
    woort_set_int(sv + 2, 2);
    woort_set_int(sv + 3, 3);

    woort_vec_push(sv, sv + 1);
    woort_vec_push(sv, sv + 2);
    woort_vec_push(sv, sv + 3);

    size_t len = woort_vec_len(sv);
    TEST_ASSERT(len == 3);

    woort_vec_pop(sv);
    len = woort_vec_len(sv);
    TEST_ASSERT(len == 2);

    woort_vec_resize(sv, 5);
    len = woort_vec_len(sv);
    TEST_ASSERT(len == 5);

    woort_vec_clear(sv);
    len = woort_vec_len(sv);
    TEST_ASSERT(len == 0);

    woort_pop(4);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 39: Map 详细操作 (get/set/erase/contains) ========== */
static void test_map_detailed_ops(void)
{
    TEST_BEGIN("map_detailed_ops_set_get_erase_contains");

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    TEST_ASSERT(woort_push_reserve(5, &sv));

    woort_set_map(sv);
    woort_set_int(sv + 1, 100);
    woort_set_string(sv + 2, "key1");
    woort_set_int(sv + 3, 200);
    woort_set_string(sv + 4, "key2");

    bool inserted = woort_map_set_string(sv, "key1", sv + 3);
    TEST_ASSERT(inserted == true);

    inserted = woort_map_set_string(sv, "key2", sv + 4);
    TEST_ASSERT(inserted == true);

    size_t len = woort_map_len(sv);
    TEST_ASSERT(len == 2);

    bool contains = woort_map_contains_string(sv, "key1");
    TEST_ASSERT(contains == true);

    bool erased = woort_map_erase_string(sv, "key1");
    TEST_ASSERT(erased == true);

    contains = woort_map_contains_string(sv, "key1");
    TEST_ASSERT(contains == false);

    woort_pop(5);
    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    TEST_END();
}

/* ========== 测试 40: Map 详细操作 ========== */
static void test_struct_detailed_ops(void)
{
    TEST_BEGIN("map_detailed_insert_lookup");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c_key1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_key2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val1 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_val2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, &f));

    woort_IRValue* v_key1 = (woort_IRValue*)woort_IRFunction_load_const(f, c_key1);
    woort_IRValue* v_val1 = (woort_IRValue*)woort_IRFunction_load_const(f, c_val1);
    woort_IRValue* v_key2 = (woort_IRValue*)woort_IRFunction_load_const(f, c_key2);
    woort_IRValue* v_val2 = (woort_IRValue*)woort_IRFunction_load_const(f, c_val2);
    TEST_ASSERT(v_key1 && v_val1 && v_key2 && v_val2);

    woort_IRValue* m = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(m != NULL);

    TEST_ASSERT(woort_IR_PUSHCHK(f, v_key1));
    TEST_ASSERT(woort_IR_PUSHCHK(f, v_val1));
    TEST_ASSERT(woort_IR_MKMAP(f, m, 1));

    TEST_ASSERT(woort_IR_ret(f, m));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_string(cenv, c_key1, "key1");
    woort_CodeEnv_set_const_int(cenv, c_val1, 100);
    woort_CodeEnv_set_const_string(cenv, c_key2, "key2");
    woort_CodeEnv_set_const_int(cenv, c_val2, 200);
    woort_CodeEnv_unlock(cenv);
    woort_CodeEnv_set_const_script_closure(cenv, c_entry, cenv->m_code_begin);

    woort_VMRuntime* vm;
    TEST_ASSERT(woort_VMRuntime_create(&vm));
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_entry);
    woort_VmCallStatus status = woort_invoke(sv + 1, sv);
    TEST_ASSERT(status == WOORT_VM_CALL_STATUS_NORMAL);

    woort_pop(2);
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(cenv);
    woort_VMRuntime_destroy(vm);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 主函数 ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init();

    (void)printf("\n=== C API Tests ===\n\n");

    test_vm_runtime_lifecycle();
    test_ir_add_static();
    test_ir_vreg_and_argument();
    test_ir_label_and_bind();
    test_ir_srcloc_push_pop();
    test_ir_mov_load_store();
    test_ir_stack_ops();
    test_ir_type_conversions();
    test_ir_real_arithmetic();
    test_ir_real_comparisons();
    test_ir_string_comparisons();
    test_ir_string_concat();
    test_ir_logical_ops();
    test_ir_jcc_jccz();
    test_ir_jcc_variants();
    test_vm_call_status();
    test_codeenv_query_and_lock();
    test_box_types();
    test_struct_operations();
    test_vector_operations();
    test_map_operations();
    test_call_conventions();
    test_extern_function_call();
    test_float_type();
    test_stack_reserve_pop();
    test_stack_setters();
    test_heap_type_setters();
    test_union_setters();
    test_option_setters();
    test_result_setters();
    test_return_macros();
    test_unbox_operations();
    test_recursive_fibonacci();
    test_vm_status_resync();
    test_import_value();
    test_abort_and_yield();
    test_const_struct();
    test_vector_detailed_ops();
    test_map_detailed_ops();
    test_struct_detailed_ops();

    (void)printf("\n=== Results: %d/%d passed ===\n\n",
        g_tests_passed, g_tests_run);

    woort_shutdown();

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
