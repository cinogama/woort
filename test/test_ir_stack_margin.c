/*
 * 需要访问编译器内部结构（m_committed_codes / IRFunction 字段），
 * 内部头会引入完整版 woort_Value 定义，因此本翻译单元需要以
 * WOORT_IMPL 方式包含公共头。测试默认静态链接 woort（WOORT_STATIC_LIB），
 * 该定义不影响 API 链接属性。
 */
#define WOORT_IMPL
#include "woort.h"

#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_opcode_formal.h"
#include "woort_vector.h"

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
    (void)printf("PASS\n");                                     \
    g_tests_passed++;                                           \
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
 * Regression tests for PUSHRCHK temp-slot margin (stack allocation).
 *
 * 背景：vreg 栈槽的逻辑偏移为 -slot-captured_count，发射层的
 * _get_fact_offset 会把 <= -126 的逻辑偏移再偏移 -3（避开临时槽
 * -126/-127/-128）。因此当 captured_count + max_slots 越过 125 时，
 * PUSHRCHK 必须在 max_slots 之外多预留 3 个槽，否则最深的栈槽
 * （实际偏移最深可达 -(captured_count+max_slots+2)）会写到帧外。
 *
 * 帧布局（VM CALLS/CALLC + PUSHRCHK）：
 *   [bp+3..]  参数（调用方压栈）
 *   [bp+2]    返回地址
 *   [bp+1]    callway + bp offset
 *   [bp..bp-captured_count+1] 捕获值（call 时 memcpy 展开）
 *   [bp-captured_count-1 .. bp-captured_count-max_slots] 局部槽
 *   （PUSHRCHK 预留；越过 -126 边界时需 +3）
 *
 * 原实现仅按 max_slots > 125 决定 +3 余量（忽略了 captured_count），
 * 捕获数较多的闭包在边界条件下会少预留 3 个槽。
 *
 * 本测试不运行函数，直接解码函数入口的 PUSHRCHK 操作数，
 * 用"仅 captured_count 不同的同构函数"做差分断言：
 *   - 未越界（captured_count + max_slots <= 125）：操作数相同；
 *   - 越界：操作数恰好多 3。
 *
 * 未修复时：captured=120 的 PUSHRCHK 操作数与 captured=0 相同（少 3）。
 * =================================================================== */

/*
 * 构造一个拥有 nv 个同时活跃 vreg 的函数：
 *   v0..v{nv-1} = arg0
 *   r = arg0
 *   r += v0; r += v1; ... r += v{nv-1}
 *   return r
 * 所有 v_i 的活跃区间在 ADDI 链处重叠，max_slots >= nv 且 <= nv+2。
 */
static woort_IRFunction* build_fn_with_live_slots(
    woort_IRCompiler* irc,
    uint32_t param_count,
    uint32_t captured_count,
    uint32_t nv)
{
    woort_IRFunction* f = NULL;
    if (!woort_IRCompiler_add_function(irc, param_count, captured_count, &f))
        return NULL;

    woort_IRValue* arg0 = woort_IRFunction_get_argument(f, 0);
    if (arg0 == NULL)
        return NULL;

    woort_IRValue** vs = (woort_IRValue**)malloc(nv * sizeof(woort_IRValue*));
    if (vs == NULL)
        return NULL;

    for (uint32_t i = 0; i < nv; ++i)
    {
        vs[i] = woort_IRFunction_new_vreg(f);
        if (vs[i] == NULL ||
            !woort_IR_MOV(f, vs[i], arg0))
        {
            free(vs);
            return NULL;
        }
    }

    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    if (r == NULL || !woort_IR_MOV(f, r, arg0))
    {
        free(vs);
        return NULL;
    }

    for (uint32_t i = 0; i < nv; ++i)
    {
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f);
        if (r2 == NULL ||
            !woort_IR_ADDI(f, r2, r, vs[i]))
        {
            free(vs);
            return NULL;
        }
        r = r2;
    }

    free(vs);

    if (!woort_IR_ret(f, r))
        return NULL;
    return f;
}

/* 读取函数入口处的 PUSHRCHK 操作数（N24）。失败返回 -1。 */
static int64_t first_pushrchk_operand(
    woort_IRCompiler* irc,
    const woort_IRFunction* f)
{
    if (f->m_code_offset >= irc->m_committed_codes.m_size)
        return -1;

    const woort_Bytecode bc =
        ((const woort_Bytecode*)irc->m_committed_codes.m_data)[f->m_code_offset];

    /* 必须是 PUSHRCHK（PUSHCHK 主命令 + mode 0） */
    if (WOORT_BYTECODE(OPM8, bc) !=
        WOORT_BYTECODE(OPM8, woort_OpCode_PUSHRCHK(0)))
        return -1;

    return (int64_t)WOORT_BYTECODE(ABC24, bc);
}

static void free_env(woort_CodeEnv* cenv)
{
    woort_CodeEnv_drop(cenv);
}

/* ========== Test 1: 捕获区+局部槽越界时 PUSHRCHK 需 +3 ========== */
static void test_stack_margin_crossing_boundary(void)
{
    TEST_BEGIN("stack_margin crossing -126 boundary (+3)");

    /*
     * nv=118 => max_slots <= 120（每条 ADDI 至多同时活跃: 未用完的 v + r + dst）。
     * captured=0  : 0+120 <= 125，无余量 => 操作数 == max_slots。
     * captured=120: 120+120 > 125，需 +3 => 操作数 == max_slots + 3。
     */
    enum { NV = 118, CAPTURED = 120 };

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_plain = build_fn_with_live_slots(irc, 1, 0, NV);
    TEST_ASSERT(f_plain != NULL);
    woort_IRFunction* f_capt = build_fn_with_live_slots(irc, 1, CAPTURED, NV);
    TEST_ASSERT(f_capt != NULL);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const int64_t op_plain = first_pushrchk_operand(irc, f_plain);
    const int64_t op_capt = first_pushrchk_operand(irc, f_capt);

    TEST_ASSERT(op_plain > 0);
    TEST_ASSERT(op_plain <= 125); /* 未越界侧不带余量（差分前提） */
    TEST_ASSERT(op_capt > 0);
    TEST_ASSERT_EQ_INT(op_plain + 3, op_capt);

    free_env(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 2: 未越界时不额外预留 ========== */
static void test_stack_margin_not_crossed(void)
{
    TEST_BEGIN("stack_margin below boundary (no +3)");

    /* nv=2 => max_slots <= 4；captured=120: 120+4 <= 125，无需余量 */
    enum { NV = 2, CAPTURED = 120 };

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_plain = build_fn_with_live_slots(irc, 1, 0, NV);
    TEST_ASSERT(f_plain != NULL);
    woort_IRFunction* f_capt = build_fn_with_live_slots(irc, 1, CAPTURED, NV);
    TEST_ASSERT(f_capt != NULL);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const int64_t op_plain = first_pushrchk_operand(irc, f_plain);
    const int64_t op_capt = first_pushrchk_operand(irc, f_capt);

    TEST_ASSERT(op_plain > 0);
    TEST_ASSERT(op_capt > 0);
    TEST_ASSERT_EQ_INT(op_plain, op_capt);

    free_env(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 3: 捕获数上限（不得侵入临时槽窗口） ========== */
static void test_captured_count_limit(void)
{
    TEST_BEGIN("captured_count limit (<= 126, temp window)");

    woort_IRCompiler* irc = woort_IRCompiler_create();

    /*
     * 捕获区在调用时解包到偏移 0..-(captured_count-1)，而 -126/-127/-128
     * 是发射层固定的 a8 临时槽。captured_count >= 127 会使捕获值落进
     * 临时槽窗口且更深的偏移会被错误平移 —— add_function 必须拒绝。
     */
    woort_IRFunction* f_bad = NULL;
    TEST_ASSERT(woort_IRCompiler_add_function(irc, 0, 127, &f_bad) == false);
    TEST_ASSERT(f_bad == NULL);

    /* 126（窗口上沿）必须可用，且编译器未被污染 */
    woort_IRFunction* f_ok = build_fn_with_live_slots(irc, 1, 126, 1);
    TEST_ASSERT(f_ok != NULL);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    free_env(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Test 4: 参数超出 S8 时帧需预留到临时槽窗口 ========== */
static void test_many_params_temp_reserve(void)
{
    TEST_BEGIN("many params reserve temp window (-128)");

    /*
     * 参数偏移为 3+idx。param_count = 130 时最深参数在 +132，超出 S8；
     * a8 类指令会把这类操作数搬运到固定临时槽 -126..-128，帧必须
     * 预留到 -128（captured=0 时即 PUSHRCHK(128)）。
     * 对照组 param_count=1，少量局部槽，无余量。
     */
    enum { NV = 2, MANY_PARAMS = 130 };

    woort_IRCompiler* irc = woort_IRCompiler_create();

    woort_IRFunction* f_plain = build_fn_with_live_slots(irc, 1, 0, NV);
    TEST_ASSERT(f_plain != NULL);
    woort_IRFunction* f_many = build_fn_with_live_slots(irc, MANY_PARAMS, 0, NV);
    TEST_ASSERT(f_many != NULL);

    woort_CodeEnv* cenv;
    TEST_ASSERT(woort_IRCompiler_finish(irc, &cenv));

    const int64_t op_plain = first_pushrchk_operand(irc, f_plain);
    const int64_t op_many = first_pushrchk_operand(irc, f_many);

    TEST_ASSERT(op_plain > 0);
    TEST_ASSERT(op_plain <= 125); /* 对照组不需要临时槽窗口 */
    TEST_ASSERT(op_many > 0);
    TEST_ASSERT_EQ_INT(128, op_many);

    free_env(cenv);
    woort_IRCompiler_close(irc);

    TEST_END();
}

/* ========== Main ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    (void)printf("\n=== IR Stack-Margin Regression Tests ===\n\n");
    (void)printf("NOTE: Without the captured_count margin fix, test 1 sees\n");
    (void)printf("      identical PUSHRCHK operands (missing +3).\n\n");

    test_stack_margin_crossing_boundary();
    test_stack_margin_not_crossed();
    test_captured_count_limit();
    test_many_params_temp_reserve();

    (void)printf("\n  %d/%d tests passed.\n\n", g_tests_passed, g_tests_run);

    woort_shutdown(NULL, NULL);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
