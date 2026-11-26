/*
 * 槽位窗口（V2）：实参 / 元素直接写入函数入口预留的栈槽，消费指令改用
 * 不弹栈的 MK*NP / RESULTNP 读取，函数体内 sp 不动。
 *
 * 每个用例同时检查运行结果和生成的字节码：字节码经反汇编捕获成文本，
 * 按助记符计数（PUSHSCHK / PUSHCCHK / SPSET / MK*NP ...），并直接解码
 * 函数入口 PUSHRCHK 的操作数。
 *
 * 每个用例先在解释器下跑一遍，再在 woort_CodeEnv_jit 之后跑一遍；
 * JIT 默认是否开启取决于构建类型（Debug 关、RelWithDebInfo 开）。
 *
 * 需要访问编译器内部结构（m_committed_codes / IRFunction 字段），
 * 内部头会引入完整版 woort_Value 定义，因此以 WOORT_IMPL 方式包含公共头
 * （同 test_ir_stack_margin.c）。
 */
#define WOORT_IMPL
#include "woort.h"

#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_opcode_formal.h"
#include "woort_disassembly.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ========== Test Infrastructure ========== */

static int g_tests_run = 0;
static int g_tests_passed = 0;

/* 用例内使用局部变量 h（_Harness）；结束时关闭并统计 */
#define TEST_BEGIN(name)                                        \
    do {                                                        \
        const char* _test_name = (name);                        \
        g_tests_run++;                                          \
        (void)printf("  [TEST] %-45s ", _test_name);            \
        fflush(stdout);

#define TEST_END()                                              \
    if (h.failures == 0) { (void)printf("PASS\n"); g_tests_passed++; } \
    else (void)printf("FAIL (%d)\n", h.failures);               \
    h_close(&h);                                                \
    } while (0)

/* 结束编译；结构错误时登记失败并结束用例 */
#define PREPARE_OR_RETURN()                                     \
    do {                                                        \
        if (!h_prepare(&h))                                     \
        {                                                       \
            (void)printf("FAIL (woort_IRCompiler_finish failed)\n"); \
            h_close(&h);                                        \
            return;                                             \
        }                                                       \
    } while (0)

/* 槽位下标与常量的简写 */
#define SLOT(w, i) woort_IRWindow_slot((w), (i))
#define CONST(f, i) woort_IRFunction_fetch_const((f), (i))

#define FAIL(h, msg)                                                        \
    do {                                                                    \
        (void)printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));    \
        ++(h)->failures;                                                    \
    } while (0)

#define CHECK(h, cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            (void)printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++(h)->failures;                                                 \
        }                                                                    \
    } while (0)

/* ---------- 常量绑定登记 ---------- */

typedef enum _BindKind
{
    BIND_INT,
    BIND_REAL,
    BIND_BOX_INT,
    BIND_SCRIPT_FUNCTION,
    BIND_CLOSURE,
    BIND_NATIVE,

} _BindKind;

typedef struct _Bind
{
    _BindKind m_kind;
    woort_IRConstantIndex m_idx;
    union
    {
        woort_Int m_int;
        woort_Real m_real;
        const woort_IRFunction* m_function;
        woort_NativeFunction m_native;
    };

} _Bind;

#define MAX_BINDS 512

typedef struct _Harness
{
    woort_IRCompiler* irc;
    woort_CodeEnv* cenv;        /* h_prepare 之后 */
    woort_VMRuntime* vm;        /* h_start 之后 */
    woort_StackValue sv;        /* 实参 0 在 sv，入口闭包与返回值在 sv + 1 */
    bool pushed;
    woort_VmCallStatus status;

    _Bind binds[MAX_BINDS];
    size_t bind_count;

    int failures;

} _Harness;

static void h_init(_Harness* h)
{
    memset(h, 0, sizeof(*h));
    h->status = WOORT_VM_CALL_STATUS_ABORTED;
    h->irc = woort_IRCompiler_create();
}

static void h_close(_Harness* h)
{
    if (h->vm != NULL)
    {
        if (h->pushed)
            woort_pop(3);
        (void)woort_VMRuntime_swap(NULL);
        woort_VMRuntime_destroy(h->vm);
    }
    if (h->cenv != NULL)
        woort_CodeEnv_drop(h->cenv);
    if (h->irc != NULL)
        woort_IRCompiler_close(h->irc);
    h->vm = NULL;
    h->cenv = NULL;
    h->irc = NULL;
}

static _Bind* h_add_bind(_Harness* h, _BindKind kind, woort_IRConstantIndex idx)
{
    if (h->bind_count >= MAX_BINDS)
    {
        ++h->failures;
        return NULL;
    }

    _Bind* const b = &h->binds[h->bind_count++];
    b->m_kind = kind;
    b->m_idx = idx;
    return b;
}

static void h_int(_Harness* h, woort_IRConstantIndex idx, woort_Int value)
{
    _Bind* const b = h_add_bind(h, BIND_INT, idx);
    if (b != NULL)
        b->m_int = value;
}

static void h_real(_Harness* h, woort_IRConstantIndex idx, woort_Real value)
{
    _Bind* const b = h_add_bind(h, BIND_REAL, idx);
    if (b != NULL)
        b->m_real = value;
}

static void h_box_int(_Harness* h, woort_IRConstantIndex idx, woort_Int value)
{
    _Bind* const b = h_add_bind(h, BIND_BOX_INT, idx);
    if (b != NULL)
        b->m_int = value;
}

/* CALLNWO2 的目标：按常量区下标直接调用的脚本函数 */
static void h_function(_Harness* h, woort_IRConstantIndex idx, const woort_IRFunction* f)
{
    _Bind* const b = h_add_bind(h, BIND_SCRIPT_FUNCTION, idx);
    if (b != NULL)
        b->m_function = f;
}

/* CALL2 的目标，或宿主经 woort_invoke 调用的入口：脚本闭包 */
static void h_closure(_Harness* h, woort_IRConstantIndex idx, const woort_IRFunction* f)
{
    _Bind* const b = h_add_bind(h, BIND_CLOSURE, idx);
    if (b != NULL)
        b->m_function = f;
}

static void h_native(_Harness* h, woort_IRConstantIndex idx, woort_NativeFunction fn)
{
    _Bind* const b = h_add_bind(h, BIND_NATIVE, idx);
    if (b != NULL)
        b->m_native = fn;
}

/* 结束编译并绑定登记过的常量；结构错误时 woort_IRCompiler_finish 返回 false */
static bool h_prepare(_Harness* h)
{
    if (!woort_IRCompiler_finish(h->irc, &h->cenv))
        return false;

    woort_CodeEnv_lock(h->cenv);
    for (size_t i = 0; i < h->bind_count; ++i)
    {
        const _Bind* const b = &h->binds[i];
        switch (b->m_kind)
        {
        case BIND_INT:
            woort_CodeEnv_set_const_int(h->cenv, b->m_idx, b->m_int);
            break;
        case BIND_REAL:
            woort_CodeEnv_set_const_real(h->cenv, b->m_idx, b->m_real);
            break;
        case BIND_BOX_INT:
            woort_CodeEnv_set_const_box_int(h->cenv, b->m_idx, b->m_int);
            break;
        case BIND_SCRIPT_FUNCTION:
        case BIND_CLOSURE:
        {
            const woort_Bytecode* addr = NULL;
            (void)woort_CodeEnv_query_function(
                h->cenv, (woort_IRFunction*)b->m_function, &addr);
            if (b->m_kind == BIND_SCRIPT_FUNCTION)
                woort_CodeEnv_set_const_script_function(h->cenv, b->m_idx, addr);
            else
                woort_CodeEnv_set_const_script_closure(h->cenv, b->m_idx, addr);
            break;
        }
        case BIND_NATIVE:
            (void)woort_CodeEnv_set_const_extern_function(
                h->cenv, b->m_idx, b->m_native, NULL, NULL);
            break;
        default:
            break;
        }
    }
    woort_CodeEnv_unlock(h->cenv);
    return true;
}

static void h_start(_Harness* h)
{
    if (!woort_VMRuntime_create(&h->vm))
        return;
    (void)woort_VMRuntime_swap(h->vm);
}

/*
 * 调用常量 entry 处的闭包，带一个整数实参 arg（无参函数忽略它）。
 * 返回后结果在 h->sv + 1。
 */
static void h_call(_Harness* h, woort_IRConstantIndex entry, woort_Int arg)
{
    h->status = WOORT_VM_CALL_STATUS_ABORTED;

    if (h->vm == NULL)
        return;
    if (h->pushed)
        woort_pop(3);

    if (!woort_push_reserve(3, &h->sv))
        return;
    h->pushed = true;

    /*
     * 约定同 test_jit_*：push_reserve(3) 得到的 sv 是被调帧的实参 0，
     * 函数值放在 sv + 1，woort_invoke(结果槽, 函数槽) 返回后结果也在 sv + 1。
     */
    woort_load_const(h->sv + 1, h->cenv, entry);
    woort_set_int(h->sv, arg);
    h->status = woort_invoke(h->sv + 1, h->sv + 1);
}

/* 把 sv + 1 处的结果拆箱，返回类型标签；out_int / out_real 可为 NULL */
static woort_BoxValueType h_unbox(_Harness* h, /* OPTIONAL */ woort_Int* out_int,
    /* OPTIONAL */ woort_Real* out_real)
{
    woort_StackValue tmp;
    if (!woort_push_reserve(1, &tmp))
        return (woort_BoxValueType)-1;

    const woort_BoxValueType type = woort_unbox(tmp, h->sv + 1);
    if (out_int != NULL)
        *out_int = woort_int(tmp);
    if (out_real != NULL)
        *out_real = woort_real(tmp);

    woort_pop(1);
    return type;
}

/* ---------- 字节码检查：把一个函数反汇编成文本 ---------- */

#define MAX_LINES 2048

static char g_lines[MAX_LINES][96];
static int g_line_count; /* 已收录的行数 */

/*
 * 非 NULL 时只收录以其中某个前缀开头的行，供指令数超过 MAX_LINES 的大函数使用
 * （见 dump_function_filtered）。dump_function 用完复位。
 */
static const char* const* g_dump_filter;

static int capture_line(const char* format, ...)
{
    char text[sizeof(g_lines[0])];

    va_list ap;
    va_start(ap, format);
    (void)vsnprintf(text, sizeof(text), format, ap);
    va_end(ap);

    bool keep = true;
    if (g_dump_filter != NULL)
    {
        keep = false;
        for (size_t k = 0; g_dump_filter[k] != NULL; ++k)
        {
            if (strncmp(text, g_dump_filter[k], strlen(g_dump_filter[k])) == 0)
            {
                keep = true;
                break;
            }
        }
    }

    if (keep && g_line_count < MAX_LINES)
    {
        memcpy(g_lines[g_line_count], text, sizeof(text));
        ++g_line_count;
    }

    /* 返回值未使用，与回调约定的打印字符数无关 */
    return (int)strlen(text);
}

/* 捕获一个函数的全部字节码；须在 h_prepare 之后、h_close 之前调用 */
static void dump_function(const _Harness* h, const woort_IRFunction* f)
{
    const woort_Bytecode* pc =
        (const woort_Bytecode*)h->irc->m_committed_codes.m_data + f->m_code_offset;
    const woort_Bytecode* const end = pc + f->m_code_length;

    g_line_count = 0;
    while (pc < end)
        pc = woort_disassembly(pc, capture_line);
}

/* 只捕获以 filter 中某个助记符开头的指令（filter 以 NULL 结尾） */
static void dump_function_filtered(
    const _Harness* h, const woort_IRFunction* f, const char* const* filter)
{
    g_dump_filter = filter;
    dump_function(h, f);
    g_dump_filter = NULL;
}

/* 以 mnemonic 为完整助记符的指令条数（MKVEC 不匹配 MKVECNP / MKVECEXT） */
static int count_mnemonic(const char* mnemonic)
{
    const size_t len = strlen(mnemonic);
    int count = 0;

    for (int i = 0; i < g_line_count && i < MAX_LINES; ++i)
    {
        if (strncmp(g_lines[i], mnemonic, len) == 0 &&
            (g_lines[i][len] == ' ' || g_lines[i][len] == '\n' ||
                g_lines[i][len] == '\0'))
            ++count;
    }
    return count;
}

static int count_lines_with(const char* needle)
{
    int count = 0;
    for (int i = 0; i < g_line_count && i < MAX_LINES; ++i)
    {
        if (strstr(g_lines[i], needle) != NULL)
            ++count;
    }
    return count;
}

/* 带栈检查的压栈条数 */
static int count_push_checks(void)
{
    return count_mnemonic("PUSHSCHK") + count_mnemonic("PUSHCCHK");
}

/* 函数入口 PUSHRCHK 的操作数；首字不是 PUSHRCHK 时返回 -1 */
static int64_t entry_reserve(const _Harness* h, const woort_IRFunction* f)
{
    const woort_Bytecode bc =
        ((const woort_Bytecode*)h->irc->m_committed_codes.m_data)[f->m_code_offset];

    if (WOORT_BYTECODE(OPM8, bc) != WOORT_BYTECODE(OPM8, woort_OpCode_PUSHRCHK(0)))
        return -1;

    return (int64_t)WOORT_BYTECODE(ABC24, bc);
}

/* ---------- native 函数 ---------- */

woort_api native_sub2(void)
{
    return woort_ret_int(woort_int(0) - woort_int(1));
}

woort_api native_double1(void)
{
    return woort_ret_int(woort_int(0) * 2);
}

woort_api native_zero(void)
{
    return woort_ret_int(42);
}

/* ---------- 常用的小函数 ---------- */

/* 两个整数参数：a * p + b * q（常量 c_p、c_q 由调用方分配并绑定） */
static woort_IRFunction* make_lin2(woort_IRCompiler* irc, woort_IRConstantIndex c_p,
    woort_IRConstantIndex c_q)
{
    woort_IRFunction* f = NULL;
    if (!woort_IRCompiler_add_function(irc, 2, 0, &f))
        return NULL;

    const woort_IRValue* a = woort_IRFunction_get_argument(f, 0);
    const woort_IRValue* b = woort_IRFunction_get_argument(f, 1);
    woort_IRValue* ta = woort_IRFunction_new_vreg(f);
    woort_IRValue* tb = woort_IRFunction_new_vreg(f);
    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    (void)woort_IR_MULI(f, ta, a, CONST(f, c_p));
    (void)woort_IR_MULI(f, tb, b, CONST(f, c_q));
    (void)woort_IR_ADDI(f, r, ta, tb);
    (void)woort_IR_ret(f, r);
    return f;
}

/* 一个整数参数：x + v（常量 c_v 由调用方分配并绑定） */
static woort_IRFunction* make_addconst(woort_IRCompiler* irc, woort_IRConstantIndex c_v)
{
    woort_IRFunction* f = NULL;
    if (!woort_IRCompiler_add_function(irc, 1, 0, &f))
        return NULL;

    const woort_IRValue* x = woort_IRFunction_get_argument(f, 0);
    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    (void)woort_IR_ADDI(f, r, x, CONST(f, c_v));
    (void)woort_IR_ret(f, r);
    return f;
}

/*
 * 用 MKSTRUCT2 造一个三个字段的结构体写入 st：
 * 整数 c0、实数 c1、整数 c2。
 */
static void build_struct3(woort_IRFunction* f, woort_IRValue* st, woort_IRConstantIndex c0,
    woort_IRConstantIndex c1, woort_IRConstantIndex c2)
{
    woort_IRWindow* w = woort_IRFunction_new_window(f, 3);
    (void)woort_IR_MOV(f, SLOT(w, 0), CONST(f, c0));
    (void)woort_IR_MOV(f, SLOT(w, 1), CONST(f, c1));
    (void)woort_IR_MOV(f, SLOT(w, 2), CONST(f, c2));
    (void)woort_IR_MKSTRUCT2(f, st, w);
}

/* ========== 1. CALLNWO2：实参顺序、无压栈、sp 不动 ========== */

static void test_call_window_basic(void)
{
    TEST_BEGIN("call_window_basic (CALLNWO2, 2 slots)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口闭包，1 = sub，2..3 = 10、3 */
    for (int i = 0; i < 4; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    /* sub(a, b) = a - b */
    woort_IRFunction* f_sub = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 2, 0, &f_sub));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_sub, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_sub, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_sub);
        CHECK(&h, woort_IR_SUBI(f_sub, r, a, b));
        CHECK(&h, woort_IR_ret(f_sub, r));
    }

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_main));
    {
        woort_IRWindow* args = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, args != NULL);

        /* 槽 0 = 实参 0，槽 1 = 实参 1（自然序，没有 V1 的逆序压栈） */
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 0), CONST(f_main, 2)));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 1), CONST(f_main, 3)));

        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, args, r));
        CHECK(&h, woort_IR_ret(f_main, r));

        CHECK(&h, SLOT(args, 2) == NULL);                          /* 越界 */
        CHECK(&h, woort_IRWindow_slot(NULL, 0) == NULL);
        CHECK(&h, woort_IRFunction_new_window(NULL, 1) == NULL);
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, NULL, r) == false); /* 窗口不可为 NULL */
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, args, r) == false); /* 窗口只能消费一次 */
    }

    h_int(&h, 2, 10);
    h_int(&h, 3, 3);
    h_function(&h, 1, f_sub);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 7);
    }

    /* 入口预留：一个 vreg（r）加两个窗口位置 */
    dump_function(&h, f_main);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_mnemonic("CALLNWO") == 1);
    CHECK(&h, count_mnemonic("RESULT") == 1);
    CHECK(&h, count_lines_with("POP 0") == 1);
    CHECK(&h, entry_reserve(&h, f_main) == 3);

    TEST_END();
}

/* ========== 2. CALLNFP2：native 函数 ========== */

static void test_call_window_native(void)
{
    TEST_BEGIN("call_window_native (CALLNFP2)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口闭包，1 = sub2，2 = zero，3..4 = 10、3 */
    for (int i = 0; i < 5; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_main));
    {
        woort_IRWindow* args = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 0), CONST(f_main, 3)));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 1), CONST(f_main, 4)));
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNFP2(f_main, 1, args, r1));

        /* 零槽窗口：无参调用 */
        woort_IRWindow* none = woort_IRFunction_new_window(f_main, 0);
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNFP2(f_main, 2, none, r2));

        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_ADDI(f_main, r, r1, r2));
        CHECK(&h, woort_IR_ret(f_main, r));
    }

    h_int(&h, 3, 10);
    h_int(&h, 4, 3);
    h_native(&h, 1, native_sub2);
    h_native(&h, 2, native_zero);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 49); /* (10 - 3) + 42 */
    }

    dump_function(&h, f_main);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_mnemonic("CALLNFP") == 2);
    CHECK(&h, count_mnemonic("RESULT") == 2);
    CHECK(&h, count_lines_with("POP 0") == 2);

    TEST_END();
}

/* ========== 3. CALL2：经闭包间接调用；闭包本身由 MKCLOSURE2 创建 ========== */

static void test_call_window_indirect(void)
{
    TEST_BEGIN("call_window_indirect (CALL2 / MKCLOSURE2)");
    _Harness h;
    h_init(&h);

    /*
     * 常量：0 = 入口闭包，1 = sub（无捕获，可直接当闭包调用），2..3 = 10、3，
     *       4 = 带一个捕获的模板，5 = 捕获值 5，6 = 100
     */
    for (int i = 0; i < 7; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    /* sub(a, b) = a - b */
    woort_IRFunction* f_sub = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 2, 0, &f_sub));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_sub, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_sub, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_sub);
        CHECK(&h, woort_IR_SUBI(f_sub, r, a, b));
        CHECK(&h, woort_IR_ret(f_sub, r));
    }

    /* subc(a, b, ^c) = a - b + c * 100 */
    woort_IRFunction* f_subc = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 2, 1, &f_subc));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_subc, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_subc, 1);
        const woort_IRValue* c = woort_IRFunction_get_captured(f_subc, 0);
        woort_IRValue* t = woort_IRFunction_new_vreg(f_subc);
        woort_IRValue* u = woort_IRFunction_new_vreg(f_subc);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_subc);
        CHECK(&h, woort_IR_SUBI(f_subc, t, a, b));
        CHECK(&h, woort_IR_MULI(f_subc, u, c, CONST(f_subc, 6)));
        CHECK(&h, woort_IR_ADDI(f_subc, r, t, u));
        CHECK(&h, woort_IR_ret(f_subc, r));
    }

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_main));
    {
        /* 闭包是只在这里用一次的常量：发 CALLC */
        woort_IRWindow* args = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 0), CONST(f_main, 2)));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 1), CONST(f_main, 3)));
        woort_IRValue* r1 = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALL2(f_main, CONST(f_main, 1), args, r1));

        /* 捕获窗口的槽 i 就是模板里的 captured(i) */
        woort_IRWindow* cap = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(cap, 0), CONST(f_main, 5)));
        woort_IRValue* clo = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_MKCLOSURE2(f_main, clo, cap, 4));

        /* 闭包在 vreg 里：发 CALLS */
        woort_IRWindow* args2 = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args2, 0), CONST(f_main, 2)));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args2, 1), CONST(f_main, 3)));
        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALL2(f_main, clo, args2, r2));

        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_ADDI(f_main, r, r1, r2));
        CHECK(&h, woort_IR_ret(f_main, r));
    }

    h_int(&h, 2, 10);
    h_int(&h, 3, 3);
    h_int(&h, 5, 5);
    h_int(&h, 6, 100);
    h_closure(&h, 1, f_sub);
    h_closure(&h, 4, f_subc);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 514); /* 7 + (7 + 500) */
    }

    dump_function(&h, f_main);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_mnemonic("MKCLOSURENP") == 1);
    CHECK(&h, count_mnemonic("CALLC") == 1);
    CHECK(&h, count_mnemonic("CALLS") == 1);
    CHECK(&h, count_lines_with("POP 0") == 2);

    TEST_END();
}

/* ========== 4. 丢弃返回值：不发 RESULT，也不发 POPR ========== */

static void test_call_window_void(void)
{
    TEST_BEGIN("call_window_void (dst == NULL)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口闭包，1 = sub，2..3 = 10、3，4 = 99 */
    for (int i = 0; i < 5; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_sub = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 2, 0, &f_sub));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_sub, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_sub, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_sub);
        CHECK(&h, woort_IR_SUBI(f_sub, r, a, b));
        CHECK(&h, woort_IR_ret(f_sub, r));
    }

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_main));
    {
        woort_IRWindow* args = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 0), CONST(f_main, 2)));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 1), CONST(f_main, 3)));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, args, NULL));
        CHECK(&h, woort_IR_ret(f_main, CONST(f_main, 4)));
    }

    h_int(&h, 2, 10);
    h_int(&h, 3, 3);
    h_int(&h, 4, 99);
    h_function(&h, 1, f_sub);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 99);
    }

    dump_function(&h, f_main);
    CHECK(&h, count_mnemonic("RESULT") == 0);
    CHECK(&h, count_mnemonic("POPR") == 0);
    CHECK(&h, count_push_checks() == 0);

    TEST_END();
}

/* ========== 5. 直接写槽：运算结果、静态区加载、常量都直接进槽位 ========== */

static void test_direct_writes(void)
{
    TEST_BEGIN("direct_writes (ADDI / LOAD / const into slots)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口闭包，1 = sum3，2..3 = 1、2，4 = 20，5 = 300 */
    for (int i = 0; i < 6; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    /* sum3(a, b, c) = a + b + c */
    woort_IRFunction* f_sum3 = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 3, 0, &f_sum3));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_sum3, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_sum3, 1);
        const woort_IRValue* c = woort_IRFunction_get_argument(f_sum3, 2);
        woort_IRValue* t = woort_IRFunction_new_vreg(f_sum3);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_sum3);
        CHECK(&h, woort_IR_ADDI(f_sum3, t, a, b));
        CHECK(&h, woort_IR_ADDI(f_sum3, r, t, c));
        CHECK(&h, woort_IR_ret(f_sum3, r));
    }

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_main));
    {
        woort_IRValue* x = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* y = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_MOV(f_main, x, CONST(f_main, 2)));
        CHECK(&h, woort_IR_MOV(f_main, y, CONST(f_main, 3)));

        const woort_IRStaticIndex s = woort_IRCompiler_add_static(h.irc);
        CHECK(&h, woort_IR_STORE(f_main, s, CONST(f_main, 4)));

        woort_IRWindow* args = woort_IRFunction_new_window(f_main, 3);
        CHECK(&h, woort_IR_ADDI(f_main, SLOT(args, 0), x, y)); /* 1 + 2 */
        CHECK(&h, woort_IR_LOAD(f_main, SLOT(args, 1), s));    /* 20 */
        CHECK(&h, woort_IR_MOV(f_main, SLOT(args, 2), CONST(f_main, 5)));

        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, args, r));
        CHECK(&h, woort_IR_ret(f_main, r));
    }

    h_int(&h, 2, 1);
    h_int(&h, 3, 2);
    h_int(&h, 4, 20);
    h_int(&h, 5, 300);
    h_function(&h, 1, f_sum3);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 323); /* 3 + 20 + 300 */
    }

    /* 常量直连的 MOV 发 LOAD；除此之外没有任何搬运 */
    dump_function(&h, f_main);
    CHECK(&h, count_mnemonic("MOV") == 0);
    CHECK(&h, count_mnemonic("ADDI") == 1);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, count_mnemonic("SPSET") == 0);

    TEST_END();
}

/* ========== 6. 装箱与结构体字段直接写槽 ========== */

/* 第 6 组用例的常量下标 */
enum
{
    C_E_BOX, C_E_I, C_E_R, C_E_B, C_E_S, C_E_BIG, C_E_V1, /* 入口闭包 0..6 */
    C_ID1,                                                /* 7 */
    C_SEVEN,                                              /* 8 */
    C_MAKER,                                              /* 9 */
    C_F5, C_F2_5, C_F1,                                   /* 10..12：结构体字段 5、2.5、1 */
    C_BIG,                                                /* 13：放不进内联装箱的整数 */
    C_ZERO,                                               /* 14 */
    C_BOXED_COUNT

};

/* 单槽窗口里写什么 */
typedef enum _SlotWriter
{
    WRITE_BOXDYN,     /* 常量装箱（BOXDYN） */
    WRITE_LDIDSTBOXI, /* 结构体字段 0 装箱为整数（LDIDSTBOXI） */
    WRITE_LDIDSTBOXR, /* 结构体字段 1 装箱为实数（LDIDSTBOXR） */
    WRITE_LDIDSTBOXB, /* 结构体字段 2 装箱为布尔（LDIDSTBOXB） */
    WRITE_LDIDSTRUCT, /* 结构体字段 0，不装箱（LDIDSTRUCT） */
    WRITE_BIG,        /* 放不进内联装箱的大整数（LDIDSTBOXI） */
    WRITE_VREG,       /* 不用窗口：LDIDSTBOXI 的结果写进 vreg */

} _SlotWriter;

/*
 * 造一个函数：把一个值写进单槽窗口，再调用 id1 原样返回它。
 * WRITE_VREG 的函数不使用窗口：经 V1 形式的 CALLNWO 取得结构体，装箱后写进 vreg。
 */
static void build_slot_case(_Harness* h, woort_IRFunction* f, _SlotWriter writer)
{
    if (writer == WRITE_VREG)
    {
        woort_IRValue* st = woort_IRFunction_new_vreg(f);
        woort_IRValue* t = woort_IRFunction_new_vreg(f);
        CHECK(h, woort_IR_CALLNWO(f, C_MAKER, 0, st));
        CHECK(h, woort_IR_LDIDSTBOXI(f, t, st, 0));
        CHECK(h, woort_IR_ret(f, t));
        return;
    }

    woort_IRValue* r = woort_IRFunction_new_vreg(f);

    woort_IRValue* st = NULL;
    if (writer != WRITE_BOXDYN)
    {
        st = woort_IRFunction_new_vreg(f);
        if (writer == WRITE_BIG)
            build_struct3(f, st, C_BIG, C_ZERO, C_ZERO);
        else
            build_struct3(f, st, C_F5, C_F2_5, C_F1);
    }

    woort_IRWindow* w = woort_IRFunction_new_window(f, 1);
    switch (writer)
    {
    case WRITE_BOXDYN:
        CHECK(h, woort_IR_BOXDYN(f, SLOT(w, 0), WOORT_BOX_VALUE_TYPE_INT, CONST(f, C_SEVEN)));
        break;
    case WRITE_LDIDSTBOXI:
    case WRITE_BIG:
        CHECK(h, woort_IR_LDIDSTBOXI(f, SLOT(w, 0), st, 0));
        break;
    case WRITE_LDIDSTBOXR:
        CHECK(h, woort_IR_LDIDSTBOXR(f, SLOT(w, 0), st, 1));
        break;
    case WRITE_LDIDSTBOXB:
        CHECK(h, woort_IR_LDIDSTBOXB(f, SLOT(w, 0), st, 2));
        break;
    default: /* WRITE_LDIDSTRUCT */
        CHECK(h, woort_IR_LDIDSTRUCT(f, SLOT(w, 0), st, 0));
        break;
    }

    CHECK(h, woort_IR_CALLNWO2(f, C_ID1, w, r));
    CHECK(h, woort_IR_ret(f, r));
}

static void test_boxed_writes(void)
{
    TEST_BEGIN("boxed_writes (BOXDYN / LDIDSTBOX* / LDIDSTRUCT)");
    _Harness h;
    h_init(&h);

    for (int i = 0; i < C_BOXED_COUNT; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    /* id(p) = p */
    woort_IRFunction* f_id1 = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_id1));
    CHECK(&h, woort_IR_ret(f_id1, woort_IRFunction_get_argument(f_id1, 0)));

    /* maker() = 结构体 (5, 2.5, 1) */
    woort_IRFunction* f_maker = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_maker));
    {
        woort_IRValue* st = woort_IRFunction_new_vreg(f_maker);
        build_struct3(f_maker, st, C_F5, C_F2_5, C_F1);
        CHECK(&h, woort_IR_ret(f_maker, st));
    }

    static const _SlotWriter writers[] = {
        WRITE_BOXDYN, WRITE_LDIDSTBOXI, WRITE_LDIDSTBOXR, WRITE_LDIDSTBOXB,
        WRITE_LDIDSTRUCT, WRITE_BIG, WRITE_VREG,
    };
    enum { CASE_COUNT = (int)(sizeof(writers) / sizeof(writers[0])) };

    woort_IRFunction* mains[CASE_COUNT];
    for (int k = 0; k < CASE_COUNT; ++k)
    {
        mains[k] = NULL;
        CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &mains[k]));
        if (mains[k] != NULL)
            build_slot_case(&h, mains[k], writers[k]);
    }

    h_function(&h, C_ID1, f_id1);
    h_function(&h, C_MAKER, f_maker);
    for (int k = 0; k < CASE_COUNT; ++k)
        h_closure(&h, (woort_IRConstantIndex)k, mains[k]);
    h_int(&h, C_SEVEN, 7);
    h_int(&h, C_F5, 5);
    h_real(&h, C_F2_5, 2.5);
    h_int(&h, C_F1, 1);
    h_int(&h, C_BIG, (woort_Int)1 << 62);
    h_int(&h, C_ZERO, 0);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        woort_Int iv = 0;
        woort_Real rv = 0.0;

        h_call(&h, C_E_BOX, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, h_unbox(&h, &iv, NULL) == WOORT_BOX_VALUE_TYPE_INT);
        CHECK(&h, iv == 7);

        h_call(&h, C_E_I, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, h_unbox(&h, &iv, NULL) == WOORT_BOX_VALUE_TYPE_INT);
        CHECK(&h, iv == 5);

        h_call(&h, C_E_R, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, h_unbox(&h, NULL, &rv) == WOORT_BOX_VALUE_TYPE_REAL);
        CHECK(&h, rv == 2.5);

        h_call(&h, C_E_B, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_unbox_bool(h.sv + 1));

        h_call(&h, C_E_S, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 5);

        h_call(&h, C_E_BIG, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, h_unbox(&h, &iv, NULL) == WOORT_BOX_VALUE_TYPE_INT);
        CHECK(&h, iv == ((woort_Int)1 << 62));

        h_call(&h, C_E_V1, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, h_unbox(&h, &iv, NULL) == WOORT_BOX_VALUE_TYPE_INT);
        CHECK(&h, iv == 5);
    }

    /* 装箱读取各发一条指令，没有经过 vreg 中转的搬运 */
    dump_function(&h, mains[1]);
    CHECK(&h, count_mnemonic("LDIDSTBOXI") == 1);
    CHECK(&h, count_mnemonic("MOV") == 0);
    CHECK(&h, count_push_checks() == 0);
    dump_function(&h, mains[0]);
    CHECK(&h, count_mnemonic("BOXDYN") == 1);
    CHECK(&h, count_mnemonic("MOV") == 0);

    /* 不用窗口的函数同样受益，入口预留里没有操作数区 */
    dump_function(&h, mains[6]);
    CHECK(&h, count_mnemonic("LDIDSTBOXI") == 1);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_push_checks() == 0);

    TEST_END();
}

/* ========== 7. MKVEC2：元素顺序、空窗口、超过 255 个元素时回退为压栈 ========== */

static void test_mkvec2(void)
{
    TEST_BEGIN("mkvec2 (order / empty / >255 push fallback)");
    _Harness h;
    h_init(&h);

    enum { E_V0, E_V1, E_V3, E_V300, C_BASE };
    static const uint32_t counts[] = { 0, 1, 3, 300 };
    enum { CASE_COUNT = (int)(sizeof(counts) / sizeof(counts[0])) };

    for (int i = 0; i < C_BASE + 300; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* mains[CASE_COUNT];
    for (int k = 0; k < CASE_COUNT; ++k)
    {
        mains[k] = NULL;
        CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &mains[k]));
        if (mains[k] == NULL)
            continue;

        woort_IRFunction* const f = mains[k];
        woort_IRValue* v = woort_IRFunction_new_vreg(f);
        woort_IRWindow* w = woort_IRFunction_new_window(f, counts[k]);

        /* 槽 i 就是第 i 个元素 */
        for (uint32_t i = 0; i < counts[k]; ++i)
        {
            (void)woort_IR_MOV(f, SLOT(w, i), CONST(f, C_BASE + i));
            h_int(&h, C_BASE + i, (woort_Int)(1000 + i));
        }
        (void)woort_IR_MKVEC2(f, v, w);
        (void)woort_IR_ret(f, v);

        h_closure(&h, (woort_IRConstantIndex)k, f);
    }

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        for (int k = 0; k < CASE_COUNT; ++k)
        {
            h_call(&h, (woort_IRConstantIndex)k, 0);
            CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
            CHECK(&h, woort_vec_len(h.sv + 1) == counts[k]);

            for (uint32_t i = 0; i < counts[k]; ++i)
            {
                woort_StackValue elem;
                (void)woort_push_reserve(1, &elem);
                (void)woort_vec_get(elem, h.sv + 1, i);
                if (woort_int(elem) != (woort_Int)(1000 + i))
                {
                    (void)printf(
                        "    FAIL %s:%d: case %d, vec[%u]: expected %d, got %lld\n",
                        __FILE__, __LINE__, k, i, 1000 + (int)i,
                        (long long)woort_int(elem));
                    ++h.failures;
                    woort_pop(1);
                    break;
                }
                woort_pop(1);
            }
        }
    }

    dump_function(&h, mains[2]);
    CHECK(&h, count_mnemonic("MKVECNP") == 1);
    CHECK(&h, count_mnemonic("MKVEC") == 0);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, count_mnemonic("SPSET") == 0);

    dump_function(&h, mains[0]);
    CHECK(&h, count_mnemonic("MKVECNP") == 1);

    /* 300 个元素超出 NP 变体的 8 位计数：先压栈，再用会弹栈的 MKVECEXT */
    dump_function(&h, mains[3]);
    CHECK(&h, count_mnemonic("MKVECNP") == 0);
    CHECK(&h, count_mnemonic("MKVEC") == 1);
    CHECK(&h, count_push_checks() == 300);
    CHECK(&h, count_mnemonic("SPSET") == 0);

    TEST_END();
}

/* ========== 8. MKMAP2：槽 2p 为键、槽 2p+1 为值 ========== */

static void test_mkmap2(void)
{
    TEST_BEGIN("mkmap2 (key/value slot order, odd count rejected)");
    _Harness h;
    h_init(&h);

    enum { E_MAIN, C_PAIR0 }; /* 入口 0；第 p 对：键 1 + 2p，值 2 + 2p */
    const uint32_t pairs = 3;

    for (int i = 0; i < 1 + 2 * (int)pairs; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_main));
    {
        woort_IRValue* m = woort_IRFunction_new_vreg(f_main);
        woort_IRWindow* w = woort_IRFunction_new_window(f_main, pairs * 2);
        for (uint32_t p = 0; p < pairs; ++p)
        {
            (void)woort_IR_MOV(f_main, SLOT(w, p * 2), CONST(f_main, C_PAIR0 + p * 2));
            (void)woort_IR_MOV(f_main, SLOT(w, p * 2 + 1), CONST(f_main, C_PAIR0 + p * 2 + 1));
            h_box_int(&h, C_PAIR0 + p * 2, (woort_Int)(100 + p * 10));
            h_box_int(&h, C_PAIR0 + p * 2 + 1, (woort_Int)(2000 + p * 100));
        }
        (void)woort_IR_MKMAP2(f_main, m, w);
        (void)woort_IR_ret(f_main, m);
    }
    h_closure(&h, E_MAIN, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, E_MAIN, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_map_len(h.sv + 1) == pairs);

        for (uint32_t p = 0; p < pairs; ++p)
        {
            woort_StackValue elem;
            (void)woort_push_reserve(1, &elem);
            CHECK(&h, woort_map_get_by_int(elem, h.sv + 1, (woort_Int)(100 + p * 10)));
            CHECK(&h, woort_unbox_int(elem) == (woort_Int)(2000 + p * 100));
            woort_pop(1);
        }
    }

    dump_function(&h, f_main);
    CHECK(&h, count_mnemonic("MKMAPNP") == 1);
    CHECK(&h, count_mnemonic("MKMAP") == 0);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, count_mnemonic("SPSET") == 0);

    /* 槽位数必须是偶数：发射函数直接返回 false，不留指令 */
    {
        _Harness odd;
        h_init(&odd);

        woort_IRFunction* f = NULL;
        CHECK(&odd, woort_IRCompiler_add_function(odd.irc, 0, 0, &f));
        woort_IRValue* v = woort_IRFunction_new_vreg(f);
        woort_IRWindow* w = woort_IRFunction_new_window(f, 3);
        CHECK(&odd, woort_IR_MKMAP2(f, v, w) == false);
        CHECK(&odd, f->m_instructions.m_size == 0);

        h.failures += odd.failures;
        h_close(&odd);
    }

    TEST_END();
}

/* ========== 9. MKSTRUCT2 的字段顺序、MKCLOSURE2 的捕获顺序 ========== */

static void test_construct_order(void)
{
    TEST_BEGIN("construct_order (MKSTRUCT2 fields / MKCLOSURE2 captures)");
    _Harness h;
    h_init(&h);

    enum
    {
        E_STRUCT, E_CLOS, C_TMPL,      /* 0..2 */
        C_11, C_22, C_33,              /* 3..5 */
        C_1, C_2, C_3,                 /* 6..8 */
        C_100, C_10,                   /* 9..10 */

    };
    for (int i = 0; i <= C_10; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    /* 模板：三个捕获，返回 c0 * 100 + c1 * 10 + c2 */
    woort_IRFunction* f_tmpl = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 3, &f_tmpl));
    {
        const woort_IRValue* c0 = woort_IRFunction_get_captured(f_tmpl, 0);
        const woort_IRValue* c1 = woort_IRFunction_get_captured(f_tmpl, 1);
        const woort_IRValue* c2 = woort_IRFunction_get_captured(f_tmpl, 2);
        woort_IRValue* t0 = woort_IRFunction_new_vreg(f_tmpl);
        woort_IRValue* t1 = woort_IRFunction_new_vreg(f_tmpl);
        woort_IRValue* t2 = woort_IRFunction_new_vreg(f_tmpl);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_tmpl);
        CHECK(&h, woort_IR_MULI(f_tmpl, t0, c0, CONST(f_tmpl, C_100)));
        CHECK(&h, woort_IR_MULI(f_tmpl, t1, c1, CONST(f_tmpl, C_10)));
        CHECK(&h, woort_IR_ADDI(f_tmpl, t2, t0, t1));
        CHECK(&h, woort_IR_ADDI(f_tmpl, r, t2, c2));
        CHECK(&h, woort_IR_ret(f_tmpl, r));
    }

    /* 返回结构体 (11, 22, 33) */
    woort_IRFunction* f_struct = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_struct));
    {
        woort_IRValue* st = woort_IRFunction_new_vreg(f_struct);
        build_struct3(f_struct, st, C_11, C_22, C_33);
        CHECK(&h, woort_IR_ret(f_struct, st));
    }

    /* 捕获 (1, 2, 3) 的闭包，用零槽窗口调用它 */
    woort_IRFunction* f_clos = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_clos));
    {
        /* 捕获 (1, 2, 3) 的闭包，用零槽窗口调用它 */
        woort_IRWindow* cap = woort_IRFunction_new_window(f_clos, 3);
        (void)woort_IR_MOV(f_clos, SLOT(cap, 0), CONST(f_clos, C_1));
        (void)woort_IR_MOV(f_clos, SLOT(cap, 1), CONST(f_clos, C_2));
        (void)woort_IR_MOV(f_clos, SLOT(cap, 2), CONST(f_clos, C_3));
        woort_IRValue* c = woort_IRFunction_new_vreg(f_clos);
        (void)woort_IR_MKCLOSURE2(f_clos, c, cap, C_TMPL);

        woort_IRWindow* none = woort_IRFunction_new_window(f_clos, 0);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_clos);
        (void)woort_IR_CALL2(f_clos, c, none, r);
        (void)woort_IR_ret(f_clos, r);
    }

    h_closure(&h, E_STRUCT, f_struct);
    h_closure(&h, E_CLOS, f_clos);
    h_closure(&h, C_TMPL, f_tmpl);
    h_int(&h, C_11, 11);
    h_int(&h, C_22, 22);
    h_int(&h, C_33, 33);
    h_int(&h, C_1, 1);
    h_int(&h, C_2, 2);
    h_int(&h, C_3, 3);
    h_int(&h, C_100, 100);
    h_int(&h, C_10, 10);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, E_STRUCT, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_struct_len(h.sv + 1) == 3);
        CHECK(&h, woort_struct_get_int(h.sv + 1, 0) == 11);
        CHECK(&h, woort_struct_get_int(h.sv + 1, 1) == 22);
        CHECK(&h, woort_struct_get_int(h.sv + 1, 2) == 33);

        h_call(&h, E_CLOS, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 123); /* 1 * 100 + 2 * 10 + 3 */
    }

    dump_function(&h, f_clos);
    CHECK(&h, count_mnemonic("MKCLOSURENP") == 1);
    CHECK(&h, count_mnemonic("MKCLOSURE") == 0);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, count_mnemonic("SPSET") == 0);

    dump_function(&h, f_struct);
    CHECK(&h, count_mnemonic("MKSTRUCTNP") == 1);
    CHECK(&h, count_mnemonic("MKSTRUCT") == 0);
    CHECK(&h, count_push_checks() == 0);

    TEST_END();
}

/* ========== 10. 嵌套窗口 ========== */

woort_api native_veclen_plus1(void)
{
    return woort_ret_int((woort_Int)woort_vec_len(0) + woort_int(1));
}

/*
 * 四个入口函数，参数 x：
 *   reverse  f(h(x), g(x))     最后一个实参先求值（前端的顺序）：外层槽位从高位往下填，
 *                              嵌套窗口放得进低位空位，sp 全程不动
 *   chain    g(g(g(x)))        每次调用的结果就是下一个窗口的唯一元素
 *   forward  f(g(x), h(x))     第一个实参先求值：位置 0 已写，第二个嵌套窗口只能落到
 *                              入口位置以下，各窗口不在同一位置，需要校正 sp
 *   mk       native([7], g(x)) 构造指令把向量直接写进外层槽位：它先写目标再读元素，
 *                              元素窗口要让开目标槽位
 * 其中 f(a, b) = a * 10 + b，g(x) = x + 1，h(x) = x * 2。
 */
static void test_nested_windows(void)
{
    TEST_BEGIN("nested_windows (reverse / chain / forward / mk)");
    _Harness h;
    h_init(&h);

    enum
    {
        E_REVERSE, E_CHAIN, E_FORWARD, E_MK, /* 入口 0..3 */
        C_F, C_G, C_H, C_NATIVE,             /* 4..7 */
        C_10, C_1, C_2, C_7,                 /* 8..11 */

    };
    for (int i = 0; i <= C_7; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_f = make_lin2(h.irc, C_10, C_1);
    woort_IRFunction* f_g = make_addconst(h.irc, C_1);
    CHECK(&h, f_f != NULL && f_g != NULL);

    woort_IRFunction* f_h = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_h));
    {
        const woort_IRValue* x = woort_IRFunction_get_argument(f_h, 0);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_h);
        CHECK(&h, woort_IR_MULI(f_h, r, x, CONST(f_h, C_2)));
        CHECK(&h, woort_IR_ret(f_h, r));
    }

    woort_IRFunction* mains[4] = { NULL, NULL, NULL, NULL };
    for (int k = 0; k < 4; ++k)
        CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &mains[k]));
    if (mains[0] == NULL || mains[1] == NULL || mains[2] == NULL || mains[3] == NULL)
    {
        (void)printf("FAIL (add_function failed)\n");
        h_close(&h);
        return;
    }

    /* reverse：槽 1（第二个实参）先写 */
    {
        woort_IRFunction* const f = mains[0];
        const woort_IRValue* const x = woort_IRFunction_get_argument(f, 0);

        woort_IRWindow* outer = woort_IRFunction_new_window(f, 2);
        woort_IRWindow* g = woort_IRFunction_new_window(f, 1);
        (void)woort_IR_MOV(f, SLOT(g, 0), x);
        (void)woort_IR_CALLNWO2(f, C_G, g, SLOT(outer, 1));

        woort_IRWindow* hh = woort_IRFunction_new_window(f, 1);
        (void)woort_IR_MOV(f, SLOT(hh, 0), x);
        (void)woort_IR_CALLNWO2(f, C_H, hh, SLOT(outer, 0));

        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        (void)woort_IR_CALLNWO2(f, C_F, outer, r);
        (void)woort_IR_ret(f, r);
    }

    /* chain：最内层的窗口最后创建 */
    {
        woort_IRFunction* const f = mains[1];
        const woort_IRValue* const x = woort_IRFunction_get_argument(f, 0);

        woort_IRWindow* outer = woort_IRFunction_new_window(f, 1);
        woort_IRWindow* middle = woort_IRFunction_new_window(f, 1);
        woort_IRWindow* inner = woort_IRFunction_new_window(f, 1);
        (void)woort_IR_MOV(f, SLOT(inner, 0), x);
        (void)woort_IR_CALLNWO2(f, C_G, inner, SLOT(middle, 0));
        (void)woort_IR_CALLNWO2(f, C_G, middle, SLOT(outer, 0));

        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        (void)woort_IR_CALLNWO2(f, C_G, outer, r);
        (void)woort_IR_ret(f, r);
    }

    /* forward：槽 0（第一个实参）先写 */
    {
        woort_IRFunction* const f = mains[2];
        const woort_IRValue* const x = woort_IRFunction_get_argument(f, 0);

        woort_IRWindow* outer = woort_IRFunction_new_window(f, 2);
        woort_IRWindow* g = woort_IRFunction_new_window(f, 1);
        (void)woort_IR_MOV(f, SLOT(g, 0), x);
        (void)woort_IR_CALLNWO2(f, C_G, g, SLOT(outer, 0));

        woort_IRWindow* hh = woort_IRFunction_new_window(f, 1);
        (void)woort_IR_MOV(f, SLOT(hh, 0), x);
        (void)woort_IR_CALLNWO2(f, C_H, hh, SLOT(outer, 1));

        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        (void)woort_IR_CALLNWO2(f, C_F, outer, r);
        (void)woort_IR_ret(f, r);
    }

    /* mk：MKVEC2 的目标是外层窗口的槽 0 */
    {
        woort_IRFunction* const f = mains[3];
        const woort_IRValue* const x = woort_IRFunction_get_argument(f, 0);

        woort_IRWindow* outer = woort_IRFunction_new_window(f, 2);

        woort_IRWindow* elem = woort_IRFunction_new_window(f, 1);
        (void)woort_IR_MOV(f, SLOT(elem, 0), CONST(f, C_7));
        (void)woort_IR_MKVEC2(f, SLOT(outer, 0), elem);

        woort_IRWindow* g = woort_IRFunction_new_window(f, 1);
        (void)woort_IR_MOV(f, SLOT(g, 0), x);
        (void)woort_IR_CALLNWO2(f, C_G, g, SLOT(outer, 1));

        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        (void)woort_IR_CALLNFP2(f, C_NATIVE, outer, r);
        (void)woort_IR_ret(f, r);
    }

    h_int(&h, C_10, 10);
    h_int(&h, C_1, 1);
    h_int(&h, C_2, 2);
    h_int(&h, C_7, 7);
    h_function(&h, C_F, f_f);
    h_function(&h, C_G, f_g);
    h_function(&h, C_H, f_h);
    h_native(&h, C_NATIVE, native_veclen_plus1);
    for (int k = 0; k < 4; ++k)
        h_closure(&h, (woort_IRConstantIndex)k, mains[k]);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, E_REVERSE, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 106); /* 10 * 10 + 6 */

        h_call(&h, E_CHAIN, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 8); /* 5 + 1 + 1 + 1 */

        h_call(&h, E_FORWARD, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 70); /* 6 * 10 + 10 */

        h_call(&h, E_MK, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 7); /* 向量长度 1，加上 5 + 1 */
    }

    for (int k = 0; k < 4; ++k)
    {
        dump_function(&h, mains[k]);
        CHECK(&h, count_push_checks() == 0);
    }

    /* 逆序与链式：所有窗口都在位置 0，sp 不动；预留 = 一个 vreg + 窗口 */
    dump_function(&h, mains[0]);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, entry_reserve(&h, mains[0]) == 3);
    dump_function(&h, mains[1]);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, entry_reserve(&h, mains[1]) == 2);

    /* 正序：第二个嵌套窗口在入口位置以下，三个窗口位置不一，入口到位置 1、回到位置 0、
       再到位置 1 */
    dump_function(&h, mains[2]);
    CHECK(&h, count_mnemonic("SPSET") == 3);
    CHECK(&h, entry_reserve(&h, mains[2]) == 4);

    /* mk：元素窗口让开目标槽位，入口位置随之加深；只有外层调用需要校正 sp */
    dump_function(&h, mains[3]);
    CHECK(&h, count_mnemonic("MKVECNP") == 1);
    CHECK(&h, count_mnemonic("SPSET") == 1);
    CHECK(&h, entry_reserve(&h, mains[3]) == 4);

    TEST_END();
}

/* ========== 11. 实参里的三元：窗口在两个分支里各自被消费 ========== */

static void test_window_ternary(void)
{
    TEST_BEGIN("window_ternary (window consumed inside each branch)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口，1 = f，2 = g，3 = h，4..6 = 10、1、2，7 = 3 */
    for (int i = 0; i < 8; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_f = make_lin2(h.irc, 4, 5);
    woort_IRFunction* f_g = make_addconst(h.irc, 5);
    CHECK(&h, f_f != NULL && f_g != NULL);

    woort_IRFunction* f_h = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_h));
    {
        const woort_IRValue* x = woort_IRFunction_get_argument(f_h, 0);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_h);
        CHECK(&h, woort_IR_MULI(f_h, r, x, CONST(f_h, 6)));
        CHECK(&h, woort_IR_ret(f_h, r));
    }

    /* f(x > 3 ? g(x) : h(x), x)：x > 3 时跳到 l_else，那里算的是 g(x) */
    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_main));
    {
        const woort_IRValue* const x = woort_IRFunction_get_argument(f_main, 0);
        woort_IRLabel* const l_else = woort_IRFunction_new_label(f_main);
        woort_IRLabel* const l_end = woort_IRFunction_new_label(f_main);

        woort_IRWindow* outer = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(outer, 1), x));
        CHECK(&h, woort_IR_jcc_gt(f_main, x, CONST(f_main, 7), l_else));

        woort_IRWindow* hh = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(hh, 0), x));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 3, hh, SLOT(outer, 0)));
        CHECK(&h, woort_IR_jmp(f_main, l_end));

        CHECK(&h, woort_IR_bind(f_main, l_else));
        woort_IRWindow* g = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(g, 0), x));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 2, g, SLOT(outer, 0)));

        CHECK(&h, woort_IR_bind(f_main, l_end));
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, outer, r));
        CHECK(&h, woort_IR_ret(f_main, r));
    }

    h_int(&h, 4, 10);
    h_int(&h, 5, 1);
    h_int(&h, 6, 2);
    h_int(&h, 7, 3);
    h_function(&h, 1, f_f);
    h_function(&h, 2, f_g);
    h_function(&h, 3, f_h);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 65); /* g(5) * 10 + 5 */

        h_call(&h, 0, 1);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 21); /* h(1) * 10 + 1 */
    }

    dump_function(&h, f_main);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_push_checks() == 0);

    TEST_END();
}

/* ========== 12. 循环里的窗口：每轮重新打开、写入、消费 ========== */

static void test_window_loop(void)
{
    TEST_BEGIN("window_loop (window opened and consumed per iteration)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口，1 = inc，2..3 = 0、1 */
    for (int i = 0; i < 4; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_inc = make_addconst(h.irc, 3);
    CHECK(&h, f_inc != NULL);

    /*
     * main(n) {
     *     s = 0; i = 0;
     *     while (i < n) { s += inc(i); i += 1; }
     *     return s;
     * }
     */
    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_main));
    {
        const woort_IRValue* const n = woort_IRFunction_get_argument(f_main, 0);
        const woort_IRValue* const zero = CONST(f_main, 2);
        const woort_IRValue* const one = CONST(f_main, 3);
        woort_IRValue* s = woort_IRFunction_new_vreg(f_main);
        woort_IRValue* i = woort_IRFunction_new_vreg(f_main);
        woort_IRLabel* const l_loop = woort_IRFunction_new_label(f_main);
        woort_IRLabel* const l_end = woort_IRFunction_new_label(f_main);

        CHECK(&h, woort_IR_MOV(f_main, s, zero));
        CHECK(&h, woort_IR_MOV(f_main, i, zero));
        CHECK(&h, woort_IR_bind(f_main, l_loop));
        CHECK(&h, woort_IR_jcc_ge(f_main, i, n, l_end));

        woort_IRWindow* w = woort_IRFunction_new_window(f_main, 1);
        woort_IRValue* t = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(w, 0), i));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, w, t));
        CHECK(&h, woort_IR_ADDI(f_main, s, s, t));
        CHECK(&h, woort_IR_ADDI(f_main, i, i, one));
        CHECK(&h, woort_IR_jmp(f_main, l_loop));

        CHECK(&h, woort_IR_bind(f_main, l_end));
        CHECK(&h, woort_IR_ret(f_main, s));
    }

    h_int(&h, 2, 0);
    h_int(&h, 3, 1);
    h_function(&h, 1, f_inc);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 4);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 10); /* 1 + 2 + 3 + 4 */

        h_call(&h, 0, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 0);
    }

    dump_function(&h, f_main);
    CHECK(&h, count_mnemonic("CALLNWO") == 1);
    CHECK(&h, count_lines_with("POP 0") == 1);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_push_checks() == 0);

    TEST_END();
}

/* ========== 13. 与 V1 栈操作混用 ========== */

/*
 * main(x) {
 *     a = sub(x, 3);          // V1：PUSHCHK x2 + CALLNWO(argc)
 *     b = sub(a, 1);          // V2：窗口
 *     return native_double1(b); // V1：PUSHCHK + CALLNFP(argc)
 * }
 * 所有窗口都在位置 0，V1 栈区间开始时 sp 本来就在入口预留后的位置，不需要校正。
 */
static void test_mixed_v1_v2(void)
{
    TEST_BEGIN("mixed_v1_v2 (PUSHCHK/CALL(argc) beside windows)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口，1 = sub，2 = native，3..4 = 3、1 */
    for (int i = 0; i < 5; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_sub = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 2, 0, &f_sub));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_sub, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_sub, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_sub);
        CHECK(&h, woort_IR_SUBI(f_sub, r, a, b));
        CHECK(&h, woort_IR_ret(f_sub, r));
    }

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_main));
    {
        const woort_IRValue* const x = woort_IRFunction_get_argument(f_main, 0);

        /* V1：实参逆序压栈 */
        woort_IRValue* a = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_PUSHCHK(f_main, CONST(f_main, 3)));
        CHECK(&h, woort_IR_PUSHCHK(f_main, x));
        CHECK(&h, woort_IR_CALLNWO(f_main, 1, 2, a));

        /* V2：槽 0 是第一个实参 */
        woort_IRValue* b = woort_IRFunction_new_vreg(f_main);
        woort_IRWindow* w = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(w, 0), a));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(w, 1), CONST(f_main, 4)));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, w, b));

        /* V1：native */
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_PUSHCHK(f_main, b));
        CHECK(&h, woort_IR_CALLNFP(f_main, 2, 1, r));
        CHECK(&h, woort_IR_ret(f_main, r));
    }

    h_int(&h, 3, 3);
    h_int(&h, 4, 1);
    h_function(&h, 1, f_sub);
    h_native(&h, 2, native_double1);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 10);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 12); /* ((10 - 3) - 1) * 2 */
    }

    dump_function(&h, f_main);
    CHECK(&h, count_push_checks() == 3);
    CHECK(&h, count_mnemonic("CALLNWO") == 2);
    CHECK(&h, count_mnemonic("CALLNFP") == 1);
    CHECK(&h, count_lines_with("POP 0") == 1); /* 只有窗口调用不弹栈 */
    CHECK(&h, count_mnemonic("SPSET") == 0);

    TEST_END();
}

/*
 * sp 被窗口抬离入口位置之后出现 V1 栈区间：区间开始前必须把 sp 放回去。
 * main(x) {
 *     f(g(x), h(x));           // 正序求值：h 的窗口在位置 0，g 与 f 在位置 1
 *     return sub(x, 3);        // V1 栈区间
 * }
 */
static void test_mixed_v1_after_lift(void)
{
    TEST_BEGIN("mixed_v1_after_lift (V1 stack region after lifted sp)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口，1 = f，2 = g，3 = h，4 = sub，5..8 = 10、1、2、3 */
    for (int i = 0; i < 9; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_f = make_lin2(h.irc, 5, 6);
    woort_IRFunction* f_g = make_addconst(h.irc, 6);
    CHECK(&h, f_f != NULL && f_g != NULL);

    woort_IRFunction* f_h = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_h));
    {
        const woort_IRValue* x = woort_IRFunction_get_argument(f_h, 0);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_h);
        CHECK(&h, woort_IR_MULI(f_h, r, x, CONST(f_h, 7)));
        CHECK(&h, woort_IR_ret(f_h, r));
    }

    woort_IRFunction* f_sub = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 2, 0, &f_sub));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_sub, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_sub, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_sub);
        CHECK(&h, woort_IR_SUBI(f_sub, r, a, b));
        CHECK(&h, woort_IR_ret(f_sub, r));
    }

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_main));
    {
        const woort_IRValue* const x = woort_IRFunction_get_argument(f_main, 0);

        /* 与 test_nested_windows 的 forward 相同：入口到位置 1、回到位置 0、再到位置 1 */
        woort_IRWindow* outer = woort_IRFunction_new_window(f_main, 2);
        woort_IRWindow* g = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(g, 0), x));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 2, g, SLOT(outer, 0)));
        woort_IRWindow* hh = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(hh, 0), x));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 3, hh, SLOT(outer, 1)));

        woort_IRValue* t = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, outer, t));

        /* V1 栈区间：此时 sp 停在位置 1。实参逆序压栈，第 0 个实参最后压 */
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_PUSHCHK(f_main, CONST(f_main, 8)));
        CHECK(&h, woort_IR_PUSHCHK(f_main, x));
        CHECK(&h, woort_IR_CALLNWO(f_main, 4, 2, r));
        CHECK(&h, woort_IR_ADDI(f_main, r, r, t));
        CHECK(&h, woort_IR_ret(f_main, r));
    }

    h_int(&h, 5, 10);
    h_int(&h, 6, 1);
    h_int(&h, 7, 2);
    h_int(&h, 8, 3);
    h_function(&h, 1, f_f);
    h_function(&h, 2, f_g);
    h_function(&h, 3, f_h);
    h_function(&h, 4, f_sub);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 72); /* (6 * 10 + 10) + (5 - 3) */
    }

    /* 三个窗口校正三次，V1 栈区间开始前再放回入口位置 */
    dump_function(&h, f_main);
    CHECK(&h, count_mnemonic("SPSET") == 4);
    CHECK(&h, count_push_checks() == 2);

    TEST_END();
}

/* ========== 14. 内层窗口打开期间写外层槽位：整个函数回退为压栈降级 ========== */

static void test_outer_write_fallback(void)
{
    TEST_BEGIN("outer_write_fallback (whole function push-lowered)");
    _Harness h;
    h_init(&h);

    /* 常量：0 = 入口，1 = f，2 = g，3..4 = 10、1，5..6 = 7、9 */
    for (int i = 0; i < 7; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_f = make_lin2(h.irc, 3, 4);
    woort_IRFunction* f_g = make_addconst(h.irc, 4);
    CHECK(&h, f_f != NULL && f_g != NULL);

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 0, 0, &f_main));
    {
        woort_IRWindow* outer = woort_IRFunction_new_window(f_main, 2);

        /* 先打开内层窗口，再写外层槽位：违反「内层窗口打开期间只写它自己的槽位」 */
        woort_IRWindow* inner = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(outer, 1), CONST(f_main, 5)));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(inner, 0), CONST(f_main, 6)));
        CHECK(&h, woort_IR_CALLNWO2(f_main, 2, inner, SLOT(outer, 0)));

        /* 调用窗口压栈降级时实参逆序压栈：核对顺序 */
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNWO2(f_main, 1, outer, r));

        /* 构造窗口压栈降级时元素正序压栈 */
        woort_IRValue* v = woort_IRFunction_new_vreg(f_main);
        woort_IRWindow* elems = woort_IRFunction_new_window(f_main, 2);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(elems, 0), CONST(f_main, 5)));
        CHECK(&h, woort_IR_MOV(f_main, SLOT(elems, 1), CONST(f_main, 6)));
        CHECK(&h, woort_IR_MKVEC2(f_main, v, elems));

        woort_IRValue* r2 = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_ADDI(f_main, r2, r, CONST(f_main, 6)));
        CHECK(&h, woort_IR_ret(f_main, r2));
    }

    h_int(&h, 3, 10);
    h_int(&h, 4, 1);
    h_int(&h, 5, 7);
    h_int(&h, 6, 9);
    h_function(&h, 1, f_f);
    h_function(&h, 2, f_g);
    h_closure(&h, 0, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, 0, 0);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        /* f(10, 7) = 10 * 10 + 7 = 107，再加 9 */
        CHECK(&h, woort_int(h.sv + 1) == 116);
    }

    /* 三个窗口全部压栈降级：2 + 1 + 2 次带检查的压栈，没有 SPSET、没有 NP 指令 */
    dump_function(&h, f_main);
    CHECK(&h, count_push_checks() == 5);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_mnemonic("MKVECNP") == 0);
    CHECK(&h, count_mnemonic("MKVEC") == 1);
    CHECK(&h, count_lines_with("POP 0") == 0);

    TEST_END();
}

/* ========== 15. sp 抬起后往更低的槽位写值：先放下 sp ========== */

/*
 * GC 只扫描 sp 以上的槽位。内层窗口消费后 sp 停在该窗口的位置，这时再把值写进
 * 更低的外层槽位，必须先把 sp 放到那个槽位。
 *
 *   X = 窗口(1)，X.slot0 先写为 5（位置 0）
 *   A = 窗口(2)，在 X 之下（位置 -2、-1）
 *   W = 窗口(1)，在 X 之下（位置 -1），调用 g(x) 的结果写进 A.slot1；此后 sp 停在 -1
 *   再用 MOV 把 7 写进 A.slot0（位置 -2）：低于 sp，要先放下 sp
 * 最后 f(A) = f(7, x + 1)，h(X) = h(5)，返回两者之和。
 */
static void test_write_below_sp(void)
{
    TEST_BEGIN("write_below_sp (sp lowered before writing a lower slot)");
    _Harness h;
    h_init(&h);

    enum { E_MAIN, C_F, C_G, C_H, C_10, C_1, C_X5, C_A7 };
    for (int i = 0; i <= C_A7; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_f = make_lin2(h.irc, C_10, C_1);
    woort_IRFunction* f_g = make_addconst(h.irc, C_1);
    woort_IRFunction* f_h = make_addconst(h.irc, C_1);
    CHECK(&h, f_f != NULL && f_g != NULL && f_h != NULL);

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_main));
    {
        const woort_IRValue* const x = woort_IRFunction_get_argument(f_main, 0);

        woort_IRWindow* win_x = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(win_x, 0), CONST(f_main, C_X5)));

        woort_IRWindow* win_a = woort_IRFunction_new_window(f_main, 2);
        woort_IRWindow* win_w = woort_IRFunction_new_window(f_main, 1);
        CHECK(&h, woort_IR_MOV(f_main, SLOT(win_w, 0), x));
        CHECK(&h, woort_IR_CALLNWO2(f_main, C_G, win_w, SLOT(win_a, 1)));

        /* sp 停在位置 -1；A.slot0 在位置 -2 */
        CHECK(&h, woort_IR_MOV(f_main, SLOT(win_a, 0), CONST(f_main, C_A7)));

        woort_IRValue* ra = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNWO2(f_main, C_F, win_a, ra));

        woort_IRValue* rx = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_CALLNWO2(f_main, C_H, win_x, rx));

        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_ADDI(f_main, r, ra, rx));
        CHECK(&h, woort_IR_ret(f_main, r));
    }

    h_int(&h, C_10, 10);
    h_int(&h, C_1, 1);
    h_int(&h, C_X5, 5);
    h_int(&h, C_A7, 7);
    h_function(&h, C_F, f_f);
    h_function(&h, C_G, f_g);
    h_function(&h, C_H, f_h);
    h_closure(&h, E_MAIN, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, E_MAIN, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        /* f(7, 5 + 1) = 70 + 6 = 76，再加 h(5) = 6 */
        CHECK(&h, woort_int(h.sv + 1) == 82);
    }

    /*
     * 位置整体上移 2：X 在 2，A 在 0，W 在 1，窗口共占 3 个位置。sp 依次为：
     * 入口 0 → W 调用 1 → 写 A.slot0 时降到 0 → A 调用（已在 0）→ X 调用回到 2。
     * vreg：ra、rx 同时活跃，r 复用它们的槽，共 2 个。
     */
    dump_function(&h, f_main);
    CHECK(&h, count_mnemonic("SPSET") == 3);
    CHECK(&h, count_push_checks() == 0);
    CHECK(&h, entry_reserve(&h, f_main) == 5);

    TEST_END();
}

/* ========== 16. 结构错误：woort_IRCompiler_finish 返回 false ========== */

/*
 * 每个小用例在自己的编译器里构造一个函数，登记两个整数常量；finish 必须失败。
 * kind 选择要构造的错误。
 */
typedef enum _BadCase
{
    BAD_UNWRITTEN_SLOT,      /* 有槽位从没写过 */
    BAD_UNBALANCED_JOIN,     /* 窗口只在一个分支里被消费 */
    BAD_OUTER_CONSUMED_FIRST,/* 内层窗口还开着就消费外层 */
    BAD_OPEN_AT_RETURN,      /* 返回时窗口还开着 */
    BAD_INSIDE_V1_REGION,    /* V1 栈操作没配平就消费窗口 */
    BAD_WRITE_AFTER_CONSUMED,/* 窗口消费之后又写它的槽位 */

} _BadCase;

static bool build_bad_case(woort_IRCompiler* irc, _BadCase kind)
{
    woort_IRConstantIndex const c_entry = woort_IRCompiler_add_constant(irc);
    woort_IRConstantIndex const c_five = woort_IRCompiler_add_constant(irc);
    (void)c_entry;

    woort_IRFunction* f = NULL;
    if (!woort_IRCompiler_add_function(irc, 0, 0, &f))
        return false;

    const woort_IRValue* const five = CONST(f, c_five);
    woort_IRValue* r = woort_IRFunction_new_vreg(f);
    bool ok = true;

    switch (kind)
    {
    case BAD_UNWRITTEN_SLOT:
    {
        woort_IRWindow* w = woort_IRFunction_new_window(f, 2);
        ok = woort_IR_MOV(f, SLOT(w, 0), five) &&
            woort_IR_CALLNWO2(f, c_five, w, r);
        break;
    }
    case BAD_UNBALANCED_JOIN:
    {
        woort_IRLabel* const l_end = woort_IRFunction_new_label(f);
        woort_IRWindow* w = woort_IRFunction_new_window(f, 1);
        ok = woort_IR_MOV(f, SLOT(w, 0), five) &&
            woort_IR_jcc(f, five, l_end) &&
            woort_IR_CALLNWO2(f, c_five, w, r) &&
            woort_IR_bind(f, l_end);
        break;
    }
    case BAD_OUTER_CONSUMED_FIRST:
    {
        woort_IRWindow* outer = woort_IRFunction_new_window(f, 1);
        ok = woort_IR_MOV(f, SLOT(outer, 0), five);
        woort_IRWindow* inner = woort_IRFunction_new_window(f, 1);
        ok = ok &&
            woort_IR_MOV(f, SLOT(inner, 0), five) &&
            woort_IR_CALLNWO2(f, c_five, outer, r);
        break;
    }
    case BAD_OPEN_AT_RETURN:
    {
        woort_IRWindow* w = woort_IRFunction_new_window(f, 1);
        ok = woort_IR_MOV(f, SLOT(w, 0), five) &&
            woort_IR_ret(f, five);
        break;
    }
    case BAD_INSIDE_V1_REGION:
    {
        woort_IRWindow* w = woort_IRFunction_new_window(f, 1);
        ok = woort_IR_MOV(f, SLOT(w, 0), five) &&
            woort_IR_PUSHCHK(f, five) &&
            woort_IR_CALLNWO2(f, c_five, w, r);
        break;
    }
    default: /* BAD_WRITE_AFTER_CONSUMED */
    {
        woort_IRWindow* w = woort_IRFunction_new_window(f, 1);
        ok = woort_IR_MOV(f, SLOT(w, 0), five) &&
            woort_IR_CALLNWO2(f, c_five, w, r) &&
            woort_IR_MOV(f, SLOT(w, 0), five);
        break;
    }
    }

    (void)woort_IR_ret(f, r);
    return ok;
}

static void test_diagnostics(void)
{
    TEST_BEGIN("diagnostics (finish rejects malformed windows)");
    _Harness h;
    h_init(&h);

    static const struct
    {
        _BadCase m_kind;
        const char* m_name;

    } cases[] = {
        { BAD_UNWRITTEN_SLOT, "unwritten slot" },
        { BAD_UNBALANCED_JOIN, "window consumed on one branch only" },
        { BAD_OUTER_CONSUMED_FIRST, "outer consumed while inner is open" },
        { BAD_OPEN_AT_RETURN, "window still open at return" },
        { BAD_INSIDE_V1_REGION, "window consumed inside V1 stack region" },
        { BAD_WRITE_AFTER_CONSUMED, "slot written after its window was consumed" },
    };

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k)
    {
        _Harness bad;
        h_init(&bad);

        if (!build_bad_case(bad.irc, cases[k].m_kind))
        {
            (void)printf("    [%s] building the IR failed\n", cases[k].m_name);
            ++h.failures;
        }
        else
        {
            woort_CodeEnv* cenv = NULL;
            if (woort_IRCompiler_finish(bad.irc, &cenv))
            {
                (void)printf("    [%s] finish succeeded, expected failure\n", cases[k].m_name);
                ++h.failures;
            }
        }

        /* finish 的诊断经 woort_log 写到 stderr，会出现在测试输出里 */
        h_close(&bad);
    }

    TEST_END();
}

/* ========== 17. 深帧：操作数区越过临时槽窗口的边界 ========== */

/*
 * 与 test_ir_stack_margin 相同的构造：nv 个同时活跃的 vreg 把帧撑过 -126。
 * 入口预留必须把窗口的位置算进去。
 */
static void test_deep_frame(void)
{
    TEST_BEGIN("deep_frame (operand area beyond the -126 boundary)");
    _Harness h;
    h_init(&h);

    enum { E_V1, E_V2, C_SUB, C_ONE };
    const uint32_t nv = 200;

    for (int i = 0; i < 4; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_sub = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 2, 0, &f_sub));
    {
        const woort_IRValue* a = woort_IRFunction_get_argument(f_sub, 0);
        const woort_IRValue* b = woort_IRFunction_get_argument(f_sub, 1);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_sub);
        CHECK(&h, woort_IR_SUBI(f_sub, r, a, b));
        CHECK(&h, woort_IR_ret(f_sub, r));
    }

    /* 两个入口函数都返回 nv * x：先把 nv 个 vreg 全部置为 x，再把它们逐个加到 r 上 */
    woort_IRFunction* mains[2] = { NULL, NULL };
    for (int k = 0; k < 2; ++k)
    {
        CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &mains[k]));
        if (mains[k] == NULL)
            break;

        woort_IRFunction* const f = mains[k];
        const woort_IRValue* const x = woort_IRFunction_get_argument(f, 0);

        woort_IRValue** const vs = (woort_IRValue**)malloc((size_t)nv * sizeof(woort_IRValue*));
        if (vs == NULL)
        {
            ++h.failures;
            break;
        }

        for (uint32_t i = 0; i < nv; ++i)
        {
            vs[i] = woort_IRFunction_new_vreg(f);
            CHECK(&h, woort_IR_MOV(f, vs[i], x));
        }

        /* r = (nv + 1) * x */
        woort_IRValue* r = woort_IRFunction_new_vreg(f);
        CHECK(&h, woort_IR_MOV(f, r, x));
        for (uint32_t i = 0; i < nv; ++i)
        {
            woort_IRValue* next = woort_IRFunction_new_vreg(f);
            CHECK(&h, woort_IR_ADDI(f, next, r, vs[i]));
            r = next;
        }
        free(vs);

        /* out = r - x = nv * x */
        woort_IRValue* out = woort_IRFunction_new_vreg(f);
        if (k == 0)
        {
            /* V1：实参逆序压栈，第 0 个实参最后压 */
            CHECK(&h, woort_IR_PUSHCHK(f, x));
            CHECK(&h, woort_IR_PUSHCHK(f, r));
            CHECK(&h, woort_IR_CALLNWO(f, C_SUB, 2, out));
        }
        else
        {
            /* V2：槽位在 200 个 vreg 之下 */
            woort_IRWindow* w = woort_IRFunction_new_window(f, 2);
            CHECK(&h, woort_IR_MOV(f, SLOT(w, 0), r));
            CHECK(&h, woort_IR_MOV(f, SLOT(w, 1), x));
            CHECK(&h, woort_IR_CALLNWO2(f, C_SUB, w, out));
        }
        CHECK(&h, woort_IR_ret(f, out));
    }
    if (mains[1] == NULL)
    {
        (void)printf("FAIL (add_function failed)\n");
        h_close(&h);
        return;
    }

    h_int(&h, C_ONE, 1);
    h_function(&h, C_SUB, f_sub);
    h_closure(&h, E_V1, mains[0]);
    h_closure(&h, E_V2, mains[1]);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        for (int k = 0; k < 2; ++k)
        {
            h_call(&h, (woort_IRConstantIndex)k, 3);
            CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
            CHECK(&h, woort_int(h.sv + 1) == (woort_Int)nv * 3);
        }
    }

    /* 两个函数的 vreg 完全相同，V2 的入口预留恰好多出窗口的两个位置 */
    dump_function(&h, mains[0]);
    CHECK(&h, count_push_checks() == 2);
    const int64_t reserve_v1 = entry_reserve(&h, mains[0]);
    dump_function(&h, mains[1]);
    CHECK(&h, count_push_checks() == 0);
    const int64_t reserve_v2 = entry_reserve(&h, mains[1]);
    CHECK(&h, reserve_v1 > 126); /* 已经越过边界，预留里含临时槽的 3 个余量 */
    CHECK(&h, reserve_v2 == reserve_v1 + 2);
    CHECK(&h, count_mnemonic("SPSET") == 0);

    TEST_END();
}

/* ========== 18. 帧深超过 S16：sp 校正回退为相对移动 ========== */

/*
 * SPSET 的操作数只有 16 位。入口预留后的位置离 SB 超过约 32767 个槽时放不下，
 * 校正 sp 改为从当前位置出发的相对移动：位置升高用 POPR，位置降低用 PUSHRCHK。
 * 这里用 nv 个同时活跃的 vreg 把帧撑到那个深度（必须是真的 vreg：靠声明捕获数
 * 撑深度不行，CALLNWO 不展开捕获区）。
 * main 的结构与 test_nested_windows 的 forward 相同：sp 依次为 入口 → 位置 1 →
 * 位置 0 → 位置 1。
 */
static void test_deep_frame_s16(void)
{
    TEST_BEGIN("deep_frame_s16 (sp adjusted by relative moves)");
    _Harness h;
    h_init(&h);

    /* 捕获 0 个 + vreg 32770 个 + 窗口 3 个位置 + 临时槽余量 3 个，入口位置在 S16 之外 */
    const uint32_t nv = 32770;
    enum { E_MAIN, C_F, C_G, C_H, C_10, C_1, C_2 };

    for (int i = 0; i <= C_2; ++i)
        (void)woort_IRCompiler_add_constant(h.irc);

    woort_IRFunction* f_f = make_lin2(h.irc, C_10, C_1);
    woort_IRFunction* f_g = make_addconst(h.irc, C_1);
    CHECK(&h, f_f != NULL && f_g != NULL);

    woort_IRFunction* f_h = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_h));
    {
        const woort_IRValue* x = woort_IRFunction_get_argument(f_h, 0);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_h);
        CHECK(&h, woort_IR_MULI(f_h, r, x, CONST(f_h, C_2)));
        CHECK(&h, woort_IR_ret(f_h, r));
    }

    woort_IRFunction* f_main = NULL;
    CHECK(&h, woort_IRCompiler_add_function(h.irc, 1, 0, &f_main));
    if (f_main != NULL)
    {
        const woort_IRValue* const x = woort_IRFunction_get_argument(f_main, 0);

        /* vs[i] = x，全部用到函数末尾，因此同时活跃 */
        woort_IRValue** const vs = (woort_IRValue**)malloc((size_t)nv * sizeof(woort_IRValue*));
        if (vs == NULL)
            ++h.failures;

        woort_IRValue* keep = woort_IRFunction_new_vreg(f_main);
        CHECK(&h, woort_IR_MOV(f_main, keep, x));

        for (uint32_t i = 0; i < nv && vs != NULL; ++i)
        {
            vs[i] = woort_IRFunction_new_vreg(f_main);
            if (vs[i] == NULL)
            {
                ++h.failures;
                break;
            }
            (void)woort_IR_MOV(f_main, vs[i], x);
        }

        /* forward：f(g(x), h(x)) */
        woort_IRWindow* outer = woort_IRFunction_new_window(f_main, 2);
        woort_IRWindow* g = woort_IRFunction_new_window(f_main, 1);
        (void)woort_IR_MOV(f_main, SLOT(g, 0), x);
        (void)woort_IR_CALLNWO2(f_main, C_G, g, SLOT(outer, 0));
        woort_IRWindow* hh = woort_IRFunction_new_window(f_main, 1);
        (void)woort_IR_MOV(f_main, SLOT(hh, 0), x);
        (void)woort_IR_CALLNWO2(f_main, C_H, hh, SLOT(outer, 1));

        woort_IRValue* t = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_CALLNWO2(f_main, C_F, outer, t);

        /* 用到所有 vreg：它们的活跃区间覆盖上面整段 */
        for (uint32_t i = 0; i < nv && vs != NULL && vs[i] != NULL; ++i)
        {
            woort_IRValue* next = woort_IRFunction_new_vreg(f_main);
            (void)woort_IR_ADDI(f_main, next, keep, vs[i]);
            keep = next;
        }
        free(vs);

        /* 结果与 vreg 的值无关：返回 t + nv * 0 */
        (void)woort_IR_SUBI(f_main, keep, keep, keep);
        woort_IRValue* r = woort_IRFunction_new_vreg(f_main);
        (void)woort_IR_ADDI(f_main, r, t, keep);
        (void)woort_IR_ret(f_main, r);
    }

    h_int(&h, C_10, 10);
    h_int(&h, C_1, 1);
    h_int(&h, C_2, 2);
    h_function(&h, C_F, f_f);
    h_function(&h, C_G, f_g);
    h_function(&h, C_H, f_h);
    h_closure(&h, E_MAIN, f_main);

    PREPARE_OR_RETURN();

    h_start(&h);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass != 0)
            woort_CodeEnv_jit(h.cenv);

        h_call(&h, E_MAIN, 5);
        CHECK(&h, h.status == WOORT_VM_CALL_STATUS_NORMAL);
        CHECK(&h, woort_int(h.sv + 1) == 70); /* 6 * 10 + 10 */
    }

    /* 放不进 SPSET：入口 → 位置 1 升高（POPR 1）→ 位置 0 降低（PUSHRCHK 1）→ 位置 1 升高（POPR 1）。
       函数有六万多条指令，只捕获与 sp 校正和结果有关的助记符 */
    static const char* const filter[] = { "SPSET", "POPR", "PUSHRCHK", "RESULT", NULL };
    dump_function_filtered(&h, f_main, filter);
    CHECK(&h, count_mnemonic("SPSET") == 0);
    CHECK(&h, count_lines_with("POPR        1\n") == 2);
    CHECK(&h, count_lines_with("PUSHRCHK    1\n") == 1);
    CHECK(&h, count_mnemonic("RESULT") == 3);
    CHECK(&h, count_lines_with("POP 0") == 3);

    TEST_END();
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    test_call_window_basic();
    test_call_window_native();
    test_call_window_indirect();
    test_call_window_void();
    test_direct_writes();
    test_boxed_writes();
    test_mkvec2();
    test_mkmap2();
    test_construct_order();
    test_nested_windows();
    test_window_ternary();
    test_window_loop();
    test_mixed_v1_v2();
    test_mixed_v1_after_lift();
    test_outer_write_fallback();
    test_write_below_sp();
    test_diagnostics();
    test_deep_frame();
    test_deep_frame_s16();

    woort_shutdown(NULL, NULL);

    const int failures = g_tests_run - g_tests_passed;
    if (failures == 0)
    {
        printf("test_ir_window: ALL PASS (%d tests)\n", g_tests_run);
        return 0;
    }

    printf("test_ir_window: %d FAILURES\n", failures);
    return 1;
}
