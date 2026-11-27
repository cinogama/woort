/*
 * LDIDSTBOXI/R/B：索引结构体字段，按 int / real / bool 的装箱格式写入栈槽，不压栈。
 * 占用原 WOORT_OPCODE_RESERVED_0 的编码（主指令 17），mode 0/1/2 依次为 I/R/B，
 * 操作数布局与 LDIDSTRUCT 相同（A8 字段下标、B8 结构体、C8 目标）。
 *
 * 需要访问编译器内部结构（m_committed_codes / CodeEnv 代码区），内部头会引入
 * 完整版 woort_Value 定义，因此本翻译单元以 WOORT_IMPL 方式包含公共头（同
 * test_ir_stack_margin.c）。
 *
 * 每个用例各跑两遍：解释器，以及 woort_CodeEnv_jit 之后（JIT 默认只在 Release
 * 构建开启，是否真的走了 JIT 由 cenv->m_jit_linked 报告）。
 *   1. fields       —— IR 接口发射出 LDIDSTBOX 族一字指令（白盒），运行结果与
 *                      LDIDSTRUCT + BOXDYN 的基准路径逐字段一致，并按装箱格式核对
 *                      原始位（Int62 / Float63 / Bool 内联，其余扩展装箱）；
 *   2. sp_untouched —— 指令不改动 sp：其后的一次 POP 取到的仍是指令之前压入的值；
 *   3. far_slots    —— 结构体与目标槽都超出 S8 时经临时槽中转；
 *   4. mode3        —— mode 3 留空：反汇编输出 UNKNOWN_OPCODE(17)，解释器 panic
 *                      Bad command。
 */
#define WOORT_IMPL
#include "woort.h"

#include "woort_codeenv.h"
#include "woort_disassembly.h"
#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_opcode_dispatcher.h"
#include "woort_opcode_formal.h"
#include "woort_vm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(WOORT_OPCODE_LDIDSTBOX == 17,
    "LDIDSTBOX takes over the encoding of the former RESERVED_0 (RESULT).");

/* ========== 字段表与装箱格式的独立参照 ========== */

typedef enum _FieldType { FT_INT, FT_REAL, FT_BOOL } _FieldType;

typedef struct _FieldSpec
{
    _FieldType m_type;
    woort_Int m_int;    /* FT_INT / FT_BOOL */
    woort_Real m_real;  /* FT_REAL */
    const char* m_name;

} _FieldSpec;

/*
 * 整数覆盖 Int62 边界两侧与 64 位极值；实数覆盖 Float63 可表示（含负数，
 * 验证符号位）与不可表示（指数最高位与次高位相同：1e300、-0.0、次正规数）。
 */
static const _FieldSpec g_fields[] = {
    { FT_INT,  42,                                    0.0,   "int 42" },
    { FT_INT,  -5,                                    0.0,   "int -5" },
    { FT_INT,  INT64_MAX,                             0.0,   "int INT64_MAX" },
    { FT_INT,  INT64_MIN,                             0.0,   "int INT64_MIN" },
    { FT_INT,  (woort_Int)((1ULL << 61) - 1),         0.0,   "int Int62 max" },
    { FT_INT,  (woort_Int)(1ULL << 61),               0.0,   "int Int62 max + 1" },
    { FT_INT,  -(woort_Int)(1ULL << 61),              0.0,   "int Int62 min" },
    { FT_REAL, 0,                                     3.5,   "real 3.5" },
    { FT_REAL, 0,                                     -2.25, "real -2.25" },
    { FT_REAL, 0,                                     1e300, "real 1e300" },
    { FT_REAL, 0,                                     -0.0,  "real -0.0" },
    { FT_REAL, 0,                                     4.9406564584124654e-324, "real denormal" },
    { FT_BOOL, 0,                                     0.0,   "bool false" },
    { FT_BOOL, 1,                                     0.0,   "bool true" },
};
#define FIELD_COUNT ((int)(sizeof(g_fields) / sizeof(g_fields[0])))

static int g_failures = 0;
static const char* g_run_tag = ""; /* 当前是哪个用例、哪一遍运行（解释器 / JIT） */

/* 记一次失败并返回 1：先写明是哪一遍运行，再写具体检查 */
static int _fail(const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)printf("FAIL %s: %s: ", g_run_tag, tag);
    (void)vprintf(fmt, ap);
    (void)printf("\n");
    va_end(ap);
    ++g_failures;
    return 1;
}

/* 用例结束后打印结果：期间没有记过失败才算通过 */
static void _report(const char* tag, int failures_before, int jit, int jit_linked)
{
    if (g_failures == failures_before)
    {
        (void)printf("ok %s%s\n", tag,
            !jit ? "" : jit_linked ? " [jit linked]" : " [jit not linked]");
    }
}

/*
 * 独立于运行时的装箱预期（见 doc/values.md）：能内联时返回 1 并给出编码，
 * 否则返回 0，表示该值存放在 GC 分配的扩展盒中。
 */
static int _ref_box(const _FieldSpec* s, uint64_t* out_bits)
{
    switch (s->m_type)
    {
    case FT_INT:
        if (s->m_int >= -(woort_Int)(1ULL << 61) && s->m_int < (woort_Int)(1ULL << 61))
        {
            *out_bits = ((uint64_t)s->m_int << 2) | WOORT_BOX_VALUE_TYPE_INT;
            return 1;
        }
        return 0;
    case FT_REAL:
    {
        uint64_t bits;
        memcpy(&bits, &s->m_real, sizeof(bits));

        /* 指数最高位与次高位不同才能去掉一位、压成 Float63 */
        if (((bits >> 62) & 1) != ((bits >> 61) & 1))
        {
            *out_bits = (bits & 0x8000000000000000ULL)
                | ((bits & 0x3FFFFFFFFFFFFFFFULL) << 1)
                | WOORT_BOX_VALUE_TYPE_REAL;
            return 1;
        }
        return 0;
    }
    case FT_BOOL:
        *out_bits = ((uint64_t)(s->m_int != 0) << 3) | WOORT_BOX_VALUE_TYPE_BOOL;
        return 1;
    }
    return 0;
}

/* 检查栈槽 idx 里的 DynBox：类型、拆箱后的值、是否内联，内联时原始位逐位相等 */
static int _check_boxed(
    const char* tag, const _FieldSpec* s, const woort_VMRuntime* vm, woort_StackValue idx)
{
    uint64_t expect_bits = 0;
    const int expect_inline = _ref_box(s, &expect_bits);

    /* woort_StackValue 是相对 sb 的下标，原始装箱位只能从栈槽里读 */
    const uint64_t got_bits = vm->m_sb[3 + idx].m_dynamic.m_boxed;
    const int got_inline = (got_bits & 0b111) != 0;
    const woort_BoxValueType expect_type =
        s->m_type == FT_INT  ? WOORT_BOX_VALUE_TYPE_INT  :
        s->m_type == FT_REAL ? WOORT_BOX_VALUE_TYPE_REAL :
                               WOORT_BOX_VALUE_TYPE_BOOL;

    if (woort_unbox_type(idx) != expect_type)
        return _fail(tag, "%s: type %d, expected %d",
            s->m_name, (int)woort_unbox_type(idx), (int)expect_type);

    switch (s->m_type)
    {
    case FT_INT:
        if (woort_unbox_int(idx) != s->m_int)
            return _fail(tag, "%s: got %lld, expected %lld",
                s->m_name, (long long)woort_unbox_int(idx), (long long)s->m_int);
        break;
    case FT_REAL:
    {
        /* 按位比较：-0.0 与次正规数用 == 区分不出来 */
        const woort_Real got = woort_unbox_real(idx);
        if (memcmp(&got, &s->m_real, sizeof(got)) != 0)
            return _fail(tag, "%s: real bits differ", s->m_name);
        break;
    }
    case FT_BOOL:
        if (woort_unbox_bool(idx) != (s->m_int != 0))
            return _fail(tag, "%s: got %d", s->m_name, (int)woort_unbox_bool(idx));
        break;
    }

    if (got_inline != expect_inline)
        return _fail(tag, "%s: %s, expected %s", s->m_name,
            got_inline ? "inline" : "extended box",
            expect_inline ? "inline" : "extended box");

    if (expect_inline && got_bits != expect_bits)
        return _fail(tag, "%s: raw bits 0x%llx, expected 0x%llx", s->m_name,
            (unsigned long long)got_bits, (unsigned long long)expect_bits);

    return 0;
}

/* ========== IR 构造 ========== */

/* 按字段顺序压入各字段常量后 MKSTRUCT，返回装着结构体的寄存器 */
static woort_IRValue* _emit_struct(woort_IRFunction* f, const woort_IRConstantIndex* c_field)
{
    for (int k = 0; k < FIELD_COUNT; ++k)
        (void)woort_IR_PUSHCHK(f, woort_IRFunction_fetch_const(f, c_field[k]));

    woort_IRValue* const st = woort_IRFunction_new_vreg(f);
    (void)woort_IR_MKSTRUCT(f, st, FIELD_COUNT);
    return st;
}

/* 把结构体 st 的第 idx 个字段装箱到 dst */
static void _emit_field_box(woort_IRFunction* f, woort_IRValue* dst, const woort_IRValue* st, int idx)
{
    switch (g_fields[idx].m_type)
    {
    case FT_INT:  (void)woort_IR_LDIDSTBOXI(f, dst, st, (uint32_t)idx); break;
    case FT_REAL: (void)woort_IR_LDIDSTBOXR(f, dst, st, (uint32_t)idx); break;
    case FT_BOOL: (void)woort_IR_LDIDSTBOXB(f, dst, st, (uint32_t)idx); break;
    }
}

/* 基准路径：LDIDSTRUCT + BOXDYN，语义应与 LDIDSTBOX* 完全相同 */
static void _emit_field_box_ref(woort_IRFunction* f, woort_IRValue* dst, const woort_IRValue* st, int idx)
{
    const uint8_t type =
        g_fields[idx].m_type == FT_INT  ? WOORT_BOX_VALUE_TYPE_INT  :
        g_fields[idx].m_type == FT_REAL ? WOORT_BOX_VALUE_TYPE_REAL :
                                          WOORT_BOX_VALUE_TYPE_BOOL;

    woort_IRValue* const raw = woort_IRFunction_new_vreg(f);
    (void)woort_IR_LDIDSTRUCT(f, raw, st, (uint32_t)idx);
    (void)woort_IR_BOXDYN(f, dst, type, raw);
}

/* ========== 白盒：解码函数的字节码 ========== */

static const woort_Bytecode* _fn_begin(const woort_CodeEnv* cenv, const woort_IRFunction* f)
{
    return cenv->m_code_begin + f->m_code_offset;
}

static const woort_Bytecode* _fn_end(const woort_CodeEnv* cenv, const woort_IRFunction* f)
{
    return _fn_begin(cenv, f) + f->m_code_length;
}

static int _is_ldidstbox(woort_Bytecode bc)
{
    return (uint8_t)WOORT_BYTECODE(OP6, bc) == WOORT_OPCODE_LDIDSTBOX;
}

/*
 * f 中恰好 FIELD_COUNT 条 LDIDSTBOX：按字段顺序出现，mode 与字段类型对应，
 * A8 为字段下标，B8 为同一个结构体槽；没有降级成 BOXDYN。
 */
static void _check_field_emission(const woort_CodeEnv* cenv, const woort_IRFunction* f, const char* tag)
{
    int seen = 0;
    int struct_slot = 0;

    for (const woort_Bytecode* p = _fn_begin(cenv, f); p < _fn_end(cenv, f);)
    {
        const woort_Bytecode bc = p[0];

        if (_is_ldidstbox(bc))
        {
            if (seen >= FIELD_COUNT)
                (void)_fail(tag, "more than %d LDIDSTBOX instructions", FIELD_COUNT);
            else
            {
                const int mode = (int)WOORT_BYTECODE(M2, bc);
                const int a8 = (int)(uint8_t)WOORT_BYTECODE(A8, bc);
                const int b8 = (int)(int8_t)WOORT_BYTECODE(B8, bc);
                const int expect_mode =
                    g_fields[seen].m_type == FT_INT  ? 0 :
                    g_fields[seen].m_type == FT_REAL ? 1 : 2;

                if (mode != expect_mode || a8 != seen)
                    (void)_fail(tag, "LDIDSTBOX #%d: mode %d field %d, expected mode %d field %d",
                        seen, mode, a8, expect_mode, seen);
                if (seen == 0)
                    struct_slot = b8;
                else if (b8 != struct_slot)
                    (void)_fail(tag, "LDIDSTBOX #%d: struct slot %d, expected %d",
                        seen, b8, struct_slot);
            }
            ++seen;
        }
        else if ((uint8_t)WOORT_BYTECODE(OP6, bc) == WOORT_OPCODE_DYN
            && WOORT_BYTECODE(M2, bc) == 0)
        {
            (void)_fail(tag, "LDIDSTBOX was lowered to BOXDYN");
        }

        p = woort_OpcodeDispatcher_decode(p, NULL, NULL);
    }

    if (seen != FIELD_COUNT)
        (void)_fail(tag, "%d LDIDSTBOX instructions, expected %d", seen, FIELD_COUNT);
}

/* ========== 运行 ========== */

typedef struct _Ctx
{
    woort_IRCompiler* m_irc;
    woort_CodeEnv* m_cenv;
    woort_VMRuntime* m_vm;

} _Ctx;

static woort_IRConstantIndex* _add_field_consts(_Ctx* ctx)
{
    woort_IRConstantIndex* const c_field =
        (woort_IRConstantIndex*)malloc(FIELD_COUNT * sizeof(woort_IRConstantIndex));

    for (int k = 0; k < FIELD_COUNT; ++k)
        c_field[k] = woort_IRCompiler_add_constant(ctx->m_irc);
    return c_field;
}

/* finish 之后填入字段常量。入口闭包常量要在这之后、_ctx_boot 之前登记 */
static void _ctx_finish(_Ctx* ctx, const woort_IRConstantIndex* c_field)
{
    (void)woort_IRCompiler_finish(ctx->m_irc, &ctx->m_cenv);

    woort_CodeEnv_lock(ctx->m_cenv);
    for (int k = 0; k < FIELD_COUNT; ++k)
    {
        if (g_fields[k].m_type == FT_REAL)
            woort_CodeEnv_set_const_real(ctx->m_cenv, c_field[k], g_fields[k].m_real);
        else
            woort_CodeEnv_set_const_int(ctx->m_cenv, c_field[k], g_fields[k].m_int);
    }
    woort_CodeEnv_unlock(ctx->m_cenv);
}

/* （可选）JIT，然后建 VM */
static void _ctx_boot(_Ctx* ctx, int jit)
{
    if (jit)
        woort_CodeEnv_jit(ctx->m_cenv);

    (void)woort_VMRuntime_create(&ctx->m_vm);
    (void)woort_VMRuntime_swap(ctx->m_vm);
}

static void _ctx_end(_Ctx* ctx)
{
    (void)woort_VMRuntime_swap(NULL);
    woort_CodeEnv_drop(ctx->m_cenv);
    woort_VMRuntime_destroy(ctx->m_vm);
    woort_IRCompiler_close(ctx->m_irc);
}

/* 把入口闭包常量登记成脚本函数入口 */
static void _set_entry(const _Ctx* ctx, woort_IRFunction* f, woort_IRConstantIndex c_entry)
{
    const woort_Bytecode* addr;
    (void)woort_CodeEnv_query_function(ctx->m_cenv, f, &addr);
    woort_CodeEnv_lock(ctx->m_cenv);
    woort_CodeEnv_set_const_script_closure(ctx->m_cenv, c_entry, addr);
    woort_CodeEnv_unlock(ctx->m_cenv);
}

/* 调用入口，返回值必须是 count 个元素的向量，对每个元素调用 check(k, 元素) */
static void _invoke_vector(
    const _Ctx* ctx,
    woort_IRConstantIndex c_entry,
    const char* tag,
    int count,
    int (*check)(const woort_VMRuntime* vm, int k, woort_StackValue elem))
{
    woort_StackValue sv;

    (void)woort_push_reserve(3, &sv); /* [0] 函数 [1] 返回值 [2] 元素 */
    woort_load_const(sv, ctx->m_cenv, c_entry);

    const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
    if (st != WOORT_VM_CALL_STATUS_NORMAL)
        (void)_fail(tag, "invoke status %d", (int)st);
    else if (woort_unbox_type(sv + 1) != WOORT_BOX_VALUE_TYPE_VEC)
        (void)_fail(tag, "return value is not a vector (type %d)", (int)woort_unbox_type(sv + 1));
    else if ((int)woort_vec_len(sv + 1) != count)
        (void)_fail(tag, "vector length %u, expected %d", (unsigned)woort_vec_len(sv + 1), count);
    else
    {
        for (int k = 0; k < count; ++k)
        {
            if (!woort_vec_get(sv + 2, sv + 1, (size_t)k))
                (void)_fail(tag, "woort_vec_get(%d) failed", k);
            else
                (void)check(ctx->m_vm, k, sv + 2);
        }
    }

    woort_pop(3);
}

/* ---------- 1. 字段装箱：与 LDIDSTRUCT + BOXDYN 基准对比 ---------- */

static int _check_field(const woort_VMRuntime* vm, int k, woort_StackValue elem)
{
    return _check_boxed("fields", &g_fields[k], vm, elem);
}

/* 建结构体，逐字段装箱，压栈成向量返回：vec[k] = box(st.k) */
static void _emit_boxed_vector(
    woort_IRFunction* f,
    const woort_IRConstantIndex* c_field,
    void (*box_fn)(woort_IRFunction*, woort_IRValue*, const woort_IRValue*, int))
{
    woort_IRValue* boxes[FIELD_COUNT];
    woort_IRValue* const st = _emit_struct(f, c_field);

    for (int k = 0; k < FIELD_COUNT; ++k)
    {
        boxes[k] = woort_IRFunction_new_vreg(f);
        box_fn(f, boxes[k], st, k);
    }

    /* MKVEC 按压栈顺序排列元素 */
    for (int k = 0; k < FIELD_COUNT; ++k)
        (void)woort_IR_PUSHCHK(f, boxes[k]);

    woort_IRValue* const vec = woort_IRFunction_new_vreg(f);
    (void)woort_IR_MKVEC(f, vec, FIELD_COUNT);
    (void)woort_IR_ret(f, vec);
}

static void run_fields(int jit)
{
    char tag[64];
    (void)snprintf(tag, sizeof(tag), "fields(%s)", jit ? "jit" : "interpreter");
    g_run_tag = tag;
    const int failures_before = g_failures;

    _Ctx ctx;
    ctx.m_irc = woort_IRCompiler_create();
    woort_IRConstantIndex* const c_field = _add_field_consts(&ctx);
    const woort_IRConstantIndex c_new = woort_IRCompiler_add_constant(ctx.m_irc);
    const woort_IRConstantIndex c_ref = woort_IRCompiler_add_constant(ctx.m_irc);

    woort_IRFunction* f_new;
    woort_IRFunction* f_ref;
    (void)woort_IRCompiler_add_function(ctx.m_irc, 0, 0, &f_new);
    _emit_boxed_vector(f_new, c_field, &_emit_field_box);
    (void)woort_IRCompiler_add_function(ctx.m_irc, 0, 0, &f_ref);
    _emit_boxed_vector(f_ref, c_field, &_emit_field_box_ref);

    _ctx_finish(&ctx, c_field);
    _set_entry(&ctx, f_new, c_new);
    _set_entry(&ctx, f_ref, c_ref);
    _ctx_boot(&ctx, jit);

    _check_field_emission(ctx.m_cenv, f_new, tag);

    /* 基准路径先行：它自己出错时不能算到 LDIDSTBOX 头上 */
    _invoke_vector(&ctx, c_ref, tag, FIELD_COUNT, &_check_field);
    _invoke_vector(&ctx, c_new, tag, FIELD_COUNT, &_check_field);

    _report(tag, failures_before, jit, ctx.m_cenv->m_jit_linked);
    _ctx_end(&ctx);
    free(c_field);
}

/* ---------- 2. 指令不改动 sp ---------- */

/* 向量：[box(st.0), box(st.2), box(st.8), box(st.13), box(弹出的值)] */
static int _check_sp(const woort_VMRuntime* vm, int k, woort_StackValue elem)
{
    static const _FieldSpec sentinel = { FT_INT, 12345, 0.0, "popped sentinel" };

    switch (k)
    {
    case 0: return _check_boxed("sp_untouched", &g_fields[0], vm, elem);
    case 1: return _check_boxed("sp_untouched", &g_fields[2], vm, elem);
    case 2: return _check_boxed("sp_untouched", &g_fields[8], vm, elem);
    case 3: return _check_boxed("sp_untouched", &g_fields[13], vm, elem);
    default: return _check_boxed("sp_untouched", &sentinel, vm, elem);
    }
}

static void run_sp_untouched(int jit)
{
    char tag[64];
    (void)snprintf(tag, sizeof(tag), "sp_untouched(%s)", jit ? "jit" : "interpreter");
    g_run_tag = tag;
    const int failures_before = g_failures;

    _Ctx ctx;
    ctx.m_irc = woort_IRCompiler_create();
    woort_IRConstantIndex* const c_field = _add_field_consts(&ctx);
    const woort_IRConstantIndex c_sentinel = woort_IRCompiler_add_constant(ctx.m_irc);
    const woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(ctx.m_irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(ctx.m_irc, 0, 0, &f);
    {
        /* 先压入一个值；LDIDSTBOX* 之后紧跟的 POP 必须取回它 */
        (void)woort_IR_PUSHCHK(f, woort_IRFunction_fetch_const(f, c_sentinel));

        woort_IRValue* const st = _emit_struct(f, c_field);
        /* 内联装箱的整数、扩展装箱的整数、实数、布尔各一个 */
        woort_IRValue* const b0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* const b1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* const b2 = woort_IRFunction_new_vreg(f);
        woort_IRValue* const b3 = woort_IRFunction_new_vreg(f);
        _emit_field_box(f, b0, st, 0);
        _emit_field_box(f, b1, st, 2);
        _emit_field_box(f, b2, st, 8);
        _emit_field_box(f, b3, st, 13);

        woort_IRValue* const popped = woort_IRFunction_new_vreg(f);
        (void)woort_IR_POP(f, popped);

        (void)woort_IR_PUSHCHK(f, b0);
        (void)woort_IR_PUSHCHK(f, b1);
        (void)woort_IR_PUSHCHK(f, b2);
        (void)woort_IR_PUSHCHK(f, b3);
        woort_IRValue* const boxed = woort_IRFunction_new_vreg(f);
        (void)woort_IR_BOXDYN(f, boxed, WOORT_BOX_VALUE_TYPE_INT, popped);
        (void)woort_IR_PUSHCHK(f, boxed);

        woort_IRValue* const vec = woort_IRFunction_new_vreg(f);
        (void)woort_IR_MKVEC(f, vec, 5);
        (void)woort_IR_ret(f, vec);
    }

    _ctx_finish(&ctx, c_field);
    woort_CodeEnv_lock(ctx.m_cenv);
    woort_CodeEnv_set_const_int(ctx.m_cenv, c_sentinel, 12345);
    woort_CodeEnv_unlock(ctx.m_cenv);
    _set_entry(&ctx, f, c_entry);
    _ctx_boot(&ctx, jit);

    _invoke_vector(&ctx, c_entry, tag, 5, &_check_sp);

    _report(tag, failures_before, jit, ctx.m_cenv->m_jit_linked);
    _ctx_end(&ctx);
    free(c_field);
}

/* ---------- 3. 结构体与目标槽超出 S8：经临时槽中转 ---------- */

static int _check_far(const woort_VMRuntime* vm, int k, woort_StackValue elem)
{
    static const _FieldSpec sum = { FT_INT, 200, 0.0, "filler sum" };

    switch (k)
    {
    case 0: return _check_boxed("far_slots", &g_fields[0], vm, elem);
    case 1: return _check_boxed("far_slots", &g_fields[2], vm, elem);
    case 2: return _check_boxed("far_slots", &g_fields[8], vm, elem);
    case 3: return _check_boxed("far_slots", &g_fields[13], vm, elem);
    default: return _check_boxed("far_slots", &sum, vm, elem);
    }
}

static void run_far_slots(int jit)
{
    enum { FILLERS = 200 };
    char tag[64];
    (void)snprintf(tag, sizeof(tag), "far_slots(%s)", jit ? "jit" : "interpreter");
    g_run_tag = tag;
    const int failures_before = g_failures;

    _Ctx ctx;
    ctx.m_irc = woort_IRCompiler_create();
    woort_IRConstantIndex* const c_field = _add_field_consts(&ctx);
    const woort_IRConstantIndex c_one = woort_IRCompiler_add_constant(ctx.m_irc);
    const woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(ctx.m_irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(ctx.m_irc, 0, 0, &f);
    {
        /* 200 个跨越 LDIDSTBOX* 存活的寄存器，把结构体和目标槽挤到 S8 之外 */
        woort_IRValue* fill[FILLERS];
        for (int i = 0; i < FILLERS; ++i)
        {
            fill[i] = woort_IRFunction_new_vreg(f);
            (void)woort_IR_MOV(f, fill[i], woort_IRFunction_fetch_const(f, c_one));
        }

        woort_IRValue* const st = _emit_struct(f, c_field);
        /* 内联装箱的整数、扩展装箱的整数、实数、布尔各一个 */
        woort_IRValue* const b0 = woort_IRFunction_new_vreg(f);
        woort_IRValue* const b1 = woort_IRFunction_new_vreg(f);
        woort_IRValue* const b2 = woort_IRFunction_new_vreg(f);
        woort_IRValue* const b3 = woort_IRFunction_new_vreg(f);
        _emit_field_box(f, b0, st, 0);
        _emit_field_box(f, b1, st, 2);
        _emit_field_box(f, b2, st, 8);
        _emit_field_box(f, b3, st, 13);

        woort_IRValue* r = fill[0];
        for (int i = 1; i < FILLERS; ++i)
        {
            woort_IRValue* const r2 = woort_IRFunction_new_vreg(f);
            (void)woort_IR_ADDI(f, r2, r, fill[i]);
            r = r2;
        }

        (void)woort_IR_PUSHCHK(f, b0);
        (void)woort_IR_PUSHCHK(f, b1);
        (void)woort_IR_PUSHCHK(f, b2);
        (void)woort_IR_PUSHCHK(f, b3);
        woort_IRValue* const boxed = woort_IRFunction_new_vreg(f);
        (void)woort_IR_BOXDYN(f, boxed, WOORT_BOX_VALUE_TYPE_INT, r);
        (void)woort_IR_PUSHCHK(f, boxed);

        woort_IRValue* const vec = woort_IRFunction_new_vreg(f);
        (void)woort_IR_MKVEC(f, vec, 5);
        (void)woort_IR_ret(f, vec);
    }

    _ctx_finish(&ctx, c_field);
    woort_CodeEnv_lock(ctx.m_cenv);
    woort_CodeEnv_set_const_int(ctx.m_cenv, c_one, 1);
    woort_CodeEnv_unlock(ctx.m_cenv);
    _set_entry(&ctx, f, c_entry);
    _ctx_boot(&ctx, jit);

    /*
     * 白盒：四条 LDIDSTBOX 的结构体操作数与目标都是临时槽（-128 读、-127 写），
     * 前一条是 MOVLD 把结构体搬进 -128，后一条是 MOVST 把结果搬回目标槽。
     */
    int seen = 0;
    for (const woort_Bytecode* p = _fn_begin(ctx.m_cenv, f); p < _fn_end(ctx.m_cenv, f);)
    {
        const woort_Bytecode bc = p[0];

        if (_is_ldidstbox(bc))
        {
            const woort_Bytecode prev = p[-1];
            const woort_Bytecode next = p[1];
            const int b8 = (int)(int8_t)WOORT_BYTECODE(B8, bc);
            const int c8 = (int)(int8_t)WOORT_BYTECODE(C8, bc);

            if (b8 != -128 || c8 != -127
                || (uint8_t)WOORT_BYTECODE(OP6, prev) != WOORT_OPCODE_MOV
                || WOORT_BYTECODE(M2, prev) != 0                       /* MOVLD */
                || (int)(int8_t)WOORT_BYTECODE(A8, prev) != -128
                || (uint8_t)WOORT_BYTECODE(OP6, next) != WOORT_OPCODE_MOV
                || WOORT_BYTECODE(M2, next) != 1                       /* MOVST */
                || (int)(int8_t)WOORT_BYTECODE(A8, next) != -127)
            {
                (void)_fail(tag, "LDIDSTBOX #%d: b8 %d c8 %d, expected temp slots -128/-127 with MOVLD/MOVST around",
                    seen, b8, c8);
            }
            ++seen;
        }

        p = woort_OpcodeDispatcher_decode(p, NULL, NULL);
    }
    if (seen != 4)
        (void)_fail(tag, "%d LDIDSTBOX instructions, expected 4", seen);

    _invoke_vector(&ctx, c_entry, tag, 5, &_check_far);

    _report(tag, failures_before, jit, ctx.m_cenv->m_jit_linked);
    _ctx_end(&ctx);
    free(c_field);
}

/* ---------- 4. mode 3 留空 ---------- */

static char g_dis_text[512];
static size_t g_dis_len;

static int _dis_callback(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(g_dis_text + g_dis_len, sizeof(g_dis_text) - g_dis_len, fmt, ap);
    va_end(ap);
    if (n > 0)
        g_dis_len += (size_t)n;
    return n;
}

static const char* _disassemble(const woort_Bytecode* p, const woort_Bytecode** out_next)
{
    g_dis_len = 0;
    g_dis_text[0] = '\0';
    *out_next = woort_disassembly(p, &_dis_callback);
    return g_dis_text;
}

static int g_panic_reason;
static char g_panic_message[256];

static woort_PanicHandler_Action _catch_panic(
    woort_VMRuntime* vm,
    const char* funcname,
    const char* location,
    int line,
    int reason,
    const char* message)
{
    (void)vm;
    (void)funcname;
    (void)location;
    (void)line;
    g_panic_reason = reason;
    (void)snprintf(g_panic_message, sizeof(g_panic_message), "%s", message != NULL ? message : "");
    return WOORT_PANIC_HANDLER_ACTION_ABORT;
}

static void run_mode3(void)
{
    const char* const tag = "mode3(interpreter)";
    g_run_tag = tag;
    const int failures_before = g_failures;

    _Ctx ctx;
    ctx.m_irc = woort_IRCompiler_create();
    woort_IRConstantIndex* const c_field = _add_field_consts(&ctx);
    const woort_IRConstantIndex c_entry = woort_IRCompiler_add_constant(ctx.m_irc);

    woort_IRFunction* f;
    (void)woort_IRCompiler_add_function(ctx.m_irc, 0, 0, &f);
    {
        woort_IRValue* const st = _emit_struct(f, c_field);
        woort_IRValue* const b = woort_IRFunction_new_vreg(f);
        _emit_field_box(f, b, st, 0);
        woort_IRValue* const u = woort_IRFunction_new_vreg(f);
        (void)woort_IR_UNBOXDYN(f, u, WOORT_BOX_VALUE_TYPE_INT, b);
        (void)woort_IR_ret(f, u);
    }

    /* 不做 JIT：保留位只在解释器里有确定的行为 */
    _ctx_finish(&ctx, c_field);
    _set_entry(&ctx, f, c_entry);
    _ctx_boot(&ctx, 0);

    /* 找到那条 LDIDSTBOXI */
    const woort_Bytecode* target = NULL;
    for (const woort_Bytecode* p = _fn_begin(ctx.m_cenv, f); p < _fn_end(ctx.m_cenv, f);)
    {
        if (_is_ldidstbox(p[0]))
            target = p;
        p = woort_OpcodeDispatcher_decode(p, NULL, NULL);
    }

    if (target == NULL)
        (void)_fail(tag, "no LDIDSTBOX instruction");
    else
    {
        const woort_Bytecode* next;
        const woort_Bytecode bc = target[0];

        if (strncmp(_disassemble(target, &next), "LDIDSTBOXI [SB ", 15) != 0)
            (void)_fail(tag, "disassembly of LDIDSTBOXI: \"%s\"", g_dis_text);

        /* 把 mode 改成 3 */
        woort_CodeEnv_lock(ctx.m_cenv);
        *(woort_Bytecode*)target = woort_OpCodeFormal_cons(
            OP6_M2_A8_B8_C8, WOORT_OPCODE_LDIDSTBOX, 3,
            (uint8_t)WOORT_BYTECODE(A8, bc), (uint8_t)WOORT_BYTECODE(B8, bc), (uint8_t)WOORT_BYTECODE(C8, bc));
        woort_CodeEnv_unlock(ctx.m_cenv);

        if (strncmp(_disassemble(target, &next), "UNKNOWN_OPCODE(17)", 18) != 0 || next != target + 1)
            (void)_fail(tag, "disassembly of mode 3: \"%s\"", g_dis_text);

        /* 解释器：panic Bad command */
        const woort_PanicHandlerFunction prev = woort_set_panic_callback(&_catch_panic);
        g_panic_reason = -1;
        g_panic_message[0] = '\0';

        woort_StackValue sv;
        (void)woort_push_reserve(2, &sv);
        woort_load_const(sv, ctx.m_cenv, c_entry);
        const woort_VmCallStatus st = woort_invoke(sv + 1, sv);
        woort_pop(2);

        (void)woort_set_panic_callback(prev);

        if (st != WOORT_VM_CALL_STATUS_ABORTED
            || g_panic_reason != WOORT_PANIC_BAD_BYTE_CODE
            || strncmp(g_panic_message, "Bad command(", 12) != 0)
        {
            (void)_fail(tag, "status %d, panic reason 0x%x \"%s\"",
                (int)st, (unsigned)g_panic_reason, g_panic_message);
        }
    }

    _report(tag, failures_before, 0, 0);
    _ctx_end(&ctx);
    free(c_field);
}

/* ========== Main ========== */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    woort_init(0, NULL);

    (void)printf("\n=== LDIDSTBOXI/R/B (struct field boxed to a stack slot) ===\n\n");

    run_fields(0);
    run_fields(1);
    run_sp_untouched(0);
    run_sp_untouched(1);
    run_far_slots(0);
    run_far_slots(1);
    run_mode3();

    (void)printf("\n  %d failure(s).\n\n", g_failures);

    woort_shutdown(NULL, NULL);

    if (g_failures == 0)
    {
        (void)printf("test_ir_ldidstbox: ALL PASS\n");
        return 0;
    }
    (void)printf("test_ir_ldidstbox: %d FAILURES\n", g_failures);
    return 1;
}
