/*
 * test_waipo_step_ret.c
 *
 * 端到端验证调试器单步跨过 RET 时「下一条指令」的计算（_woort_WAIPO_get_next_ip
 * 的 RET 分支）。RET 后的落点是沿调用栈向上、第一个并非从 native 进入的帧的
 * 返回地址：
 *   1. 普通脚本帧（NEAR）：落点是调用者中 CALL 之后的指令；
 *   2. 从 native 进入的帧（FROM_NATIVE）：闭包 RET 后先回到 native，再由 native
 *      返回脚本——落点是调用者中 CALLNFP 之后的指令，中间越过 native 自己的帧；
 *      若 native 内再经 woort_invoke 调起另一个 native、由后者调起闭包，则连续
 *      越过两层 FROM_NATIVE 帧；
 *   3. 宿主调起的顶层帧：向上没有可回到的脚本帧，无法确定下一条指令，调试器
 *      须在原指令上再次回调（其间不得执行任何指令）。
 *
 * 第 1~3 项由断点处请求 STEPIR 触发（Debugger_active）。另以 STEPIN → STEPOVER /
 * STEPOUT 的组合覆盖「步进断点命中但未满足停下条件」的续走路径
 * （Debugger_meet_breakpoint 的 refill），该路径同样要跨过 RET。
 *
 * RET 分支经调用栈 trace 接口（woort_VMRuntime_trace_next）走链。trace 遇到返回地址
 * 为 NULL 的帧即止，而 native 运行期间 vm->m_ip 是它的函数地址（解释器、JIT 与
 * woort_spawn 的 native 分支一致），native 调起的脚本闭包以之作为帧内保存的返回
 * 地址，所以 trace 能穿过这些帧到达外层脚本帧。下面对 trace 深度与各帧调用方式
 * （m_call_way）的断言钉住这一点，其中第 2 项越过两层的情形覆盖经 C API 调起的
 * native 闭包。
 *
 * 对照基准 legacy_ret_next_ip 是上述走链的独立实现：直接读 VM 栈帧
 * （sb[1].m_ret_bp / sb[2].m_ret_addr），不经过调用栈 trace 接口。每次停在 RET 上
 * （或即将步越/步出该帧时）用它算出落点，断言调试器的下一次停顿恰好落在该地址。
 *
 * 程序：
 *   f2（行 20）：v = ADDI(a, b); ret v
 *   c （行 30）：v = ADDI(a, b); ret v        由 native 调起，共两次
 *   main：
 *     行 10  v0 = ADDI(a, b)
 *     行 12  r1 = CALLNWO(f2); r2 = CALLNWO(f2);
 *            rn = CALLNFP(native_call_closure, [c])      native → c
 *            rm = CALLNFP(native_call_native, [c, m])    native → m(native) → c
 *            v = ADDI(rn, rm); vr = ADDI(v, r2)
 *     行 11  ret vr
 *
 * 指令布局（偏移为字）：RET 本身不在行断点内，需从断点处逐指令步进过去
 *   f2  0..4   行 20：PUSHRCHK, LOAD, LOAD, CADDI, RETVS          断点在 0、3
 *   c   5..9   行 30：PUSHRCHK, LOAD, LOAD, CADDI, RETVS          断点在 5、8
 *   main 10..28
 *     10..14   行 10：PUSHRCHK, LOAD, LOAD, LOAD, CADDI            断点在 10、14
 *     15..27   行 12：CALLNWO(f2), RESULT, CALLNWO(f2), RESULT,
 *                      PUSHSCHK(c), CALLNFP(native_call_closure), RESULT,
 *                      PUSHCCHK(m), PUSHSCHK(c), CALLNFP(native_call_native),
 *                      RESULT, CADDI, CADDI
 *     28       行 11：RETVS                                    断点在 28
 */

#ifdef WOORT_STATIC_LIB

/* 混含内部头（woort_waipo_debugger.h 等）需要完整版 woort_Value，
   与库内编译单元一致地声明 WOORT_IMPL；静态库下 WOORT_API 为空，链接不受影响 */
#define WOORT_IMPL 1

#include "woort.h"
#include "woort_opcode.h"
#include "woort_waipo_debugger.h"
#include "woort_codeenv.h"

#include <stdio.h>
#include <stdint.h>

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

#define TEST_FILE "test_waipo_step_ret.wo"
#define LINE_MAIN_FIRST 10u /* main 首指令所在行 */
#define LINE_MAIN_RET 11u  /* main 的 ret */
#define LINE_MAIN 12u      /* main 其余指令 */
#define LINE_F2 20u        /* f2 */
#define LINE_C 30u         /* c */

#define MAX_TRAPS 64

static woort_CodeEnv* g_cenv = NULL;
static woort_IRConstantIndex g_c_closure; /* 脚本闭包 c */

static const woort_Bytecode* g_main_entry = NULL;
static const woort_Bytecode* g_f2_entry = NULL;

static const woort_Bytecode* g_trap_ips[MAX_TRAPS];
static size_t g_trap_count = 0;

/* 三个 native 各自被调起的次数，以及闭包 c 经它们返回的值 */
static size_t g_direct_calls = 0;
static size_t g_nested_calls = 0;
static size_t g_inner_calls = 0;
static woort_Int g_direct_got = 0;
static woort_Int g_inner_got = 0;

/* ===================================================================== */

/*
 * 对照基准：RET 之后的落点 = 沿栈向上第一个非 FROM_NATIVE 帧的返回地址。
 * out_skipped 返回越过的 FROM_NATIVE 帧数（可为 NULL）。
 * 返回 false 表示向上没有可回到的脚本帧。
 */
static bool legacy_ret_next_ip(
    woort_VMRuntime* vm,
    const woort_Bytecode** out_next_ip,
    /* OPTIONAL */ size_t* out_skipped)
{
    const woort_Value* trace_sb = vm->m_sb;
    size_t skipped = 0;

    while (trace_sb[1].m_ret_bp.m_way == WOORT_CALL_WAY_FROM_NATIVE)
    {
        trace_sb = trace_sb + 2 + trace_sb[1].m_ret_bp.m_bp_offset;
        if (vm->m_stack_end - trace_sb < 3)
            return false;
        ++skipped;
    }

    *out_next_ip = (const woort_Bytecode*)trace_sb[2].m_ret_addr;
    if (out_skipped != NULL)
        *out_skipped = skipped;
    return true;
}

/*
 * 走一遍调用栈 trace：返回帧数，并按由内向外在 out_ways 中记下前 max_count 帧的
 * 调用方式（out_ways 可为 NULL）。
 * 第 0 层是当前帧，其后每层来自更内一帧保存的返回地址，返回地址为 NULL 即止
 * （见 woort_VMRuntime_trace_next）。
 */
static size_t trace_call_ways(
    woort_VMRuntime* vm,
    /* OPTIONAL */ woort_CallWay* out_ways,
    size_t max_count)
{
    woort_VMRuntime_TraceCallstack_Iter iter;
    woort_VMRuntime_trace_begin(vm, &iter);

    woort_VMRuntime_TraceCallstack frame;
    size_t depth = 0;
    while (woort_VMRuntime_trace_next(&iter, out_ways != NULL ? &frame : NULL))
    {
        if (out_ways != NULL && depth < max_count)
            out_ways[depth] = frame.m_call_way;
        ++depth;
    }
    return depth;
}

/* ip 处的原始指令是否为 RET/RETV/RETVC（POPRS 不算返回） */
static bool ip_is_ret(const woort_Bytecode* ip)
{
    const woort_Bytecode bc = woort_CodeEnv_raw_trap(g_cenv, ip);
    const uint8_t op6 = (uint8_t)WOORT_BYTECODE(OP6, bc);
    const uint8_t m2 = (uint8_t)WOORT_BYTECODE(M2, bc);

    return op6 == (uint8_t)WOORT_OPCODE_RET && m2 != 3;
}

/* ip 所在函数的入口地址 */
static const woort_Bytecode* function_entry_of(const woort_Bytecode* ip)
{
    const woort_FunctionBoundary* const boundary =
        woort_CodeEnv_find_function_boundary_by_offset(
            g_cenv, (uint32_t)(ip - g_cenv->m_code_begin));
    if (boundary == NULL)
        return NULL;
    return g_cenv->m_code_begin + boundary->m_offset_begin;
}

static size_t record_trap(const woort_Bytecode* ip)
{
    const size_t index = g_trap_count;
    if (index < MAX_TRAPS)
        g_trap_ips[index] = ip;
    ++g_trap_count;

    const woort_Bytecode* const entry = function_entry_of(ip);
    printf("trap[%zu] %s+0x%02x%s\n",
        index,
        entry == g_main_entry ? "main" : entry == g_f2_entry ? "f2" : "c",
        (unsigned)(ip - g_cenv->m_code_begin),
        ip_is_ret(ip) ? " RET" : "");
    return index;
}

/* ===================================================================== */

/* ===== 场景 1：断点处 STEPIR 跨过 RET ===== */

static bool g_expect_pending = false;                    /* 已从 RET 步出，等待落点 */
static const woort_Bytecode* g_expect_ip = NULL;
static const woort_Bytecode* g_undeterminable_ip = NULL; /* 上次因无法确定落点而原地回调 */

static size_t g_near_steps = 0;
static size_t g_from_native_steps[3] = { 0, 0, 0 }; /* 下标为越过的 FROM_NATIVE 帧数 */
static size_t g_undeterminable_count = 0;

static woort_WAIPO_TrapEndBehavior step_ret_trap(woort_VMRuntime* vm)
{
    const woort_Bytecode* const ip = vm->m_ip;
    (void)record_trap(ip);

    /* 回归时不得原地无限回调 */
    if (g_trap_count > MAX_TRAPS)
        return WOORT_WAIPO_TRAP_CONTINUE;

    if (g_undeterminable_ip != NULL)
    {
        /* 无法确定落点后，下一次回调必须还在原指令上 */
        CHECK(ip == g_undeterminable_ip);
        g_undeterminable_ip = NULL;
        return WOORT_WAIPO_TRAP_CONTINUE;
    }

    if (ip_is_ret(ip))
    {
        /* 不得带着尚未兑现的落点期望遇到另一条 RET */
        CHECK(!g_expect_pending);

        const woort_Bytecode* next_ip = NULL;
        size_t skipped = 0;
        woort_CallWay ways[4];
        if (legacy_ret_next_ip(vm, &next_ip, &skipped))
        {
            if (vm->m_sb[1].m_ret_bp.m_way == WOORT_CALL_WAY_FROM_NATIVE)
            {
                /* c 由 native 调起：向上越过 native 的帧（直接调起 1 层，经 m 调起 2 层） */
                CHECK(skipped == 1 || skipped == 2);
                ++g_from_native_steps[skipped];

                /*
                 * trace 须穿过 c 与被越过的 native 帧到达 main，由内向外依次是：
                 * c（以及经 m 调起时 m 自己的帧）为 FROM_NATIVE，发起 CALLNFP 的
                 * native 帧为 NEAR，main 为顶层帧（FROM_NATIVE）；再往外无帧
                 */
                CHECK(trace_call_ways(vm, ways, 4) == 2 + skipped);
                for (size_t i = 0; i < skipped; ++i)
                    CHECK(ways[i] == WOORT_CALL_WAY_FROM_NATIVE);
                CHECK(ways[skipped] == WOORT_CALL_WAY_NEAR);
                CHECK(ways[skipped + 1] == WOORT_CALL_WAY_FROM_NATIVE);
            }
            else
            {
                CHECK(skipped == 0);
                ++g_near_steps;

                CHECK(trace_call_ways(vm, ways, 2) == 2); /* f2、main，再往外无帧 */
                CHECK(ways[0] == WOORT_CALL_WAY_NEAR);
                CHECK(ways[1] == WOORT_CALL_WAY_FROM_NATIVE);
            }
            g_expect_pending = true;
            g_expect_ip = next_ip;
        }
        else
        {
            ++g_undeterminable_count;
            g_undeterminable_ip = ip;

            /* 顶层帧：宿主调起（FROM_NATIVE），返回地址为 NULL，再往外无帧 */
            CHECK(trace_call_ways(vm, ways, 1) == 1);
            CHECK(ways[0] == WOORT_CALL_WAY_FROM_NATIVE);
        }
        return WOORT_WAIPO_TRAP_STEPIR;
    }

    if (g_expect_pending)
    {
        CHECK(ip == g_expect_ip);
        g_expect_pending = false;
        return WOORT_WAIPO_TRAP_CONTINUE;
    }

    /* 用户断点处 / 步进途中的普通指令：逐指令走到 RET */
    return WOORT_WAIPO_TRAP_STEPIR;
}

/* ===== 场景 2：STEPIN / STEPOVER / STEPOUT，续走路径（refill）跨过 RET ===== */

static const woort_WAIPO_TrapEndBehavior g_script[] = {
    /* [0] main 首指令（断点）     STEPIN   → 同行后续指令源码位置未变，续走 */
    /* [1] 同行主指令（断点）      STEPIN   用户断点压过步进续走，在此停下 */
    /* [2] CALLNWO(f2)             STEPIN   → 进入 f2 */
    /* [3] f2 入口                 STEPOVER 步进断点逐指令装到 f2 的 RET；RET 处
                                            源码位置未变，续走时跨过 RET */
    /* [4] 落回 main               STEPIN   → 进入 f2（第二次调用） */
    /* [5] f2 入口                 STEPOUT  RET 处仍在当前函数内，续走时跨过 RET */
    /* [6] 落回 main               CONTINUE */
    WOORT_WAIPO_TRAP_STEPIN,
    WOORT_WAIPO_TRAP_STEPIN,
    WOORT_WAIPO_TRAP_STEPIN,
    WOORT_WAIPO_TRAP_STEPOVER,
    WOORT_WAIPO_TRAP_STEPIN,
    WOORT_WAIPO_TRAP_STEPOUT,
    WOORT_WAIPO_TRAP_CONTINUE,
};
#define SCRIPT_LEN (sizeof(g_script) / sizeof(g_script[0]))

static const woort_Bytecode* g_out_expect = NULL; /* 步越/步出尚未兑现的落点 */
static const woort_Bytecode* g_landing_ips[2] = { NULL, NULL }; /* 两次落回 main 的地址 */

static woort_WAIPO_TrapEndBehavior scripted_trap(woort_VMRuntime* vm)
{
    const woort_Bytecode* const ip = vm->m_ip;
    const size_t index = record_trap(ip);
    CHECK(index < SCRIPT_LEN);
    if (index >= SCRIPT_LEN)
        return WOORT_WAIPO_TRAP_CONTINUE;

    switch (index)
    {
    case 0:
        CHECK(ip == g_main_entry);
        break;
    case 1:
    case 2:
        CHECK(ip != g_main_entry && function_entry_of(ip) == g_main_entry);
        break;
    case 3:
    case 5:
        /* 当前帧的 RET 落点：步越/步出都会在 f2 的 RET 处续走 */
        CHECK(ip == g_f2_entry);
        CHECK(g_out_expect == NULL);
        CHECK(legacy_ret_next_ip(vm, &g_out_expect, NULL));
        g_landing_ips[index == 3 ? 0 : 1] = g_out_expect;
        break;
    default:
        CHECK(ip == g_out_expect);
        CHECK(function_entry_of(ip) == g_main_entry);
        g_out_expect = NULL;
        break;
    }
    return g_script[index];
}

/* ===================================================================== */

typedef enum _TrapMode
{
    TRAP_MODE_STEP_RET,
    TRAP_MODE_SCRIPTED

} TrapMode;

static TrapMode g_trap_mode = TRAP_MODE_STEP_RET;

static woort_WAIPO_TrapEndBehavior trap_cb(
    woort_WAIPO_Debugger* debugger, woort_VMRuntime* vm)
{
    (void)debugger;

    return g_trap_mode == TRAP_MODE_STEP_RET
        ? step_ret_trap(vm)
        : scripted_trap(vm);
}

/* ===================================================================== */

/* 第 0 个参数是脚本闭包：经 woort_invoke 进入脚本，建立 FROM_NATIVE 帧 */
woort_api native_call_closure(void)
{
    ++g_direct_calls;

    if (woort_invoke(WOORT_RETURN_SLOT, 0) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke closure.");

    g_direct_got = woort_int(WOORT_RETURN_SLOT);
    return woort_ret();
}

/* 自身是 native，再经 woort_invoke 调起常量池里的脚本闭包 */
woort_api native_call_closure_const(void)
{
    ++g_inner_calls;

    woort_StackValue sv;
    if (!woort_push_reserve(2, &sv))
        return woort_ret_panic("Stack overflow.");

    woort_load_const(sv, g_cenv, g_c_closure);
    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    g_inner_got = woort_int(sv + 1);
    woort_pop(2);

    if (st == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke closure.");

    return woort_ret();
}

/* 第 1 个参数是 native 闭包：经 woort_invoke 调起它，形成连续两层 FROM_NATIVE 帧 */
woort_api native_call_native(void)
{
    ++g_nested_calls;

    if (woort_invoke(WOORT_IGNORE, 1) == WOORT_VM_CALL_STATUS_ABORTED)
        return woort_ret_panic("Failed to invoke closure.");

    return woort_ret_int(42);
}

/* 运行一次 main 闭包，返回执行结果（调用失败时为 -1） */
static woort_Int run_main_once(
    woort_IRConstantIndex c_main, woort_VmCallStatus* out_status)
{
    woort_VMRuntime* vm;
    (void)woort_VMRuntime_create(&vm);
    (void)woort_VMRuntime_swap(vm);

    woort_StackValue sv;
    (void)woort_push_reserve(2, &sv);
    woort_load_const(sv, g_cenv, c_main);
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

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    woort_IRCompiler* irc = woort_IRCompiler_create();

    /* 数据常量先于闭包常量分配（CodeEnv 约定） */
    woort_IRConstantIndex c_a = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_b = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_f2 = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_direct = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_nested = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_inner = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_c = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex c_main = woort_IRCompiler_add_constant(irc);

    woort_IRFunction* f2;
    woort_IRFunction* c;
    woort_IRFunction* f_main;
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f2);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &c);
    (void)woort_IRCompiler_add_function(irc, 0, 0, &f_main);
    {
        (void)woort_IRFunction_push_srcloc(f2, TEST_FILE, LINE_F2, 1, LINE_F2, 20);
        woort_IRValue* v = woort_IRFunction_new_vreg(f2);
        (void)woort_IR_ADDI(f2, v,
            woort_IRFunction_fetch_const(f2, c_a),
            woort_IRFunction_fetch_const(f2, c_b));
        (void)woort_IR_ret(f2, v);
        woort_IRFunction_pop_srcloc(f2);

        (void)woort_IRFunction_push_srcloc(c, TEST_FILE, LINE_C, 1, LINE_C, 20);
        woort_IRValue* cv = woort_IRFunction_new_vreg(c);
        (void)woort_IR_ADDI(c, cv,
            woort_IRFunction_fetch_const(c, c_a),
            woort_IRFunction_fetch_const(c, c_b));
        (void)woort_IR_ret(c, cv);
        woort_IRFunction_pop_srcloc(c);

        (void)woort_IRFunction_push_srcloc(f_main, TEST_FILE, LINE_MAIN_FIRST, 1, LINE_MAIN_FIRST, 40);
        woort_IRValue* v0 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_ADDI(f_main, v0,
            woort_IRFunction_fetch_const(f_main, c_a),
            woort_IRFunction_fetch_const(f_main, c_b));
        woort_IRFunction_pop_srcloc(f_main);

        (void)woort_IRFunction_push_srcloc(f_main, TEST_FILE, LINE_MAIN, 1, LINE_MAIN, 40);
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNWO(f_main, c_f2, 0, r1);

        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNWO(f_main, c_f2, 0, r2);

        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_c));
        woort_IRValue* rn = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNFP(f_main, c_direct, 1, rn);

        /* 参数逆序压栈（最后一个参数先压）：槽 0 为 c，槽 1 为 m */
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_inner));
        (void)woort_IR_PUSHCHK(f_main, woort_IRFunction_fetch_const(f_main, c_c));
        woort_IRValue* rm = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNFP(f_main, c_nested, 2, rm);

        woort_IRValue* vsum = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_ADDI(f_main, vsum, rn, rm);
        woort_IRValue* vr = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_ADDI(f_main, vr, vsum, r2);
        woort_IRFunction_pop_srcloc(f_main);

        (void)woort_IRFunction_push_srcloc(f_main, TEST_FILE, LINE_MAIN_RET, 1, LINE_MAIN_RET, 20);
        (void)woort_IR_ret(f_main, vr);
        woort_IRFunction_pop_srcloc(f_main);
    }

    (void)woort_IRCompiler_finish(irc, &g_cenv);

    const woort_Bytecode* f2_addr;
    const woort_Bytecode* c_addr;
    (void)woort_CodeEnv_query_function(g_cenv, f2, &f2_addr);
    (void)woort_CodeEnv_query_function(g_cenv, c, &c_addr);
    (void)woort_CodeEnv_query_function(g_cenv, f_main, &g_main_entry);
    g_f2_entry = f2_addr;
    g_c_closure = c_c;

    woort_CodeEnv_lock(g_cenv);
    woort_CodeEnv_set_const_int(g_cenv, c_a, 30);
    woort_CodeEnv_set_const_int(g_cenv, c_b, 12);
    /* CALLNWO 读常量槽的 m_script_function（裸函数地址），须用 script_function 而非闭包 */
    woort_CodeEnv_set_const_script_function(g_cenv, c_f2, f2_addr);
    woort_CodeEnv_set_const_script_closure(g_cenv, c_c, c_addr);
    woort_CodeEnv_set_const_script_closure(g_cenv, c_main, g_main_entry);
    (void)woort_CodeEnv_set_const_extern_function(g_cenv, c_direct, native_call_closure, NULL, NULL);
    (void)woort_CodeEnv_set_const_extern_function(g_cenv, c_nested, native_call_native, NULL, NULL);
    (void)woort_CodeEnv_set_const_extern_closure(g_cenv, c_inner, native_call_closure_const, NULL, NULL);
    woort_CodeEnv_unlock(g_cenv);

    /* ===== 0. 无调试器：结果正确，三条 native 路径各走一次 ===== */
    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 42 + 42 + 42);
        CHECK(g_direct_calls == 1);
        CHECK(g_nested_calls == 1);
        CHECK(g_inner_calls == 1);
        CHECK(g_direct_got == 42);
        CHECK(g_inner_got == 42);
    }

    /* 空闲的 VM（尚未调起任何函数，m_sb 停在栈顶）：第 0 层按宿主边界计为 FROM_NATIVE */
    {
        woort_VMRuntime* vm;
        (void)woort_VMRuntime_create(&vm);
        (void)woort_VMRuntime_swap(vm);

        woort_VMRuntime_TraceCallstack_Iter iter;
        woort_VMRuntime_trace_begin(vm, &iter);

        woort_VMRuntime_TraceCallstack frame;
        CHECK(woort_VMRuntime_trace_next(&iter, &frame));
        CHECK(frame.m_call_way == WOORT_CALL_WAY_FROM_NATIVE);

        (void)woort_VMRuntime_swap(NULL);
        woort_VMRuntime_destroy(vm);
    }

    woort_WAIPO_Debugger* dbg = NULL;
    CHECK(woort_WAIPO_Debugger_attach(&trap_cb, &dbg)
        == WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS);

    /* ===== 1. 断点处 STEPIR 跨过 RET：普通帧 / 从 native 进入的帧 / 顶层帧 ===== */
    woort_WAIPO_Debugger_BreakpointId id_f2 = 0;
    woort_WAIPO_Debugger_BreakpointId id_c = 0;
    woort_WAIPO_Debugger_BreakpointId id_ret = 0;
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(dbg, TEST_FILE, LINE_F2, &id_f2));
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(dbg, TEST_FILE, LINE_C, &id_c));
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(dbg, TEST_FILE, LINE_MAIN_RET, &id_ret));

    g_trap_mode = TRAP_MODE_STEP_RET;
    g_trap_count = 0;
    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 42 + 42 + 42); /* 步进不得破坏执行 */
        CHECK(g_direct_calls == 2);
        CHECK(g_nested_calls == 2);
        CHECK(g_inner_calls == 2);

        /*
         * f2 两次调用、c 两次调用各「入口 → 3 条指令 → RET → 落回 main」6 次停顿，
         * main 的 ret 处「无法确定 → 原地再次回调」2 次
         */
        CHECK(g_trap_count == 6 * 4 + 2);
        CHECK(g_near_steps == 2);
        CHECK(g_from_native_steps[0] == 0);
        CHECK(g_from_native_steps[1] == 1);
        CHECK(g_from_native_steps[2] == 1);
        CHECK(g_undeterminable_count == 1);
        CHECK(!g_expect_pending);
        CHECK(g_undeterminable_ip == NULL);

        /* 无法确定落点的两次回调之间，VM 的指令指针保持不动 */
        CHECK(g_trap_count >= 2
            && g_trap_ips[g_trap_count - 1] == g_trap_ips[g_trap_count - 2]);
    }

    CHECK(woort_WAIPO_Debugger_delete_breakpoint(dbg, id_f2));
    CHECK(woort_WAIPO_Debugger_delete_breakpoint(dbg, id_c));
    CHECK(woort_WAIPO_Debugger_delete_breakpoint(dbg, id_ret));

    /* ===== 2. STEPIN / STEPOVER / STEPOUT：续走路径（refill）跨过 RET ===== */
    woort_WAIPO_Debugger_BreakpointId id_first = 0;
    CHECK(woort_WAIPO_Debugger_set_source_breakpoint(dbg, TEST_FILE, LINE_MAIN_FIRST, &id_first));

    g_out_expect = NULL;
    g_trap_mode = TRAP_MODE_SCRIPTED;
    g_trap_count = 0;
    {
        woort_VmCallStatus st;
        const woort_Int r = run_main_once(c_main, &st);
        CHECK(st == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(r == 42 + 42 + 42);
        CHECK(g_trap_count == SCRIPT_LEN);
        CHECK(g_out_expect == NULL); /* 两次落点均已兑现 */

        /* 分别落回第一个、第二个 CALLNWO 之后 */
        CHECK(g_landing_ips[0] != NULL
            && g_landing_ips[1] != NULL
            && g_landing_ips[0] < g_landing_ips[1]
            && function_entry_of(g_landing_ips[0]) == g_main_entry);
    }

    /* ===== 清理 ===== */
    woort_CodeEnv_drop(g_cenv);
    woort_IRCompiler_close(irc);

    woort_shutdown(NULL, NULL);

    if (g_failures == 0)
    {
        printf("test_waipo_step_ret: ALL PASS\n");
        return 0;
    }
    printf("test_waipo_step_ret: %d FAILURES\n", g_failures);
    return 1;
}

#else
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
}
#endif
