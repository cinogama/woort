/*
 * test_ir_srcloc.c
 *
 * 测试 IR 源码位置信息支持（仅使用 woort.h 公共 API）：
 *   1. IR 编译后源码映射生成
 *   2. 字节码偏移 -> 源码位置查询
 *   3. 源码位置 -> 字节码偏移查询
 *   4. 无源码信息时的行为
 *   5. push/pop 嵌套
 *   6. 多函数场景
 */

#include "woort.h"

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
        (void)printf("  [TEST] %-50s ", _test_name);            \
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

/* ========== 测试 1: IR 编译 + 源码映射生成 + 双向查询 ========== */
/*
 * 模拟编译如下代码：
 *
 * func add_one(x: int) => int {    // line 1
 *     let result = x + 1;          // line 2
 *     return result;                // line 3
 * }
 */
static void test_ir_srcloc_basic(void)
{
    TEST_BEGIN("ir_srcloc_basic_compile_and_query");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    const char* path = woort_IRCompiler_intern_string(irc, "test.woo");

    /* 添加常量 1 */
    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);

    /* 添加函数 */
    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, 0, &f));

    /* 获取参数 */
    woort_IRValue* arg_x = woort_IRFunction_get_argument(f, 0);
    TEST_ASSERT(arg_x != NULL);

    /* line 2: let result = x + 1 */
    TEST_ASSERT(woort_IRFunction_push_srcloc(f, path, 2, 1, 2, 22));
    const woort_IRValue* v_one = woort_IRFunction_fetch_const(f, c1);
    TEST_ASSERT(v_one != NULL);
    woort_IRValue* v_result = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(v_result != NULL);
    TEST_ASSERT(woort_IR_ADDI(f, v_result, arg_x, v_one));
    woort_IRFunction_pop_srcloc(f);

    /* line 3: return result */
    TEST_ASSERT(woort_IRFunction_push_srcloc(f, path, 3, 1, 3, 15));
    TEST_ASSERT(woort_IR_ret(f, v_result));
    woort_IRFunction_pop_srcloc(f);

    /* 编译 */
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    /* 设置常量值 */
    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_unlock(cenv);

    /* 查询：字节码偏移 -> 源码位置 */
    woort_SourceLocation found_loc;
    memset(&found_loc, 0, sizeof(found_loc));

    /* 应该能找到 line 2 */
    uint32_t off2 = 0;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(
        cenv, "test.woo", 2, &off2));
    memset(&found_loc, 0, sizeof(found_loc));
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(
        cenv, off2, &found_loc));
    TEST_ASSERT(found_loc.m_begin_line == 2);

    /* 查询 line 3 */
    uint32_t off3 = 0;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(
        cenv, "test.woo", 3, &off3));
    memset(&found_loc, 0, sizeof(found_loc));
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(
        cenv, off3, &found_loc));
    TEST_ASSERT(found_loc.m_begin_line == 3);

    /* 查询不存在的文件 */
    uint32_t bad_offset;
    TEST_ASSERT(!woort_CodeEnv_find_offset_by_srcloc(
        cenv, "nonexistent.woo", 1, &bad_offset));

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 2: 无源码信息时的行为 ========== */
static void test_ir_no_srcloc(void)
{
    TEST_BEGIN("ir_no_srcloc_fallback");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));

    /* 不推入任何源码位置，直接发射 IR */
    const woort_IRValue* v = woort_IRFunction_fetch_const(f, c42);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(woort_IR_ret(f, v));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c42, 42);
    woort_CodeEnv_unlock(cenv);

    /* 查询应返回 false */
    woort_SourceLocation loc;
    TEST_ASSERT(!woort_CodeEnv_find_srcloc_by_offset(cenv, 0, &loc));

    uint32_t offset;
    TEST_ASSERT(!woort_CodeEnv_find_offset_by_srcloc(cenv, "test.woo", 1, &offset));

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 3: push/pop 嵌套 ========== */
/*
 * 模拟：
 *   push(outer)     -> line 10
 *     IR_ADDI         -> 关联 line 10
 *     push(inner)   -> line 20
 *       IR_SUBI       -> 关联 line 20
 *     pop
 *     IR_MULI         -> 关联 line 10 (恢复到外层)
 *   pop
 *   IR_ret            -> 无源码信息
 */
static void test_ir_srcloc_nested_push_pop(void)
{
    TEST_BEGIN("ir_srcloc_nested_push_pop");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    const char* path = woort_IRCompiler_intern_string(irc, "nested.woo");

    woort_IRConstantIndex c0 = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 2, 0, &f));

    woort_IRValue* a = woort_IRFunction_get_argument(f, 0);
    woort_IRValue* b = woort_IRFunction_get_argument(f, 1);
    woort_IRValue* r1 = woort_IRFunction_new_vreg(f);
    woort_IRValue* r2 = woort_IRFunction_new_vreg(f);
    woort_IRValue* r3 = woort_IRFunction_new_vreg(f);
    TEST_ASSERT(a && b && r1 && r2 && r3);

    /* outer: line 10 */
    TEST_ASSERT(woort_IRFunction_push_srcloc(f, path, 10, 1, 10, 20));
    TEST_ASSERT(woort_IR_ADDI(f, r1, a, b));      /* -> line 10 */

    /* inner: line 20 */
    TEST_ASSERT(woort_IRFunction_push_srcloc(f, path, 20, 1, 20, 20));
    TEST_ASSERT(woort_IR_SUBI(f, r2, a, b));      /* -> line 20 */
    woort_IRFunction_pop_srcloc(f);

    TEST_ASSERT(woort_IR_MULI(f, r3, r1, r2));    /* -> line 10 (恢复) */
    woort_IRFunction_pop_srcloc(f);

    /* 无源码信息 */
    TEST_ASSERT(woort_IR_ret(f, r3));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c0, 0);
    woort_CodeEnv_unlock(cenv);

    /* 验证 line 10 和 line 20 都能查到 */
    uint32_t off10, off20;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(cenv, "nested.woo", 10, &off10));
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(cenv, "nested.woo", 20, &off20));

    woort_SourceLocation loc;
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(cenv, off10, &loc));
    TEST_ASSERT(loc.m_begin_line == 10);

    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(cenv, off20, &loc));
    TEST_ASSERT(loc.m_begin_line == 20);

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 4: 多函数场景 ========== */
static void test_ir_srcloc_multi_function(void)
{
    TEST_BEGIN("ir_srcloc_multi_function");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    const char* path_a = woort_IRCompiler_intern_string(irc, "file_a.woo");
    const char* path_b = woort_IRCompiler_intern_string(irc, "file_b.woo");
    TEST_ASSERT(path_a != NULL && path_b != NULL);

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);

    /* 函数 1: 在 file_a.woo */
    woort_IRFunction* f1;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, 0, &f1));

    woort_IRValue* arg1 = woort_IRFunction_get_argument(f1, 0);
    const woort_IRValue* one1 = woort_IRFunction_fetch_const(f1, c1);
    woort_IRValue* res1 = woort_IRFunction_new_vreg(f1);
    TEST_ASSERT(arg1 && one1 && res1);

    TEST_ASSERT(woort_IRFunction_push_srcloc(f1, path_a, 5, 1, 5, 20));
    TEST_ASSERT(woort_IR_ADDI(f1, res1, arg1, one1));
    TEST_ASSERT(woort_IR_ret(f1, res1));
    woort_IRFunction_pop_srcloc(f1);

    /* 函数 2: 在 file_b.woo */
    woort_IRFunction* f2;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, 0, &f2));

    woort_IRValue* arg2 = woort_IRFunction_get_argument(f2, 0);
    const woort_IRValue* one2 = woort_IRFunction_fetch_const(f2, c1);
    woort_IRValue* res2 = woort_IRFunction_new_vreg(f2);
    TEST_ASSERT(arg2 && one2 && res2);

    TEST_ASSERT(woort_IRFunction_push_srcloc(f2, path_b, 10, 1, 10, 20));
    TEST_ASSERT(woort_IR_SUBI(f2, res2, arg2, one2));
    TEST_ASSERT(woort_IR_ret(f2, res2));
    woort_IRFunction_pop_srcloc(f2);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c1, 1);
    woort_CodeEnv_unlock(cenv);

    /* 查询 file_a.woo line 5 */
    uint32_t off_a;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(cenv, "file_a.woo", 5, &off_a));
    woort_SourceLocation loc;
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(cenv, off_a, &loc));
    TEST_ASSERT(loc.m_begin_line == 5);
    TEST_ASSERT(loc.m_filepath != NULL);
    TEST_ASSERT(strcmp(loc.m_filepath, "file_a.woo") == 0);

    /* 查询 file_b.woo line 10 */
    uint32_t off_b;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(cenv, "file_b.woo", 10, &off_b));
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(cenv, off_b, &loc));
    TEST_ASSERT(loc.m_begin_line == 10);
    TEST_ASSERT(strcmp(loc.m_filepath, "file_b.woo") == 0);

    /* 两个函数的偏移不应相同 */
    TEST_ASSERT(off_a != off_b);

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 主函数 ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    (void)printf("\n=== IR Source Location Tests ===\n\n");

    test_ir_srcloc_basic();
    test_ir_no_srcloc();
    test_ir_srcloc_nested_push_pop();
    test_ir_srcloc_multi_function();

    (void)printf("\n=== Results: %d/%d passed ===\n\n",
        g_tests_passed, g_tests_run);

    woort_shutdown(NULL, NULL);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
