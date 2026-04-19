/*
 * test_ir_srcloc.c
 *
 * 测试 IR 源码位置信息支持：
 *   1. StringPool intern 去重
 *   2. SourceLocationStack push/pop
 *   3. IR 编译后源码映射生成
 *   4. 字节码偏移 -> 源码位置查询
 *   5. 源码位置 -> 字节码偏移查询
 *   6. 无源码信息时的行为
 *   7. 多函数场景
 */

#include "woort.h"

#include "woort_codeenv.h"
#include "woort_vm.h"
#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_ir_value.h"
#include "woort_ir_srcloc.h"
#include "woort_value.h"

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

/* ========== 测试 1: StringPool intern 去重 ========== */
static void test_string_pool_intern(void)
{
    TEST_BEGIN("string_pool_intern");

    woort_StringPool pool;
    woort_StringPool_init(&pool);

    const char* a = woort_StringPool_intern(&pool, "hello.woo");
    TEST_ASSERT(a != NULL);
    TEST_ASSERT(strcmp(a, "hello.woo") == 0);

    /* 相同内容 intern 应返回相同指针 */
    const char* b = woort_StringPool_intern(&pool, "hello.woo");
    TEST_ASSERT(b == a);

    /* 不同内容 intern 应返回不同指针 */
    const char* c = woort_StringPool_intern(&pool, "world.woo");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT(c != a);
    TEST_ASSERT(strcmp(c, "world.woo") == 0);

    /* NULL 返回 NULL */
    const char* d = woort_StringPool_intern(&pool, NULL);
    TEST_ASSERT(d == NULL);

    woort_StringPool_deinit(&pool);

    TEST_END();
}

/* ========== 测试 2: SourceLocationStack push/pop ========== */
static void test_srcloc_stack(void)
{
    TEST_BEGIN("srcloc_stack_push_pop");

    woort_SourceLocationStack stack;
    woort_SourceLocationStack_init(&stack);

    TEST_ASSERT(woort_SourceLocationStack_empty(&stack));
    TEST_ASSERT(woort_SourceLocationStack_top(&stack) == NULL);

    woort_SourceLocation loc1 = {
        "test.woo", 1, 1, 1, 10
    };
    woort_SourceLocation loc2 = {
        "test.woo", 5, 1, 5, 20
    };

    TEST_ASSERT(woort_SourceLocationStack_push(&stack, &loc1));
    TEST_ASSERT(!woort_SourceLocationStack_empty(&stack));

    const woort_SourceLocation* top = woort_SourceLocationStack_top(&stack);
    TEST_ASSERT(top != NULL);
    TEST_ASSERT(top->m_begin_line == 1);

    TEST_ASSERT(woort_SourceLocationStack_push(&stack, &loc2));
    top = woort_SourceLocationStack_top(&stack);
    TEST_ASSERT(top != NULL);
    TEST_ASSERT(top->m_begin_line == 5);

    woort_SourceLocationStack_pop(&stack);
    top = woort_SourceLocationStack_top(&stack);
    TEST_ASSERT(top != NULL);
    TEST_ASSERT(top->m_begin_line == 1);

    woort_SourceLocationStack_pop(&stack);
    TEST_ASSERT(woort_SourceLocationStack_empty(&stack));

    woort_SourceLocationStack_deinit(&stack);

    TEST_END();
}

/* ========== 测试 3: SourceLocation equal ========== */
static void test_srcloc_equal(void)
{
    TEST_BEGIN("srcloc_equal");

    const char* path = "test.woo";

    woort_SourceLocation a = { path, 1, 1, 1, 10 };
    woort_SourceLocation b = { path, 1, 1, 1, 10 };
    woort_SourceLocation c = { path, 2, 1, 2, 10 };
    woort_SourceLocation d = { "other.woo", 1, 1, 1, 10 };

    TEST_ASSERT(woort_SourceLocation_equal(&a, &b));
    TEST_ASSERT(!woort_SourceLocation_equal(&a, &c));
    TEST_ASSERT(!woort_SourceLocation_equal(&a, &d));

    TEST_END();
}

/* ========== 测试 4: IR 编译 + 源码映射生成 + 双向查询 ========== */
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

    /* intern 路径 */
    const char* path = woort_IRCompiler_intern_string(irc, "test.woo");
    TEST_ASSERT(path != NULL);

    /* 相同路径 intern 返回相同指针 */
    const char* path2 = woort_IRCompiler_intern_string(irc, "test.woo");
    TEST_ASSERT(path2 == path);

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
    const woort_IRValue* v_one = woort_IRFunction_load_const(f, c1);
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

    /* 验证：CodeEnv 中应该有源码映射 */
    TEST_ASSERT(cenv->m_source_map.m_entry_count > 0);
    TEST_ASSERT(cenv->m_source_map.m_entries != NULL);

    /* 查询：字节码偏移 -> 源码位置 */
    woort_SourceLocation found_loc;
    memset(&found_loc, 0, sizeof(found_loc));

    /* 第一个映射条目应该对应 line 2 */
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(
        cenv, cenv->m_source_map.m_entries[0].m_bytecode_offset, &found_loc));
    TEST_ASSERT(found_loc.m_begin_line == 2);
    TEST_ASSERT(found_loc.m_filepath != NULL);
    TEST_ASSERT(strcmp(found_loc.m_filepath, "test.woo") == 0);

    /* 查询：源码位置 -> 字节码偏移 */
    uint32_t found_offset = 0;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(
        cenv, "test.woo", 2, &found_offset));

    /* 找到的偏移应该能反向查回 line 2 */
    memset(&found_loc, 0, sizeof(found_loc));
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(
        cenv, found_offset, &found_loc));
    TEST_ASSERT(found_loc.m_begin_line == 2);

    /* 查询 line 3 */
    found_offset = 0;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(
        cenv, "test.woo", 3, &found_offset));
    memset(&found_loc, 0, sizeof(found_loc));
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(
        cenv, found_offset, &found_loc));
    TEST_ASSERT(found_loc.m_begin_line == 3);

    /* 查询不存在的文件 */
    TEST_ASSERT(!woort_CodeEnv_find_offset_by_srcloc(
        cenv, "nonexistent.woo", 1, &found_offset));

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 5: 无源码信息时的行为 ========== */
static void test_ir_no_srcloc(void)
{
    TEST_BEGIN("ir_no_srcloc_fallback");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));

    /* 不推入任何源码位置，直接发射 IR */
    const woort_IRValue* v = woort_IRFunction_load_const(f, c42);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(woort_IR_ret(f, v));

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c42, 42);
    woort_CodeEnv_unlock(cenv);

    /* 无源码映射 */
    TEST_ASSERT(cenv->m_source_map.m_entry_count == 0);

    /* 查询应返回 false */
    woort_SourceLocation loc;
    TEST_ASSERT(!woort_CodeEnv_find_srcloc_by_offset(cenv, 0, &loc));

    uint32_t offset;
    TEST_ASSERT(!woort_CodeEnv_find_offset_by_srcloc(cenv, "test.woo", 1, &offset));

    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== 测试 6: push/pop 嵌套 ========== */
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
    TEST_ASSERT(path != NULL);

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

    /* 应该有映射条目 */
    TEST_ASSERT(cenv->m_source_map.m_entry_count >= 2);

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

/* ========== 测试 7: 多函数场景 ========== */
static void test_ir_srcloc_multi_function(void)
{
    TEST_BEGIN("ir_srcloc_multi_function");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    const char* path_a = woort_IRCompiler_intern_string(irc, "file_a.woo");
    const char* path_b = woort_IRCompiler_intern_string(irc, "file_b.woo");
    TEST_ASSERT(path_a != NULL && path_b != NULL);
    TEST_ASSERT(path_a != path_b);

    woort_IRConstantIndex c1 = woort_IRCompiler_add_constant(irc);

    /* 函数 1: 在 file_a.woo */
    woort_IRFunction* f1;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 1, 0, &f1));

    woort_IRValue* arg1 = woort_IRFunction_get_argument(f1, 0);
    const woort_IRValue* one1 = woort_IRFunction_load_const(f1, c1);
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
    const woort_IRValue* one2 = woort_IRFunction_load_const(f2, c1);
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

    /* 两个函数都应该有映射 */
    TEST_ASSERT(cenv->m_source_map.m_entry_count >= 2);

    /* 查询 file_a.woo line 5 */
    uint32_t off_a;
    TEST_ASSERT(woort_CodeEnv_find_offset_by_srcloc(cenv, "file_a.woo", 5, &off_a));
    woort_SourceLocation loc;
    TEST_ASSERT(woort_CodeEnv_find_srcloc_by_offset(cenv, off_a, &loc));
    TEST_ASSERT(loc.m_begin_line == 5);
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

/* ========== 测试 8: SourceMap 二分查找精确性 ========== */
static void test_source_map_binary_search(void)
{
    TEST_BEGIN("source_map_binary_search");

    /* 手动构造一个 SourceMap 来测试二分查找 */
    woort_SourceMap_Entry entries[4];
    entries[0].m_bytecode_offset = 0;
    entries[0].m_location = (woort_SourceLocation){ "test.woo", 1, 1, 1, 10 };
    entries[1].m_bytecode_offset = 5;
    entries[1].m_location = (woort_SourceLocation){ "test.woo", 2, 1, 2, 10 };
    entries[2].m_bytecode_offset = 10;
    entries[2].m_location = (woort_SourceLocation){ "test.woo", 3, 1, 3, 10 };
    entries[3].m_bytecode_offset = 20;
    entries[3].m_location = (woort_SourceLocation){ "test.woo", 5, 1, 5, 10 };

    woort_SourceMap map;
    map.m_entries = entries;
    map.m_entry_count = 4;

    woort_SourceLocation loc;

    /* 精确命中 */
    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 0, &loc));
    TEST_ASSERT(loc.m_begin_line == 1);

    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 5, &loc));
    TEST_ASSERT(loc.m_begin_line == 2);

    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 10, &loc));
    TEST_ASSERT(loc.m_begin_line == 3);

    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 20, &loc));
    TEST_ASSERT(loc.m_begin_line == 5);

    /* 落在两个条目之间 -> 取 <= 偏移的最大条目 */
    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 3, &loc));
    TEST_ASSERT(loc.m_begin_line == 1);

    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 7, &loc));
    TEST_ASSERT(loc.m_begin_line == 2);

    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 15, &loc));
    TEST_ASSERT(loc.m_begin_line == 3);

    TEST_ASSERT(woort_SourceMap_find_by_offset(&map, 100, &loc));
    TEST_ASSERT(loc.m_begin_line == 5);

    /* 空映射 */
    woort_SourceMap empty_map;
    empty_map.m_entries = NULL;
    empty_map.m_entry_count = 0;
    TEST_ASSERT(!woort_SourceMap_find_by_offset(&empty_map, 0, &loc));

    TEST_END();
}

/* ========== 测试 9: SourceMap 按行查找 ========== */
static void test_source_map_find_by_line(void)
{
    TEST_BEGIN("source_map_find_by_line");

    const char* path = "test.woo";

    woort_SourceMap_Entry entries[3];
    entries[0].m_bytecode_offset = 0;
    entries[0].m_location = (woort_SourceLocation){ path, 1, 1, 1, 10 };
    entries[1].m_bytecode_offset = 5;
    entries[1].m_location = (woort_SourceLocation){ path, 5, 1, 5, 10 };
    entries[2].m_bytecode_offset = 10;
    entries[2].m_location = (woort_SourceLocation){ path, 10, 1, 10, 10 };

    woort_SourceMap map;
    map.m_entries = entries;
    map.m_entry_count = 3;

    uint32_t offset;

    /* 精确行号 */
    TEST_ASSERT(woort_SourceMap_find_by_line(&map, path, 1, &offset));
    TEST_ASSERT(offset == 0);

    TEST_ASSERT(woort_SourceMap_find_by_line(&map, path, 5, &offset));
    TEST_ASSERT(offset == 5);

    TEST_ASSERT(woort_SourceMap_find_by_line(&map, path, 10, &offset));
    TEST_ASSERT(offset == 10);

    /* 在两行之间 -> >= line 的最小行 */
    TEST_ASSERT(woort_SourceMap_find_by_line(&map, path, 3, &offset));
    TEST_ASSERT(offset == 5); /* line 5 是 >= 3 的最小 */

    TEST_ASSERT(woort_SourceMap_find_by_line(&map, path, 7, &offset));
    TEST_ASSERT(offset == 10); /* line 10 是 >= 7 的最小 */

    /* 大于所有行 -> fallback 到 <= line 的最大 */
    TEST_ASSERT(woort_SourceMap_find_by_line(&map, path, 100, &offset));
    TEST_ASSERT(offset == 10); /* line 10 是 <= 100 的最大 */

    /* 不匹配的路径 */
    TEST_ASSERT(!woort_SourceMap_find_by_line(&map, "other.woo", 1, &offset));

    TEST_END();
}

/* ========== 主函数 ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init();

    (void)printf("\n=== IR Source Location Tests ===\n\n");

    test_string_pool_intern();
    test_srcloc_stack();
    test_srcloc_equal();
    test_source_map_binary_search();
    test_source_map_find_by_line();
    test_ir_srcloc_basic();
    test_ir_no_srcloc();
    test_ir_srcloc_nested_push_pop();
    test_ir_srcloc_multi_function();

    (void)printf("\n=== Results: %d/%d passed ===\n\n",
        g_tests_passed, g_tests_run);

    woort_shutdown();

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
