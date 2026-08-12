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

/* ===================================================================
 * Regression tests for _phase0_jump_chaining cycle detection.
 *
 * 背景：_phase0_jump_chaining (src/woort_ir_function.c:379-410) 追踪
 * 跳转链时，终止条件仅识别：
 *   1) next_target == final_target   （自环 L: JMP L）
 *   2) next_target == original_target（链条回到最初的跳转目标）
 * 由于 final_target 每轮被覆盖，无法检测"从外部进入、且不包含
 * original_target 的环"。此类输入会导致编译期死循环。
 *
 * 这些测试在不可达死代码中构造这样的环，主路径正常返回 42。
 * Phase 0 (跳转合并) 在 Phase 1b (死代码消除) 之前运行，因此死代码
 * 中的跳转链仍会被处理。
 *
 * 未修复时：woort_IRCompiler_finish 在 _phase0_jump_chaining 中死循环。
 * 修复后：编译正常完成，死代码被 Phase 1b 消除，函数返回 42。
 * =================================================================== */

/* ========== Test 1: Two-node cycle (L_a <-> L_b) ========== */
/*
    可达主路径：
        ret 42

    不可达死代码（触发 Bug）：
        jmp L_entry        ; original_target = L_entry
    L_entry:
        jmp L_a            ; 进入环
    L_a:
        jmp L_b
    L_b:
        jmp L_a            ; 环: L_a <-> L_b （不含 L_entry）

    Phase 0 追踪：L_entry -> L_a -> L_b -> L_a -> L_b -> ...  永不回到 L_entry
*/
static void test_jump_chain_two_node_cycle(void)
{
    TEST_BEGIN("jump_chain_two_node_cycle (dead-code L_a<->L_b)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v42 = woort_IRFunction_fetch_const(f, c42);
        TEST_ASSERT(v42 != NULL);

        woort_IRLabel* L_entry = woort_IRFunction_new_label(f);
        woort_IRLabel* L_a = woort_IRFunction_new_label(f);
        woort_IRLabel* L_b = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_entry && L_a && L_b);

        /* 可达主路径 */
        TEST_ASSERT(woort_IR_ret(f, v42));

        /* 死代码：跳转链进入不含 original_target 的 2-环 */
        TEST_ASSERT(woort_IR_jmp(f, L_entry));

        TEST_ASSERT(woort_IR_bind(f, L_entry));
        TEST_ASSERT(woort_IR_jmp(f, L_a));

        TEST_ASSERT(woort_IR_bind(f, L_a));
        TEST_ASSERT(woort_IR_jmp(f, L_b));

        TEST_ASSERT(woort_IR_bind(f, L_b));
        TEST_ASSERT(woort_IR_jmp(f, L_a));
    }

    /* 若 Bug 存在，此处死循环（_phase0_jump_chaining） */
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    (void)woort_CodeEnv_query_function(cenv, f, &addr);

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

/* ========== Test 2: Three-node cycle (L_a -> L_b -> L_c -> L_a) ========== */
/*
    可达主路径：
        ret 42

    不可达死代码（触发 Bug）：
        jmp L_entry        ; original_target = L_entry
    L_entry:
        jmp L_a            ; 进入环
    L_a:
        jmp L_b
    L_b:
        jmp L_c
    L_c:
        jmp L_a            ; 环: L_a -> L_b -> L_c -> L_a （不含 L_entry）

    Phase 0 追踪：L_entry -> L_a -> L_b -> L_c -> L_a -> ...  永不回到 L_entry
*/
static void test_jump_chain_three_node_cycle(void)
{
    TEST_BEGIN("jump_chain_three_node_cycle (dead-code 3-ring)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v42 = woort_IRFunction_fetch_const(f, c42);
        TEST_ASSERT(v42 != NULL);

        woort_IRLabel* L_entry = woort_IRFunction_new_label(f);
        woort_IRLabel* L_a = woort_IRFunction_new_label(f);
        woort_IRLabel* L_b = woort_IRFunction_new_label(f);
        woort_IRLabel* L_c = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_entry && L_a && L_b && L_c);

        /* 可达主路径 */
        TEST_ASSERT(woort_IR_ret(f, v42));

        /* 死代码：跳转链进入不含 original_target 的 3-环 */
        TEST_ASSERT(woort_IR_jmp(f, L_entry));

        TEST_ASSERT(woort_IR_bind(f, L_entry));
        TEST_ASSERT(woort_IR_jmp(f, L_a));

        TEST_ASSERT(woort_IR_bind(f, L_a));
        TEST_ASSERT(woort_IR_jmp(f, L_b));

        TEST_ASSERT(woort_IR_bind(f, L_b));
        TEST_ASSERT(woort_IR_jmp(f, L_c));

        TEST_ASSERT(woort_IR_bind(f, L_c));
        TEST_ASSERT(woort_IR_jmp(f, L_a));
    }

    /* 若 Bug 存在，此处死循环（_phase0_jump_chaining） */
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    (void)woort_CodeEnv_query_function(cenv, f, &addr);

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

/* ========== Test 3: Chain enters cycle after several hops ========== */
/*
    可达主路径：
        ret 42

    不可达死代码（触发 Bug）：
        jmp L_p1            ; original_target = L_p1
    L_p1:
        jmp L_p2            ; 经过若干跳后才进入环
    L_p2:
        jmp L_a
    L_a:
        jmp L_b
    L_b:
        jmp L_a             ; 环: L_a <-> L_b （不含 L_p1 / L_p2）

    Phase 0 追踪：L_p1 -> L_p2 -> L_a -> L_b -> L_a -> ...  永不回到 L_p1
*/
static void test_jump_chain_hop_then_cycle(void)
{
    TEST_BEGIN("jump_chain_hop_then_cycle (dead-code, 2 hops to ring)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRConstantIndex c42 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 0, &f));
    {
        const woort_IRValue* v42 = woort_IRFunction_fetch_const(f, c42);
        TEST_ASSERT(v42 != NULL);

        woort_IRLabel* L_p1 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_p2 = woort_IRFunction_new_label(f);
        woort_IRLabel* L_a = woort_IRFunction_new_label(f);
        woort_IRLabel* L_b = woort_IRFunction_new_label(f);
        TEST_ASSERT(L_p1 && L_p2 && L_a && L_b);

        /* 可达主路径 */
        TEST_ASSERT(woort_IR_ret(f, v42));

        /* 死代码：经两跳后进入 2-环 */
        TEST_ASSERT(woort_IR_jmp(f, L_p1));

        TEST_ASSERT(woort_IR_bind(f, L_p1));
        TEST_ASSERT(woort_IR_jmp(f, L_p2));

        TEST_ASSERT(woort_IR_bind(f, L_p2));
        TEST_ASSERT(woort_IR_jmp(f, L_a));

        TEST_ASSERT(woort_IR_bind(f, L_a));
        TEST_ASSERT(woort_IR_jmp(f, L_b));

        TEST_ASSERT(woort_IR_bind(f, L_b));
        TEST_ASSERT(woort_IR_jmp(f, L_a));
    }

    /* 若 Bug 存在，此处死循环（_phase0_jump_chaining） */
    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const woort_Bytecode* addr;
    (void)woort_CodeEnv_query_function(cenv, f, &addr);

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

/* ========== Main ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    (void)printf("\n=== IR Jump-Chain Cycle Regression Tests ===\n\n");
    (void)printf("NOTE: Without the _phase0_jump_chaining fix, these tests\n");
    (void)printf("      hang forever inside woort_IRCompiler_finish().\n\n");

    test_jump_chain_two_node_cycle();
    test_jump_chain_three_node_cycle();
    test_jump_chain_hop_then_cycle();

    (void)printf("\n  %d/%d tests passed.\n\n", g_tests_passed, g_tests_run);

    woort_shutdown(NULL, NULL);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
