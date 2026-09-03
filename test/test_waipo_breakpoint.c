/*
 * test_waipo_breakpoint.c
 *
 * 端到端验证 WAIPO 调试器用户断点（break 命令背后的无条件断点）：
 *   1. 未设断点时执行不产生任何中断；
 *   2. set_source_breakpoint 后，断点行 srcloc 段首指令地址进入无条件断点表
 *      （m_debug_breakpoints），任意 VM 命中即进入 TrapCallback；
 *   3. 同一地址被两条断点共享时，删除其一仍保留 TRAP，全部删除后不再中断；
 *   4. 步越（STEPOVER）进入更深层调用本应继续步进时，命中被调函数入口的
 *      用户断点必须立即无条件中断——这正是 m_debug_breakpoints 记账的意义：
 *      若该表未被记录，步越会静默穿过断点，直到无法确定下一条指令才中断。
 *
 * 指令布局（srcloc 条目只在变化时生成，行断点覆盖 srcloc 段首指令）：
 *   main（全部同一 srcloc，行 5）：
 *     v0 = ADDI(30, 12); r1 = CALLNWO(f2); r2 = CALLNWO(f2); ret ADDI(r2, 1)
 *   f2（全部同一 srcloc，行 20）：
 *     v = ADDI(30, 12); ret v
 */

#ifdef WOORT_STATIC_LIB

/* 混含内部头（woort_waipo_debugger.h 等）需要完整版 woort_Value，
   与库内编译单元一致地声明 WOORT_IMPL；静态库下 WOORT_API 为空，链接不受影响 */
#define WOORT_IMPL 1

#include "woort.h"
#include "woort_waipo_debugger.h"
#include "woort_codeenv.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ===================================================================== */

static woort_WAIPO_Debugger* g_dbg = NULL;

static size_t g_trap_count = 0;
static uint32_t g_trap_lines[16] = { 0 };

/* 置位后下一次 TrapCallback 返回 STEPOVER（进入步越），随后恢复 CONTINUE */
static bool g_stepover_once = false;

static woort_WAIPO_TrapEndBehavior trap_counter_cb(
    woort_WAIPO_Debugger* debugger, woort_VMRuntime* vm)
{
    (void)debugger;

    if (g_trap_count < 16)
    {
        g_trap_lines[g_trap_count] = UINT32_MAX;
        woort_SourceLocation loc;
        if (woort_CodeEnv_find_srcloc_by_offset(
                vm->m_env,
                (uint32_t)(vm->m_ip - vm->m_env->m_code_begin),
                &loc))
        {
            g_trap_lines[g_trap_count] = loc.m_begin_line;
        }
    }
    ++g_trap_count;

    if (g_stepover_once)
    {
        g_stepover_once = false;
        return WOORT_WAIPO_TRAP_STEPOVER;
    }
    return WOORT_WAIPO_TRAP_CONTINUE;
}

/* ===================================================================== */

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

/* 收集某源码行关联的全部指令地址（与 break 命令相同的正向映射） */
typedef struct _LineIpsContext
{
    woort_CodeEnv* m_cenv;
    const woort_Bytecode* m_ips[16];
    size_t m_count;
} _LineIpsContext;

static bool collect_line_ip_callback(uint32_t bytecode_offset, void* user_data)
{
    _LineIpsContext* ctx = (_LineIpsContext*)user_data;
    if (ctx->m_count < 16)
        ctx->m_ips[ctx->m_count++] =
            ctx->m_cenv->m_code_begin + bytecode_offset;
    return true;
}

typedef struct _QueryCountContext
{
    size_t m_count;
} _QueryCountContext;

static bool count_breakpoint_callback(
    const woort_WAIPO_Debugger_BreakpointInfo* info, void* user_data)
{
    (void)info;
    _QueryCountContext* ctx = (_QueryCountContext*)user_data;
    ++ctx->m_count;
    return true;
}

static size_t query_breakpoint_count(void)
{
    _QueryCountContext ctx;
    ctx.m_count = 0;
    (void)woort_WAIPO_Debugger_query_breakpoints(
        g_dbg, &count_breakpoint_callback, &ctx);
    return ctx.m_count;
}

/* 运行一次 main 闭包，返回执行结果（调用失败时为 -1） */
static woort_Int run_main_once(
    woort_CodeEnv* cenv, woort_IRConstantIndex c_main, woort_VmCallStatus* out_status)
{
    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, cenv, c_main);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);

    const woort_Int result =
        (st == WOORT_VM_CALL_STATUS_NORMAL) ? woort_int(sv + 1) : (woort_Int)-1;

    woort_pop(2);

    (void)woort_VMRuntime_swap(NULL);
    woort_VMRuntime_destroy(vm);

    if (out_status != NULL)
        *out_status = st;
    return result;
}

/* ===================================================================== */

#define TEST_FILE "test_waipo_breakpoint.wo"
#define LINE_MAIN 5u  /* main 全部指令（行断点只覆盖首指令 v0） */
#define LINE_FUNC 20u /* f2 全部指令（行断点只覆盖入口） */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    /*
     * 构造程序（不走 JIT，断点 TRAP 作用于字节码）：
     *   main: v0 = ADDI(30,12); r1 = CALLNWO(f2); r2 = CALLNWO(f2); ret ADDI(r2,1)
     *   f2:   v = ADDI(30,12); ret v
     * main 调用 f2 两次，f2 入口行断点会被命中两次。
     */
    woort_IRCompiler* irc = woort_IRCompiler_create();

    /* 数据常量先于闭包常量分配（CodeEnv 约定） */
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_one = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f2;
    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f2);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IRFunction_push_srcloc(f2, TEST_FILE, LINE_FUNC, 1, LINE_FUNC, 20);
        woort_IRValue* v = woort_IRFunction_new_vreg(f2);
        (void)woort_IR_ADDI(f2, v,
            woort_IRFunction_fetch_const(f2, c_a),
            woort_IRFunction_fetch_const(f2, c_b));
        (void)woort_IR_ret(f2, v);
        woort_IRFunction_pop_srcloc(f2);

        (void)woort_IRFunction_push_srcloc(f_main, TEST_FILE, LINE_MAIN, 1, LINE_MAIN, 40);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_ADDI(f_main, v0,
            woort_IRFunction_fetch_const(f_main, c_a),
            woort_IRFunction_fetch_const(f_main, c_b));

        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNWO(f_main, c_f2, 0, r1);

        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNWO(f_main, c_f2, 0, r2);

        woort_IRValue* vr = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_ADDI(f_main, vr, r2,
            woort_IRFunction_fetch_const(f_main, c_one));
        (void)woort_IR_ret(f_main, vr);
        woort_IRFunction_pop_srcloc(f_main);
    }

    woort_CodeEnv* cenv;
    (void)woort_IRCompiler_finish(irc, &cenv);

    const woort_Bytecode* f2_addr;
    const woort_Bytecode* main_addr;
    (void)woort_CodeEnv_query_function(cenv, f2, &f2_addr);
    (void)woort_CodeEnv_query_function(cenv, f_main, &main_addr);

    woort_CodeEnv_lock(cenv);
    woort_CodeEnv_set_const_int(cenv, c_a, 30);
    woort_CodeEnv_set_const_int(cenv, c_b, 12);
    woort_CodeEnv_set_const_int(cenv, c_one, 1);
    /* CALLNWO 读常量槽的 m_script_function（裸函数地址），须用 script_function 而非闭包 */
    woort_CodeEnv_set_const_script_function(cenv, c_f2, f2_addr);
    woort_CodeEnv_set_const_script_closure(cenv, c_main, main_addr);
    woort_CodeEnv_unlock(cenv);

    CHECK(woort_WAIPO_Debugger_attach(&trap_counter_cb, &g_dbg)
        == WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS);

    /* ===== 1. 未设断点：不中断，结果正确 ===== */
    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(cenv, c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 43);
        CHECK(g_trap_count == 0);
    }

    /* ===== 2. 设断点：f2 入口地址进入无条件断点表 ===== */
    woort_WAIPO_Debugger_BreakpointId id1 = 0;
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(
        g_dbg, TEST_FILE, LINE_FUNC, &id1));
    CHECK(id1 != 0);
    CHECK(query_breakpoint_count() == 1);

    _LineIpsContext line_ips;
    line_ips.m_cenv = cenv;
    line_ips.m_count = 0;
    CHECK(woort_CodeEnv_foreach_offset_by_srcloc(
        cenv, TEST_FILE, LINE_FUNC, &collect_line_ip_callback, &line_ips));
    CHECK(line_ips.m_count >= 1); /* f2 入口与 RET 各成一条 srcloc 段 */
    for (size_t i = 0; i < line_ips.m_count; ++i)
    {
        CHECK(_woort_WAIPO_BreakpointCollection_contains_debug_break_at(
            &g_dbg->m_breakpoint_collection, line_ips.m_ips[i]));
    }

    /*
     * ===== 3. 命中即中断 =====
     * 行 20 有入口与 RET 两个 srcloc 条目、f2 被调用两次，
     * 非关注中的 VM 每次到达都无条件中断：共 4 次。
     */
    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(cenv, c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 43);
        CHECK(g_trap_count == 4);
        CHECK(g_trap_lines[0] == LINE_FUNC);
        CHECK(g_trap_lines[1] == LINE_FUNC);
        CHECK(g_trap_lines[2] == LINE_FUNC);
        CHECK(g_trap_lines[3] == LINE_FUNC);
    }

    /* ===== 4. 地址共享计数：删一条留一条仍中断，删空后不再中断 ===== */
    woort_WAIPO_Debugger_BreakpointId id2 = 0;
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(
        g_dbg, TEST_FILE, LINE_FUNC, &id2));
    CHECK(query_breakpoint_count() == 2);

    CHECK(woort_WAIPO_Debugger_delete_breakpoint(g_dbg, id1));
    CHECK(query_breakpoint_count() == 1);
    for (size_t i = 0; i < line_ips.m_count; ++i)
    {
        /* 仍有一条断点持有地址，无条件断点表不得摘除 */
        CHECK(_woort_WAIPO_BreakpointCollection_contains_debug_break_at(
            &g_dbg->m_breakpoint_collection, line_ips.m_ips[i]));
    }
    CHECK(!woort_WAIPO_Debugger_delete_breakpoint(g_dbg, id1)); /* 编号不复用 */

    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(cenv, c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 43);
        CHECK(g_trap_count == 8);
        for (size_t i = 4; i < 8; ++i)
            CHECK(g_trap_lines[i] == LINE_FUNC);
    }

    CHECK(woort_WAIPO_Debugger_delete_breakpoint(g_dbg, id2));
    for (size_t i = 0; i < line_ips.m_count; ++i)
    {
        CHECK(!_woort_WAIPO_BreakpointCollection_contains_debug_break_at(
            &g_dbg->m_breakpoint_collection, line_ips.m_ips[i]));
    }

    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(cenv, c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 43);
        CHECK(g_trap_count == 8); /* TRAP 已清，不再中断 */
    }

    /* ===== 5. 无条件中断优先于步越的续走 ===== */
    /*
     * 行 5 断点覆盖 main 首指令与后继指令两处。在 main 首指令处 STEPOVER 后：
     *   回调 8（行 5）：main 首指令，返回 STEPOVER 进入步越；
     *   回调 9（行 5）：步越断点装到同行的后继指令上，源码位置未变本应
     *                  继续步越——但该处是用户断点，立即无条件中断；
     *   回调 10-13（行 20）：放行后 f2 两次调用的入口与 RET 各中断一次。
     * 若无条件断点表未被记账，步越会静默穿过行 5 后继指令与 f2 全部断点，
     * 直到 main 返回处无法确定下一条指令才中断：只有 4 次回调（共 12），
     * 且第 2 次回调后紧跟的不是 f2 的行 20 中断序列。
     */
    woort_WAIPO_Debugger_BreakpointId id_main = 0;
    woort_WAIPO_Debugger_BreakpointId id_f2 = 0;
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(
        g_dbg, TEST_FILE, LINE_MAIN, &id_main));
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(
        g_dbg, TEST_FILE, LINE_FUNC, &id_f2));

    g_stepover_once = true;
    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(cenv, c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 43);
        CHECK(g_trap_count == 14);
        CHECK(g_trap_lines[8] == LINE_MAIN);  /* STEPOVER 起点 */
        CHECK(g_trap_lines[9] == LINE_MAIN);  /* 步越续走被用户断点压下 */
        CHECK(g_trap_lines[10] == LINE_FUNC); /* f2 第一次入口/RET */
        CHECK(g_trap_lines[11] == LINE_FUNC);
        CHECK(g_trap_lines[12] == LINE_FUNC); /* f2 第二次入口/RET */
        CHECK(g_trap_lines[13] == LINE_FUNC);
    }

    CHECK(woort_WAIPO_Debugger_delete_breakpoint(g_dbg, id_main));
    CHECK(woort_WAIPO_Debugger_delete_breakpoint(g_dbg, id_f2));
    CHECK(query_breakpoint_count() == 0);

    /* ===== 清理 ===== */
    woort_CodeEnv_drop(cenv);
    woort_IRCompiler_close(irc);

    woort_shutdown(NULL, NULL);

    if (g_failures == 0)
    {
        printf("test_waipo_breakpoint: ALL PASS\n");
        return 0;
    }
    printf("test_waipo_breakpoint: %d FAILURES, trap lines:", g_failures);
    for (size_t i = 0; i < g_trap_count && i < 16; ++i)
        printf(" %u", g_trap_lines[i]);
    printf("\n");
    return 1;
}

#else
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
}
#endif
