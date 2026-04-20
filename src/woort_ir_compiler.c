/*
 * woort_ir_compiler.c
 *
 * IR 编译器：字节码发射引擎。
 * 将 IR 函数（经过分析和栈槽分配后）翻译为最终的字节码序列。
 *
 * 主要流程：
 *   1. 对每个函数调用 _woort_IRFunction_analyze_and_allocate() 进行分析+栈槽分配
 *   2. 对每个函数发射字节码（常量加载 + 指令翻译 + 跳转占位）
 *   3. 跳转修正（前跳/后跳 + 溢出展开）
 *   4. 拼接所有字节码，创建 CodeEnv
 */

#include "woort_ir_compiler.h"
#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_ir_value.h"
#include "woort_ir_op.h"
#include "woort_ir_srcloc.h"
#include "woort_opcode.h"
#include "woort_opcode_builder.h"
#include "woort_opcode_formal.h"
#include "woort_codeenv.h"
#include "woort_value.h"
#include "woort_vector.h"
#include "woort_linklist.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

/* ========================================================================
 * 私有类型
 * ======================================================================== */

/*
 * 常量加载放置信息（与 woort_ir_function.c 中的定义一致）
 */
typedef struct _woort_ConstLoadInfo
{
    woort_IRConstantIndex m_const_index;
    int32_t m_stack_offset; /* 目标栈槽偏移 */
} _woort_ConstLoadInfo;

/*
 * 跳转修正记录
 */
typedef struct _JumpPatch
{
    uint32_t m_block_idx;       /* 跳转指令所在 block 的索引 */
    uint32_t m_bc_idx;          /* 在 block 的 m_bytecodes 中的索引 */
    woort_IRLabel* m_target;    /* 跳转目标 label */
    woort_IROp_Kind m_kind;     /* 原始 IR 跳转类型 */
    int32_t m_src0_off;         /* 条件跳转的操作数偏移 (cond / a) */
    int32_t m_src1_off;         /* 比较跳转的第二操作数偏移 (b) */
} _JumpPatch;

/* ========================================================================
 * 编码范围常量
 * ======================================================================== */

#define WOORT_UINT8_MAX_VAL   ((1u << 8) - 1)
#define WOORT_UINT10_MAX_VAL  ((1u << 10) - 1)
#define WOORT_UINT16_MAX_VAL  ((1u << 16) - 1)
#define WOORT_UINT18_MAX_VAL  ((1u << 18) - 1)
#define WOORT_UINT24_MAX_VAL  ((1u << 24) - 1)
#define WOORT_UINT26_MAX_VAL  ((1u << 26) - 1)

/* ========================================================================
 * 辅助函数：栈偏移转换
 * ======================================================================== */

/*
 * 将虚拟寄存器的栈偏移转为实际偏移。
 * 对于 <= -126 的偏移，额外偏移 3 以避开临时槽 -126/-127/-128。
 */
static int32_t _get_fact_offset(int32_t place)
{
    if (place <= -126)
        return place - 3;
    return place;
}

/* ========================================================================
 * 辅助函数：字节码发射
 * ======================================================================== */

static bool _emit_bc(woort_IRBlock* blk, woort_Bytecode bc)
{
    return woort_vector_push_back(&blk->m_bytecodes, 1, &bc);
}

static bool _emit_bc_ex32(woort_IRBlock* blk, woort_Bytecode bc, uint32_t ex32)
{
    const uint32_t pair[2] = { bc, ex32 };
    return woort_vector_push_back(&blk->m_bytecodes, 2, pair);
}

/* ========================================================================
 * 辅助函数：操作数加载到 S8 临时槽
 * ======================================================================== */

/*
 * 将 vreg 的值加载到 S8 可寻址的位置。
 * 如果 vreg 的实际偏移在 S8 范围内，直接返回该偏移。
 * 否则，发射 MOV 到临时槽（temp_slot），返回临时槽偏移。
 */
static bool _load_to_s8(
    woort_IRBlock* blk,
    const woort_IRValue* v,
    int8_t temp_slot,
    int8_t* out_s8)
{
    assert(temp_slot == -126 || temp_slot == -127 || temp_slot == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact = _get_fact_offset(v->m_assigned_stack_offset);

    if (fact >= INT8_MIN && fact <= INT8_MAX)
    {
        *out_s8 = (int8_t)fact;
        return true;
    }

    /* 需要搬运到临时槽 */
    if (fact >= INT16_MIN && fact <= INT16_MAX)
    {
        if (!_emit_bc(blk, woort_OpCode_MOVLD(temp_slot, (int16_t)fact)))
            return false;
    }
    else
    {
        if (!_emit_bc_ex32(blk,
            woort_OpCode_MOVLDEXT(temp_slot),
            (uint32_t)fact))
            return false;
    }
    *out_s8 = temp_slot;
    return true;
}

/*
 * 将 vreg 的值加载到 S16 可寻址的位置。
 */
static bool _load_to_s16(
    woort_IRBlock* blk,
    const woort_IRValue* v,
    int8_t temp_slot,
    int16_t* out_s16)
{
    assert(temp_slot == -126 || temp_slot == -127 || temp_slot == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact = _get_fact_offset(v->m_assigned_stack_offset);

    if (fact >= INT16_MIN && fact <= INT16_MAX)
    {
        *out_s16 = (int16_t)fact;
        return true;
    }

    if (!_emit_bc_ex32(blk,
        woort_OpCode_MOVLDEXT(temp_slot),
        (uint32_t)fact))
        return false;
    *out_s16 = temp_slot;
    return true;
}

/*
 * 获取用于写入结果的 S8 位置。
 * 如果 vreg 的实际偏移在 S8 范围内，直接返回；否则返回临时槽。
 * 调用方在操作后需要调用 _apply_store 将结果搬回。
 */
static int8_t _get_store_s8(const woort_IRValue* v, int8_t temp_slot)
{
    assert(temp_slot == -126 || temp_slot == -127 || temp_slot == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact = _get_fact_offset(v->m_assigned_stack_offset);
    if (fact >= INT8_MIN && fact <= INT8_MAX)
        return (int8_t)fact;
    return temp_slot;
}

/*
 * 获取用于写入结果的 S16 位置。
 */
static int16_t _get_store_s16(const woort_IRValue* v, int8_t temp_slot)
{
    assert(temp_slot == -126 || temp_slot == -127 || temp_slot == -128);
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact = _get_fact_offset(v->m_assigned_stack_offset);
    if (fact >= INT16_MIN && fact <= INT16_MAX)
        return (int16_t)fact;
    return temp_slot;
}

/*
 * 将临时槽的值搬运回 vreg 的实际栈位置。
 * 如果 storage == vreg 的实际偏移，则不需要搬运。
 */
static bool _apply_store(
    woort_IRBlock* blk,
    const woort_IRValue* v,
    int32_t storage)
{
    assert(v->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN);

    const int32_t fact = _get_fact_offset(v->m_assigned_stack_offset);

    if (fact == storage)
        return true;

    assert(storage == -126 || storage == -127 || storage == -128);

    if (fact >= INT16_MIN && fact <= INT16_MAX)
    {
        return _emit_bc(blk,
            woort_OpCode_MOVST((int8_t)storage, (int16_t)fact));
    }
    else
    {
        return _emit_bc_ex32(blk,
            woort_OpCode_MOVSTEXT((int16_t)storage),
            (uint32_t)fact);
    }
}

/* ========================================================================
 * 辅助函数：常量加载发射
 * ======================================================================== */

static bool _emit_const_load(
    woort_IRBlock* blk,
    int32_t stack_offset,
    uint32_t const_index)
{
    const int32_t fact = _get_fact_offset(stack_offset);

    if ((fact >= INT8_MIN && fact <= INT8_MAX) && const_index <= WOORT_UINT18_MAX_VAL)
    {
        return _emit_bc(blk, woort_OpCode_LOAD(const_index, (int8_t)fact));
    }
    else if (fact >= INT16_MIN && fact <= INT16_MAX)
    {
        return _emit_bc_ex32(blk,
            woort_OpCode_LOADEX((int16_t)fact), const_index);
    }
    else if (const_index <= WOORT_UINT18_MAX_VAL)
    {
        if (!_emit_bc(blk, woort_OpCode_LOAD(const_index, -128)))
            return false;
        return _emit_bc_ex32(blk,
            woort_OpCode_MOVSTEXT(-128), (uint32_t)fact);
    }
    else
    {
        if (!_emit_bc_ex32(blk, woort_OpCode_LOADEX(-128), const_index))
            return false;
        return _emit_bc_ex32(blk,
            woort_OpCode_MOVSTEXT(-128), (uint32_t)fact);
    }
}

/* ========================================================================
 * 辅助函数：静态存储 LOAD/STORE 发射
 * ======================================================================== */

static bool _emit_static_load(
    woort_IRBlock* blk,
    int32_t stack_offset,
    uint32_t storage_place)
{
    return _emit_const_load(blk, stack_offset, storage_place);
}

static bool _emit_static_store(
    woort_IRBlock* blk,
    int32_t stack_offset,
    uint32_t storage_place)
{
    const int32_t fact = _get_fact_offset(stack_offset);

    if ((fact >= INT8_MIN && fact <= INT8_MAX) && storage_place <= WOORT_UINT18_MAX_VAL)
    {
        return _emit_bc(blk, woort_OpCode_STORE(storage_place, (int8_t)fact));
    }
    else if (fact >= INT16_MIN && fact <= INT16_MAX)
    {
        return _emit_bc_ex32(blk,
            woort_OpCode_STOREEX((int16_t)fact), storage_place);
    }
    else if (storage_place <= WOORT_UINT18_MAX_VAL)
    {
        if (!_emit_bc_ex32(blk,
            woort_OpCode_MOVLDEXT(-128), (uint32_t)fact))
            return false;
        return _emit_bc(blk, woort_OpCode_STORE(storage_place, -128));
    }
    else
    {
        if (!_emit_bc_ex32(blk,
            woort_OpCode_MOVLDEXT(-128), (uint32_t)fact))
            return false;
        return _emit_bc_ex32(blk, woort_OpCode_STOREEX(-128), storage_place);
    }
}

/* ========================================================================
 * 辅助函数：类型转换 A8/BC16 双变体发射
 *
 * 将 src -> dst 的类型转换指令发射出来。
 * ST 变体: a8=src(S8), bc16=dst(S16)
 * LD 变体: a8=dst(S8), bc16=src(S16)
 * ======================================================================== */

typedef woort_Bytecode (*_CastSTFunc)(int8_t a8, int16_t bc16);
typedef woort_Bytecode (*_CastLDFunc)(int8_t a8, int16_t bc16);

/*
 * 使用宏包装 opcode builder 宏为普通函数指针是不可行的（宏不是函数），
 * 所以我们直接用内联的方式处理每种类型转换。
 */

#define _EMIT_CAST_BODY(blk, op, c, ST_MACRO, LD_MACRO) \
    do { \
        const int32_t r = _get_fact_offset(op->m_src[0]->m_assigned_stack_offset); \
        const int32_t w = _get_fact_offset(op->m_dst->m_assigned_stack_offset); \
        \
        if (r >= INT8_MIN && r <= INT8_MAX && w >= INT16_MIN && w <= INT16_MAX) \
        { \
            return _emit_bc(blk, ST_MACRO((int8_t)r, (int16_t)w)); \
        } \
        if (w >= INT8_MIN && w <= INT8_MAX && r >= INT16_MIN && r <= INT16_MAX) \
        { \
            return _emit_bc(blk, LD_MACRO((int8_t)w, (int16_t)r)); \
        } \
        if (r >= INT8_MIN && r <= INT8_MAX) \
        { \
            const int16_t w16 = _get_store_s16(op->m_dst, -127); \
            if (!_emit_bc(blk, ST_MACRO((int8_t)r, w16))) \
                return false; \
            return _apply_store(blk, op->m_dst, w16); \
        } \
        if (w >= INT8_MIN && w <= INT8_MAX) \
        { \
            int16_t r16; \
            if (!_load_to_s16(blk, op->m_src[0], -128, &r16)) \
                return false; \
            return _emit_bc(blk, LD_MACRO((int8_t)w, r16)); \
        } \
        if (w >= INT16_MIN && w <= INT16_MAX) \
        { \
            int8_t r8; \
            if (!_load_to_s8(blk, op->m_src[0], -128, &r8)) \
                return false; \
            return _emit_bc(blk, ST_MACRO(r8, (int16_t)w)); \
        } \
        if (r >= INT16_MIN && r <= INT16_MAX) \
        { \
            const int8_t w8 = _get_store_s8(op->m_dst, -127); \
            if (!_emit_bc(blk, LD_MACRO(w8, (int16_t)r))) \
                return false; \
            return _apply_store(blk, op->m_dst, w8); \
        } \
        { \
            int8_t r8; \
            if (!_load_to_s8(blk, op->m_src[0], -128, &r8)) \
                return false; \
            const int16_t w16 = _get_store_s16(op->m_dst, -127); \
            if (!_emit_bc(blk, ST_MACRO(r8, w16))) \
                return false; \
            return _apply_store(blk, op->m_dst, w16); \
        } \
    } while(0)

/* ========================================================================
 * 辅助函数：二元运算发射
 *
 * 可交换运算(ADDI/MULI/ADDR/MULR/LAND/LOR): dst==src0 或 dst==src1 均可使用复合形式
 * 不可交换运算(SUBI/DIVI/MODI/SUBR/DIVR/MODR): 仅 dst==src0 时可使用复合形式
 * ======================================================================== */

#define _EMIT_BINOP_COMMUTATIVE(blk, op, c, OP_MACRO, COP_MACRO) \
    do { \
        (void)(c); \
        const int32_t wf = _get_fact_offset(op->m_dst->m_assigned_stack_offset); \
        if (wf >= INT16_MIN && wf <= INT16_MAX) \
        { \
            if (op->m_dst->m_assigned_stack_offset == op->m_src[0]->m_assigned_stack_offset) \
            { \
                int8_t r; \
                if (!_load_to_s8(blk, op->m_src[1], -128, &r)) \
                    return false; \
                return _emit_bc(blk, COP_MACRO(r, (int16_t)wf)); \
            } \
            else if (op->m_dst->m_assigned_stack_offset == op->m_src[1]->m_assigned_stack_offset) \
            { \
                int8_t r; \
                if (!_load_to_s8(blk, op->m_src[0], -128, &r)) \
                    return false; \
                return _emit_bc(blk, COP_MACRO(r, (int16_t)wf)); \
            } \
        } \
        int8_t r1, r2; \
        if (!_load_to_s8(blk, op->m_src[0], -128, &r1)) \
            return false; \
        if (!_load_to_s8(blk, op->m_src[1], -127, &r2)) \
            return false; \
        const int8_t w = _get_store_s8(op->m_dst, -126); \
        if (!_emit_bc(blk, OP_MACRO(r1, r2, w))) \
            return false; \
        return _apply_store(blk, op->m_dst, w); \
    } while(0)

#define _EMIT_BINOP_NON_COMMUTATIVE(blk, op, c, OP_MACRO, COP_MACRO) \
    do { \
        (void)(c); \
        const int32_t wf = _get_fact_offset(op->m_dst->m_assigned_stack_offset); \
        if (wf >= INT16_MIN && wf <= INT16_MAX) \
        { \
            if (op->m_dst->m_assigned_stack_offset == op->m_src[0]->m_assigned_stack_offset) \
            { \
                int8_t r; \
                if (!_load_to_s8(blk, op->m_src[1], -128, &r)) \
                    return false; \
                return _emit_bc(blk, COP_MACRO(r, (int16_t)wf)); \
            } \
        } \
        int8_t r1, r2; \
        if (!_load_to_s8(blk, op->m_src[0], -128, &r1)) \
            return false; \
        if (!_load_to_s8(blk, op->m_src[1], -127, &r2)) \
            return false; \
        const int8_t w = _get_store_s8(op->m_dst, -126); \
        if (!_emit_bc(blk, OP_MACRO(r1, r2, w))) \
            return false; \
        return _apply_store(blk, op->m_dst, w); \
    } while(0)

/* 纯三地址二元比较运算（无复合形式） */
#define _EMIT_BINOP_CMP(blk, op, c, OP_MACRO) \
    do { \
        (void)(c); \
        int8_t r1, r2; \
        if (!_load_to_s8(blk, op->m_src[0], -128, &r1)) \
            return false; \
        if (!_load_to_s8(blk, op->m_src[1], -127, &r2)) \
            return false; \
        const int8_t w = _get_store_s8(op->m_dst, -126); \
        if (!_emit_bc(blk, OP_MACRO(r1, r2, w))) \
            return false; \
        return _apply_store(blk, op->m_dst, w); \
    } while(0)

/* 三地址三源无写操作数（索引存储等） */
#define _EMIT_STORE_IDX_3SRC(blk, op, c, OP_MACRO) \
    do { \
        (void)(c); \
        int8_t r1, r2, r3; \
        if (!_load_to_s8(blk, op->m_src[0], -128, &r1)) \
            return false; \
        if (!_load_to_s8(blk, op->m_src[1], -127, &r2)) \
            return false; \
        if (!_load_to_s8(blk, op->m_src[2], -126, &r3)) \
            return false; \
        return _emit_bc(blk, OP_MACRO(r1, r2, r3)); \
    } while(0)

/* ========================================================================
 * 指令分类
 * ======================================================================== */

static bool _is_jump_op(woort_IROp_Kind kind)
{
    switch (kind)
    {
    case WOORT_IROP_KIND_JMP:
    case WOORT_IROP_KIND_JCC:
    case WOORT_IROP_KIND_JCCZ:
    case WOORT_IROP_KIND_JCC_LT:
    case WOORT_IROP_KIND_JCC_LE:
    case WOORT_IROP_KIND_JCC_EQ:
    case WOORT_IROP_KIND_JCC_GT:
    case WOORT_IROP_KIND_JCC_GE:
    case WOORT_IROP_KIND_JCC_NE:
    case WOORT_IROP_KIND_JIFINITED:
        return true;
    default:
        return false;
    }
}

/* ========================================================================
 * 单条 IR 指令的字节码发射
 * ======================================================================== */

static bool _emit_op(
    woort_IRBlock* blk,
    woort_IROp* op,
    woort_IRCompiler* c,
    woort_Vector* jump_patches,
    uint32_t block_idx)
{
    switch (op->m_op)
    {
    /* ============ LABEL & EMPTY: 跳过 ============ */
    case WOORT_IROP_KIND_LABEL:
    case WOORT_IROP_KIND_EMPTY:
        return true;

    /* ============ NOP ============ */
    case WOORT_IROP_KIND_NOP:
    {
        (void)c;
        return _emit_bc(blk, woort_OpCode_NOP());
    }

    /* ============ MOV ============ */
    case WOORT_IROP_KIND_MOV:
    {
        assert(op->m_dst != NULL && op->m_src[0] != NULL);
        if (op->m_src[0]->m_is_const_direct)
        {
            const uint32_t cidx = op->m_src[0]->m_const_idx;
            return _emit_const_load(blk, op->m_dst->m_assigned_stack_offset, cidx);
        }

        const int32_t src_f = _get_fact_offset(op->m_src[0]->m_assigned_stack_offset);
        const int32_t dst_f = _get_fact_offset(op->m_dst->m_assigned_stack_offset);

        if (src_f == dst_f)
            return true; /* 同一位置，不需要搬运 */

        /* MOVLD: [SB + a8] = [SB + bc16]  (a8=dst, bc16=src) */
        if (dst_f >= INT8_MIN && dst_f <= INT8_MAX && src_f >= INT16_MIN && src_f <= INT16_MAX)
        {
            return _emit_bc(blk, woort_OpCode_MOVLD((int8_t)dst_f, (int16_t)src_f));
        }
        /* MOVST: [SB + bc16] = [SB + a8]  (a8=src, bc16=dst) */
        if (src_f >= INT8_MIN && src_f <= INT8_MAX && dst_f >= INT16_MIN && dst_f <= INT16_MAX)
        {
            return _emit_bc(blk, woort_OpCode_MOVST((int8_t)src_f, (int16_t)dst_f));
        }
        /* 两者都不在范围内，通过临时槽中转 */
        if (src_f >= INT16_MIN && src_f <= INT16_MAX)
        {
            if (!_emit_bc(blk, woort_OpCode_MOVLD(-128, (int16_t)src_f)))
                return false;
            if (dst_f >= INT16_MIN && dst_f <= INT16_MAX)
                return _emit_bc(blk, woort_OpCode_MOVST(-128, (int16_t)dst_f));
            return _emit_bc_ex32(blk, woort_OpCode_MOVSTEXT(-128), (uint32_t)dst_f);
        }
        /* src 超出 S16 */
        if (!_emit_bc_ex32(blk, woort_OpCode_MOVLDEXT(-128), (uint32_t)src_f))
            return false;
        if (dst_f >= INT16_MIN && dst_f <= INT16_MAX)
            return _emit_bc(blk, woort_OpCode_MOVST(-128, (int16_t)dst_f));
        return _emit_bc_ex32(blk, woort_OpCode_MOVSTEXT(-128), (uint32_t)dst_f);
    }

    /* ============ LOAD (静态存储) ============ */
    case WOORT_IROP_KIND_LOAD:
    {
        assert(op->m_dst != NULL);
        const uint32_t storage = op->m_static_index + c->m_constant_alloc_count;
        const int32_t w = op->m_dst->m_assigned_stack_offset;
        return _emit_static_load(blk, w, storage);
    }

    /* ============ STORE (静态存储) ============ */
    case WOORT_IROP_KIND_STORE:
    {
        assert(op->m_src[0] != NULL);
        const uint32_t storage = op->m_static_index + c->m_constant_alloc_count;
        const int32_t r = op->m_src[0]->m_assigned_stack_offset;
        return _emit_static_store(blk, r, storage);
    }

    /* ============ PUSHCHK ============ */
    case WOORT_IROP_KIND_PUSHCHK:
    {
        assert(op->m_src[0] != NULL);

        /* 常量直连优化：直接发 PUSHCCHK，跳过 LOAD + PUSHSCHK */
        if (op->m_src[0]->m_is_const_direct)
        {
            uint32_t cidx = op->m_src[0]->m_const_idx;
            if (cidx <= WOORT_UINT24_MAX_VAL)
                return _emit_bc(blk, woort_OpCode_PUSHCCHK(cidx));
            return _emit_bc_ex32(blk, woort_OpCode_PUSHCCHKEXT(), cidx);
        }

        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc(blk, woort_OpCode_PUSHSCHK(r));
    }

    /* ============ PUSHSTATICCHK ============ */
    case WOORT_IROP_KIND_PUSHSTATICCHK:
    {
        const uint32_t storage = op->m_static_index + c->m_constant_alloc_count;
        if (storage <= WOORT_UINT24_MAX_VAL)
            return _emit_bc(blk, woort_OpCode_PUSHCCHK(storage));
        return _emit_bc_ex32(blk, woort_OpCode_PUSHCCHKEXT(), storage);
    }

    /* ============ POP ============ */
    case WOORT_IROP_KIND_POP:
    {
        assert(op->m_dst != NULL);
        const int16_t w = _get_store_s16(op->m_dst, -128);
        if (!_emit_bc(blk, woort_OpCode_POPS(w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    /* ============ POPR ============ */
    case WOORT_IROP_KIND_POPR:
    {
        assert(op->m_pop_count <= WOORT_UINT24_MAX_VAL);
        return _emit_bc(blk, woort_OpCode_POPR(op->m_pop_count));
    }

    /* ============ POPRS ============ */
    case WOORT_IROP_KIND_POPRS:
    {
        assert(op->m_src[0] != NULL);
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc(blk, woort_OpCode_POPRS(r));
    }

    /* ============ 类型转换 ============ */
    case WOORT_IROP_KIND_ITOR:
        _EMIT_CAST_BODY(blk, op, c, woort_OpCode_ITORST, woort_OpCode_ITORLD);

    case WOORT_IROP_KIND_ITOS:
        _EMIT_CAST_BODY(blk, op, c, woort_OpCode_ITOSST, woort_OpCode_ITOSLD);

    case WOORT_IROP_KIND_RTOI:
        _EMIT_CAST_BODY(blk, op, c, woort_OpCode_RTOIST, woort_OpCode_RTOILD);

    case WOORT_IROP_KIND_RTOS:
        _EMIT_CAST_BODY(blk, op, c, woort_OpCode_RTOSST, woort_OpCode_RTOSLD);

    /* ============ 函数调用 ============ */
    case WOORT_IROP_KIND_CALLNWO:
    {
        (void)c;
        const uint32_t target = op->m_calln_target;
        assert(target <= WOORT_UINT26_MAX_VAL);
        if (!_emit_bc(blk, woort_OpCode_CALLNWO(target)))
            return false;
        goto _handle_call_result;
    }
    case WOORT_IROP_KIND_CALLNFP:
    {
        (void)c;
        const uint32_t target = op->m_calln_target;
        assert(target <= WOORT_UINT26_MAX_VAL);
        if (!_emit_bc(blk, woort_OpCode_CALLNFP(target)))
            return false;
        goto _handle_call_result;
    }
    case WOORT_IROP_KIND_CALLNJIT:
    {
        (void)c;
        const uint32_t target = op->m_calln_target;
        assert(target <= WOORT_UINT26_MAX_VAL);
        if (!_emit_bc(blk, woort_OpCode_CALLNJIT(target)))
            return false;
        goto _handle_call_result;
    }

    _handle_call_result:
    {
        if (op->m_dst != NULL)
        {
            const int16_t w16 = _get_store_s16(op->m_dst, -128);
            if (op->m_argument_count <= WOORT_UINT10_MAX_VAL)
            {
                if (!_emit_bc(blk, woort_OpCode_RESULT(op->m_argument_count, w16)))
                    return false;
            }
            else
            {
                assert(op->m_argument_count <= WOORT_UINT24_MAX_VAL);
                if (!_emit_bc(blk, woort_OpCode_RESULT(0, w16)))
                    return false;
                if (!_emit_bc(blk, woort_OpCode_POPR(op->m_argument_count)))
                    return false;
            }
            return _apply_store(blk, op->m_dst, w16);
        }
        else
        {
            assert(op->m_argument_count <= WOORT_UINT24_MAX_VAL);
            return op->m_argument_count == 0 ||
                _emit_bc(blk, woort_OpCode_POPR(op->m_argument_count));
        }
    }

    /* ============ CALL (间接调用) ============ */
    case WOORT_IROP_KIND_CALL:
    {
        (void)c;
        assert(op->m_src[0] != NULL);
        int16_t f16;
        if (!_load_to_s16(blk, op->m_src[0], -128, &f16))
            return false;
        if (!_emit_bc(blk, woort_OpCode_CALLS(f16)))
            return false;

        if (op->m_dst != NULL)
        {
            const int16_t w16 = _get_store_s16(op->m_dst, -128);
            if (op->m_call_argument_count <= WOORT_UINT10_MAX_VAL)
            {
                if (!_emit_bc(blk, woort_OpCode_RESULT(op->m_call_argument_count, w16)))
                    return false;
            }
            else
            {
                assert(op->m_call_argument_count <= WOORT_UINT24_MAX_VAL);
                if (!_emit_bc(blk, woort_OpCode_RESULT(0, w16)))
                    return false;
                if (!_emit_bc(blk, woort_OpCode_POPR(op->m_call_argument_count)))
                    return false;
            }
            return _apply_store(blk, op->m_dst, w16);
        }
        else
        {
            assert(op->m_call_argument_count <= WOORT_UINT24_MAX_VAL);
            return op->m_call_argument_count == 0 ||
                _emit_bc(blk, woort_OpCode_POPR(op->m_call_argument_count));
        }
    }

    /* ============ MKCLOSURE ============ */
    case WOORT_IROP_KIND_MKCLOSURE:
    {
        (void)c;
        assert(op->m_dst != NULL);
        assert(op->m_argument_count <= WOORT_UINT10_MAX_VAL);

        const int16_t w16 = _get_store_s16(op->m_dst, -128);
        const uint32_t target = op->m_calln_target;

        if (!_emit_bc_ex32(blk,
            woort_OpCode_MKCLOSURE(op->m_argument_count, w16), target))
            return false;
        return _apply_store(blk, op->m_dst, w16);
    }

    /* ============ MKVEC ============ */
    case WOORT_IROP_KIND_MKVEC:
    {
        (void)c;
        assert(op->m_dst != NULL);
        const int16_t w16 = _get_store_s16(op->m_dst, -128);
        if (op->m_count <= WOORT_UINT8_MAX_VAL)
        {
            if (!_emit_bc(blk, woort_OpCode_MKVEC(op->m_count, w16)))
                return false;
        }
        else
        {
            if (!_emit_bc_ex32(blk, woort_OpCode_MKVECEXT(w16), op->m_count))
                return false;
        }
        return _apply_store(blk, op->m_dst, w16);
    }

    /* ============ MKMAP ============ */
    case WOORT_IROP_KIND_MKMAP:
    {
        (void)c;
        assert(op->m_dst != NULL);
        const int16_t w16 = _get_store_s16(op->m_dst, -128);
        if (op->m_count <= WOORT_UINT8_MAX_VAL)
        {
            if (!_emit_bc(blk, woort_OpCode_MKMAP(op->m_count, w16)))
                return false;
        }
        else
        {
            if (!_emit_bc_ex32(blk, woort_OpCode_MKMAPEXT(w16), op->m_count))
                return false;
        }
        return _apply_store(blk, op->m_dst, w16);
    }

    /* ============ MKSTRUCT ============ */
    case WOORT_IROP_KIND_MKSTRUCT:
    {
        (void)c;
        assert(op->m_dst != NULL);
        const int16_t w16 = _get_store_s16(op->m_dst, -128);
        if (op->m_count <= WOORT_UINT8_MAX_VAL)
        {
            if (!_emit_bc(blk, woort_OpCode_MKSTRUCT(op->m_count, w16)))
                return false;
        }
        else
        {
            if (!_emit_bc_ex32(blk, woort_OpCode_MKSTRUCTEXT(w16), op->m_count))
                return false;
        }
        return _apply_store(blk, op->m_dst, w16);
    }

    /* ============ 动态类型 ============ */
    case WOORT_IROP_KIND_BOXDYN:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_BOXDYN(op->m_type, r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }
    case WOORT_IROP_KIND_UNBOXDYN:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_UNBOXDYN(op->m_type, r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }
    case WOORT_IROP_KIND_CHECKDYN:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_CHECKDYN(op->m_type, r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }
    case WOORT_IROP_KIND_PUSHBOXDYN:
    {
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc(blk, woort_OpCode_PUSHBOXDYN(op->m_type, r));
    }

    /* ============ 字符串/BOXED 转换 ============ */
    case WOORT_IROP_KIND_CASTSTO:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_CASTSTO(op->m_type, r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }
    case WOORT_IROP_KIND_CASTSFROM:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_CASTSFROM(op->m_type, r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }
    case WOORT_IROP_KIND_CASTDYN:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_CASTDYN(op->m_type, r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }
    case WOORT_IROP_KIND_ASSERTDYN:
    {
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc(blk, woort_OpCode_ASSERTDYN(op->m_type, r));
    }

    /* ============ 整数算术 ============ */
    case WOORT_IROP_KIND_ADDI:
        _EMIT_BINOP_COMMUTATIVE(blk, op, c, woort_OpCode_ADDI, woort_OpCode_CADDI);

    case WOORT_IROP_KIND_SUBI:
        _EMIT_BINOP_NON_COMMUTATIVE(blk, op, c, woort_OpCode_SUBI, woort_OpCode_CSUBI);

    case WOORT_IROP_KIND_MULI:
        _EMIT_BINOP_COMMUTATIVE(blk, op, c, woort_OpCode_MULI, woort_OpCode_CMULI);

    case WOORT_IROP_KIND_DIVI:
        _EMIT_BINOP_NON_COMMUTATIVE(blk, op, c, woort_OpCode_DIVI, woort_OpCode_CDIVI);

    case WOORT_IROP_KIND_MODI:
        _EMIT_BINOP_NON_COMMUTATIVE(blk, op, c, woort_OpCode_MODI, woort_OpCode_CMODI);

    case WOORT_IROP_KIND_NEGI:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int16_t w = _get_store_s16(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_NEGI(r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    /* ============ 整数比较 ============ */
    case WOORT_IROP_KIND_LTI:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LTI);

    case WOORT_IROP_KIND_GTI:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_GTI);

    case WOORT_IROP_KIND_LEI:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LEI);

    case WOORT_IROP_KIND_GEI:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_GEI);

    case WOORT_IROP_KIND_EQI:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_EQI);

    case WOORT_IROP_KIND_NEI:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_NEI);

    /* ============ 实数算术 ============ */
    case WOORT_IROP_KIND_ADDR:
        _EMIT_BINOP_COMMUTATIVE(blk, op, c, woort_OpCode_ADDR, woort_OpCode_CADDR);

    case WOORT_IROP_KIND_SUBR:
        _EMIT_BINOP_NON_COMMUTATIVE(blk, op, c, woort_OpCode_SUBR, woort_OpCode_CSUBR);

    case WOORT_IROP_KIND_MULR:
        _EMIT_BINOP_COMMUTATIVE(blk, op, c, woort_OpCode_MULR, woort_OpCode_CMULR);

    case WOORT_IROP_KIND_DIVR:
        _EMIT_BINOP_NON_COMMUTATIVE(blk, op, c, woort_OpCode_DIVR, woort_OpCode_CDIVR);

    case WOORT_IROP_KIND_MODR:
        _EMIT_BINOP_NON_COMMUTATIVE(blk, op, c, woort_OpCode_MODR, woort_OpCode_CMODR);

    case WOORT_IROP_KIND_NEGR:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int16_t w = _get_store_s16(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_NEGR(r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    /* ============ 实数比较 ============ */
    case WOORT_IROP_KIND_LTR:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LTR);
    case WOORT_IROP_KIND_GTR:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_GTR);
    case WOORT_IROP_KIND_LER:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LER);
    case WOORT_IROP_KIND_GER:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_GER);
    case WOORT_IROP_KIND_EQR:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_EQR);
    case WOORT_IROP_KIND_NER:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_NER);

    /* ============ 字符串 ============ */
    case WOORT_IROP_KIND_ADDS:
    {
        /*
         * ADDS: 字符串连接  dst = src[0] + src[1]
         * CADDS(a8, bc16):  [bc16] = concat([bc16], [a8])  bc16为前缀
         *   可用条件: dst == src[0]
         * CVADDS(a8, bc16): [bc16] = concat([a8], [bc16])  a8为前缀
         *   可用条件: dst == src[1]
         */
        (void)c;
        const int32_t wf = _get_fact_offset(op->m_dst->m_assigned_stack_offset);
        if (wf >= INT16_MIN && wf <= INT16_MAX)
        {
            if (op->m_dst->m_assigned_stack_offset == op->m_src[0]->m_assigned_stack_offset)
            {
                int8_t r;
                if (!_load_to_s8(blk, op->m_src[1], -128, &r))
                    return false;
                return _emit_bc(blk, woort_OpCode_CADDS(r, (int16_t)wf));
            }
            else if (op->m_dst->m_assigned_stack_offset == op->m_src[1]->m_assigned_stack_offset)
            {
                int8_t r;
                if (!_load_to_s8(blk, op->m_src[0], -128, &r))
                    return false;
                return _emit_bc(blk, woort_OpCode_CVADDS(r, (int16_t)wf));
            }
        }
        int8_t r1, r2;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r1))
            return false;
        if (!_load_to_s8(blk, op->m_src[1], -127, &r2))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -126);
        if (!_emit_bc(blk, woort_OpCode_ADDS(r1, r2, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    case WOORT_IROP_KIND_LTS:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LTS);
    case WOORT_IROP_KIND_GTS:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_GTS);
    case WOORT_IROP_KIND_LES:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LES);
    case WOORT_IROP_KIND_GES:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_GES);
    case WOORT_IROP_KIND_EQS:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_EQS);
    case WOORT_IROP_KIND_NES:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_NES);

    /* ============ 逻辑运算 ============ */
    case WOORT_IROP_KIND_LAND:
        _EMIT_BINOP_COMMUTATIVE(blk, op, c, woort_OpCode_LAND, woort_OpCode_CLAND);

    case WOORT_IROP_KIND_LOR:
        _EMIT_BINOP_COMMUTATIVE(blk, op, c, woort_OpCode_LOR, woort_OpCode_CLOR);

    case WOORT_IROP_KIND_LNOT:
    {
        (void)c;
        const int32_t wf = _get_fact_offset(op->m_dst->m_assigned_stack_offset);
        if (wf >= INT16_MIN && wf <= INT16_MAX &&
            op->m_dst->m_assigned_stack_offset == op->m_src[0]->m_assigned_stack_offset)
        {
            return _emit_bc(blk, woort_OpCode_CLNOT((int16_t)wf));
        }
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int16_t w = _get_store_s16(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_LNOT(r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    /* ============ 索引加载 ============ */
    case WOORT_IROP_KIND_LDIDXVEC:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LDIDXVEC);

    case WOORT_IROP_KIND_LDIDXVECX:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LDIDXVECX);

    case WOORT_IROP_KIND_LDIDXSTRUCT:
    {
        /* LDIDSTRUCT n8, b8, c8: struct=[SB+b8], field=n8 -> [SB+c8] */
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int8_t w = _get_store_s8(op->m_dst, -127);
        assert(op->m_index <= WOORT_UINT8_MAX_VAL);
        if (!_emit_bc(blk, woort_OpCode_LDIDSTRUCT((uint8_t)op->m_index, r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    case WOORT_IROP_KIND_LDIDXSTRING:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LDIDSTRING);

    case WOORT_IROP_KIND_LDIDXDICTI:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LDIDXDICTI);
    case WOORT_IROP_KIND_LDIDXDICTR:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LDIDXDICTR);
    case WOORT_IROP_KIND_LDIDXDICTB:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LDIDXDICTB);
    case WOORT_IROP_KIND_LDIDXDICTX:
        _EMIT_BINOP_CMP(blk, op, c, woort_OpCode_LDIDXDICTX);

    /* ============ 索引存储 - vec ============ */
    case WOORT_IROP_KIND_SDIDXVECI:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXVEC_I);
    case WOORT_IROP_KIND_SDIDXVECR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXVEC_R);
    case WOORT_IROP_KIND_SDIDXVECB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXVEC_B);
    case WOORT_IROP_KIND_SDIDXVECX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXVEC_X);

    /* ============ 索引存储 - dict (int key) ============ */
    case WOORT_IROP_KIND_SDIDXDICTII:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTII);
    case WOORT_IROP_KIND_SDIDXDICTIR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTIR);
    case WOORT_IROP_KIND_SDIDXDICTIB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTIB);
    case WOORT_IROP_KIND_SDIDXDICTIX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTIX);

    /* ============ 索引存储 - dict (real key) ============ */
    case WOORT_IROP_KIND_SDIDXDICTRI:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTRI);
    case WOORT_IROP_KIND_SDIDXDICTRR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTRR);
    case WOORT_IROP_KIND_SDIDXDICTRB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTRB);
    case WOORT_IROP_KIND_SDIDXDICTRX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTRX);

    /* ============ 索引存储 - dict (bool key) ============ */
    case WOORT_IROP_KIND_SDIDXDICTBI:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTBI);
    case WOORT_IROP_KIND_SDIDXDICTBR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTBR);
    case WOORT_IROP_KIND_SDIDXDICTBB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTBB);
    case WOORT_IROP_KIND_SDIDXDICTBX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTBX);

    /* ============ 索引存储 - dict (dynamic key) ============ */
    case WOORT_IROP_KIND_SDIDXDICTXI:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTXI);
    case WOORT_IROP_KIND_SDIDXDICTXR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTXR);
    case WOORT_IROP_KIND_SDIDXDICTXB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTXB);
    case WOORT_IROP_KIND_SDIDXDICTXX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXDICTXX);

    /* ============ 索引存储 - map (int key) ============ */
    case WOORT_IROP_KIND_SDIDXMAPII:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPII);
    case WOORT_IROP_KIND_SDIDXMAPIR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPIR);
    case WOORT_IROP_KIND_SDIDXMAPIB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPIB);
    case WOORT_IROP_KIND_SDIDXMAPIX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPIX);

    /* ============ 索引存储 - map (real key) ============ */
    case WOORT_IROP_KIND_SDIDXMAPRI:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPRI);
    case WOORT_IROP_KIND_SDIDXMAPRR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPRR);
    case WOORT_IROP_KIND_SDIDXMAPRB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPRB);
    case WOORT_IROP_KIND_SDIDXMAPRX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPRX);

    /* ============ 索引存储 - map (bool key) ============ */
    case WOORT_IROP_KIND_SDIDXMAPBI:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPBI);
    case WOORT_IROP_KIND_SDIDXMAPBR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPBR);
    case WOORT_IROP_KIND_SDIDXMAPBB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPBB);
    case WOORT_IROP_KIND_SDIDXMAPBX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPBX);

    /* ============ 索引存储 - map (dynamic key) ============ */
    case WOORT_IROP_KIND_SDIDXMAPXI:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPXI);
    case WOORT_IROP_KIND_SDIDXMAPXR:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPXR);
    case WOORT_IROP_KIND_SDIDXMAPXB:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPXB);
    case WOORT_IROP_KIND_SDIDXMAPXX:
        _EMIT_STORE_IDX_3SRC(blk, op, c, woort_OpCode_STIDXMAPXX);

    /* ============ 索引存储 - struct ============ */
    case WOORT_IROP_KIND_SDIDXSTRUCT:
    {
        /* STIDSTRUCT n10, a8, b8: struct=[SB+a8], val=[SB+b8], field=n10 */
        (void)c;
        int8_t r1, r2;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r1))
            return false;
        if (!_load_to_s8(blk, op->m_src[1], -127, &r2))
            return false;
        assert(op->m_index <= WOORT_UINT10_MAX_VAL);
        return _emit_bc(blk, woort_OpCode_STIDSTRUCT((uint16_t)op->m_index, r1, r2));
    }

    /* ============ 解包 ============ */
    case WOORT_IROP_KIND_UNPACKSTRUCT:
    {
        /* UNPACKSTRUCT bc16: [SB + bc16] */
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc(blk, woort_OpCode_UNPACKSTRUCT(r));
    }

    case WOORT_IROP_KIND_UNPACKVEC:
    {
        /* UNPACKVEC a8, bc16: src=[SB+a8], 展开数量写入 [SB+bc16] (dst) */
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int16_t w = _get_store_s16(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_UNPACKVEC(r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    case WOORT_IROP_KIND_UNPACKVECX:
    {
        (void)c;
        int8_t r;
        if (!_load_to_s8(blk, op->m_src[0], -128, &r))
            return false;
        const int16_t w = _get_store_s16(op->m_dst, -127);
        if (!_emit_bc(blk, woort_OpCode_UNPACKVECX(r, w)))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    /* ============ 结构体字段推栈 ============ */
    case WOORT_IROP_KIND_PUSHIDXSTRUCT:
    {
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        assert(op->m_index <= WOORT_UINT8_MAX_VAL);
        return _emit_bc(blk, woort_OpCode_PUSHIDXSTRUCT((uint8_t)op->m_index, r));
    }

    case WOORT_IROP_KIND_PUSHIDXSTBOXI:
    {
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        assert(op->m_index <= WOORT_UINT8_MAX_VAL);
        return _emit_bc(blk, woort_OpCode_PUSHIDXSTBOXI((uint8_t)op->m_index, r));
    }
    case WOORT_IROP_KIND_PUSHIDXSTBOXR:
    {
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        assert(op->m_index <= WOORT_UINT8_MAX_VAL);
        return _emit_bc(blk, woort_OpCode_PUSHIDXSTBOXR((uint8_t)op->m_index, r));
    }
    case WOORT_IROP_KIND_PUSHIDXSTBOXB:
    {
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        assert(op->m_index <= WOORT_UINT8_MAX_VAL);
        return _emit_bc(blk, woort_OpCode_PUSHIDXSTBOXB((uint8_t)op->m_index, r));
    }
    case WOORT_IROP_KIND_PUSHIDXSTBOXX:
    {
        (void)c;
        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        assert(op->m_index <= WOORT_UINT8_MAX_VAL);
        return _emit_bc(blk, woort_OpCode_PUSHIDXSTBOXX((uint8_t)op->m_index, r));
    }

    /* ============ 返回 ============ */
    case WOORT_IROP_KIND_RET:
    {
        (void)c;
        assert(op->m_src[0] != NULL);

        int16_t r;

        /* 常量直连优化：直接发 RETVC，跳过 LOAD + RETVS */
        if (op->m_src[0]->m_is_const_direct)
        {
            uint32_t cidx = op->m_src[0]->m_const_idx;
            if (cidx <= WOORT_UINT24_MAX_VAL)
                return _emit_bc(blk, woort_OpCode_RETVC(cidx));

            /* 超出 24-bit 范围，回退到 RETVS */
            if (!_emit_bc_ex32(blk, woort_OpCode_LOADEX(-128), cidx))
                return false;

            r = -128;
        }
        else if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc(blk, woort_OpCode_RETVS(r));
    }

    case WOORT_IROP_KIND_RET_VOID:
    {
        (void)c;
        return _emit_bc(blk, woort_OpCode_RET());
    }

    /* ============ 原子操作 ============ */
    case WOORT_IROP_KIND_ASTORE:
    {
        assert(op->m_src[0] != NULL);
        const uint32_t storage = op->m_static_index + c->m_constant_alloc_count;

        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc_ex32(blk, woort_OpCode_ASTORE(r), storage);
    }

    case WOORT_IROP_KIND_ALOAD:
    {
        assert(op->m_dst != NULL);
        const uint32_t storage = op->m_static_index + c->m_constant_alloc_count;

        const int16_t w = _get_store_s16(op->m_dst, -128);
        if (!_emit_bc_ex32(blk, woort_OpCode_ALOAD(w), storage))
            return false;
        return _apply_store(blk, op->m_dst, w);
    }

    case WOORT_IROP_KIND_CAS:
    {
        assert(op->m_src[0] != NULL && op->m_dst != NULL);
        const uint32_t storage = op->m_static_index + c->m_constant_alloc_count;

        /*
         * VM: desired = SB[A8] (read), expected = SB[BC16] (read+write)
         * IR: m_src[0] = expected, m_dst = desired
         */
        int8_t desired;
        if (!_load_to_s8(blk, op->m_src[0], -128, &desired))
            return false;
        int16_t expected;
        if (!_load_to_s16(blk, op->m_dst, -127, &expected))
            return false;
        if (!_emit_bc_ex32(blk, woort_OpCode_CAS(desired, expected), storage))
            return false;
        return _apply_store(blk, op->m_dst, expected);
    }

    /* ============ 跳转指令：发射占位符 + 记录 patch ============ */
    case WOORT_IROP_KIND_JMP:
    {
        _JumpPatch patch;
        patch.m_block_idx = block_idx;
        patch.m_bc_idx = (uint32_t)blk->m_bytecodes.m_size;
        patch.m_target = op->m_jump_target;
        patch.m_kind = WOORT_IROP_KIND_JMP;
        patch.m_src0_off = 0;
        patch.m_src1_off = 0;

        if (!_emit_bc(blk, woort_OpCode_JFWD(0)))
            return false;
        return woort_vector_push_back(jump_patches, 1, &patch);
    }

    case WOORT_IROP_KIND_JCC:
    {
        assert(op->m_src[0] != NULL);
        int8_t cond;
        if (!_load_to_s8(blk, op->m_src[0], -126, &cond))
            return false;

        _JumpPatch patch;
        patch.m_block_idx = block_idx;
        patch.m_bc_idx = (uint32_t)blk->m_bytecodes.m_size;
        patch.m_target = op->m_jump_target;
        patch.m_kind = WOORT_IROP_KIND_JCC;
        patch.m_src0_off = cond;
        patch.m_src1_off = 0;

        if (!_emit_bc(blk, woort_OpCode_JFWDNZ(cond, 0)))
            return false;
        return woort_vector_push_back(jump_patches, 1, &patch);
    }

    case WOORT_IROP_KIND_JCCZ:
    {
        assert(op->m_src[0] != NULL);
        int8_t cond;
        if (!_load_to_s8(blk, op->m_src[0], -126, &cond))
            return false;

        _JumpPatch patch;
        patch.m_block_idx = block_idx;
        patch.m_bc_idx = (uint32_t)blk->m_bytecodes.m_size;
        patch.m_target = op->m_jump_target;
        patch.m_kind = WOORT_IROP_KIND_JCCZ;
        patch.m_src0_off = cond;
        patch.m_src1_off = 0;

        if (!_emit_bc(blk, woort_OpCode_JFWDZ(cond, 0)))
            return false;
        return woort_vector_push_back(jump_patches, 1, &patch);
    }

    case WOORT_IROP_KIND_JCC_LT:
    case WOORT_IROP_KIND_JCC_LE:
    case WOORT_IROP_KIND_JCC_EQ:
    case WOORT_IROP_KIND_JCC_GT:
    case WOORT_IROP_KIND_JCC_GE:
    case WOORT_IROP_KIND_JCC_NE:
    {
        assert(op->m_src[0] != NULL && op->m_src[1] != NULL);

        int8_t a, b_val;
        woort_IROp_Kind emit_kind = op->m_op;

        /*
         * GT 和 GE 通过交换操作数转换为 LT 和 LE
         */
        if (op->m_op == WOORT_IROP_KIND_JCC_GT)
        {
            if (!_load_to_s8(blk, op->m_src[1], -126, &a))
                return false;
            if (!_load_to_s8(blk, op->m_src[0], -127, &b_val))
                return false;
            emit_kind = WOORT_IROP_KIND_JCC_LT;
        }
        else if (op->m_op == WOORT_IROP_KIND_JCC_GE)
        {
            if (!_load_to_s8(blk, op->m_src[1], -126, &a))
                return false;
            if (!_load_to_s8(blk, op->m_src[0], -127, &b_val))
                return false;
            emit_kind = WOORT_IROP_KIND_JCC_LE;
        }
        else
        {
            if (!_load_to_s8(blk, op->m_src[0], -126, &a))
                return false;
            if (!_load_to_s8(blk, op->m_src[1], -127, &b_val))
                return false;
        }

        _JumpPatch patch;
        patch.m_block_idx = block_idx;
        patch.m_bc_idx = (uint32_t)blk->m_bytecodes.m_size;
        patch.m_target = op->m_jump_target;
        patch.m_kind = emit_kind;
        patch.m_src0_off = a;
        patch.m_src1_off = b_val;

        woort_Bytecode placeholder;
        switch (emit_kind)
        {
        case WOORT_IROP_KIND_JCC_LT:
            placeholder = woort_OpCode_JFWDLT(a, b_val, 0);
            break;
        case WOORT_IROP_KIND_JCC_LE:
            placeholder = woort_OpCode_JFWDEL(a, b_val, 0);
            break;
        case WOORT_IROP_KIND_JCC_EQ:
            placeholder = woort_OpCode_JFWDEQ(a, b_val, 0);
            break;
        case WOORT_IROP_KIND_JCC_NE:
            placeholder = woort_OpCode_JFWDNEQ(a, b_val, 0);
            break;
        default:
            assert(false && "Unreachable");
            placeholder = 0;
            break;
        }

        if (!_emit_bc(blk, placeholder))
            return false;
        return woort_vector_push_back(jump_patches, 1, &patch);
    }

    /* ============ JIFINITED (一次性初始化守卫) ============ */
    case WOORT_IROP_KIND_JIFINITED:
    {
        _JumpPatch patch;
        patch.m_block_idx = block_idx;
        patch.m_bc_idx = (uint32_t)blk->m_bytecodes.m_size;
        patch.m_target = op->m_jump_target;
        patch.m_kind = WOORT_IROP_KIND_JIFINITED;
        patch.m_src0_off = 0;
        patch.m_src1_off = 0;

        const uint32_t storage = op->m_jifinited_static + c->m_constant_alloc_count;

        if (!_emit_bc_ex32(blk, woort_OpCode_JIFINITED(0), storage))
            return false;
        return woort_vector_push_back(jump_patches, 1, &patch);
    }

    /* ============ 陷阱/Panic ============ */
    case WOORT_IROP_KIND_DEBUGTRAP:
    {
        (void)c;
        return _emit_bc(blk, woort_OpCode_DEBUGTRAP());
    }

    case WOORT_IROP_KIND_PANIC:
    {
        assert(op->m_src[0] != NULL);

        if (op->m_src[0]->m_is_const_direct)
        {
            uint32_t cidx = op->m_src[0]->m_const_idx;
            if (cidx <= WOORT_UINT24_MAX_VAL)
                return _emit_bc(blk, woort_OpCode_PANICC(cidx));
        }

        int16_t r;
        if (!_load_to_s16(blk, op->m_src[0], -128, &r))
            return false;
        return _emit_bc(blk, woort_OpCode_PANICS(r));
    }

    default:
        assert(false && "Unknown or unhandled IROp kind");
        return false;
    }
}

/* ========================================================================
 * 单个函数的字节码发射
 * ======================================================================== */

static bool _emit_function(
    woort_IRFunction* f,
    woort_IRCompiler* c,
    size_t stack_space,
    woort_Vector* jump_patches,
    woort_Vector* source_map_entries)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    woort_IROp* instructions = (woort_IROp*)f->m_instructions.m_data;

    /* 上一条 IR 指令的 srcloc_index，用于检测边界变化 */
    uint32_t last_srcloc_index = WOORT_SRCLOC_INVALID_INDEX;

    for (uint32_t bi = 0; bi < block_count; ++bi)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, bi);

        /*
         * 发射 block 的常量加载（m_const_loads）
         */
        if (blk->m_const_loads.m_size > 0 && blk->m_const_loads.m_element_size > 0)
        {
            const size_t load_count = blk->m_const_loads.m_size;
            for (size_t li = 0; li < load_count; ++li)
            {
                const _woort_ConstLoadInfo* info =
                    (const _woort_ConstLoadInfo*)woort_vector_at(
                        &blk->m_const_loads, li);

                if (!_emit_const_load(blk, info->m_stack_offset, info->m_const_index))
                    return false;
            }
        }

        /*
         * 发射 block 的指令 [m_begin, m_end)
         */
        for (uint32_t ii = blk->m_begin; ii < blk->m_end; ++ii)
        {
            woort_IROp* op = &instructions[ii];

            /* 跳过 LABEL 伪指令 */
            if (op->m_op == WOORT_IROP_KIND_LABEL)
            {
                continue;
            }

            /*
             * 源码映射边界检测：当 srcloc_index 发生变化时，
             * 记录当前字节码偏移作为新源码位置的起始点。
             */
            if (source_map_entries != NULL &&
                op->m_srcloc_index != last_srcloc_index &&
                op->m_srcloc_index != WOORT_SRCLOC_INVALID_INDEX)
            {
                /* 计算当前全局字节码偏移：
                 * = 之前所有 block 的字节码数 + 当前 block 已发射的字节码数 */
                uint32_t current_offset = 0;
                for (uint32_t prev_bi = 0; prev_bi < bi; ++prev_bi)
                {
                    woort_IRBlock* prev_blk =
                        (woort_IRBlock*)woort_vector_at(&f->m_blocks, prev_bi);
                    current_offset += (uint32_t)prev_blk->m_bytecodes.m_size;
                }
                current_offset += (uint32_t)blk->m_bytecodes.m_size;

                const woort_SourceLocation* loc =
                    (const woort_SourceLocation*)woort_vector_at(
                        &f->m_source_locations, op->m_srcloc_index);

                woort_SourceMap_Entry entry;
                entry.m_bytecode_offset = current_offset;
                entry.m_location = *loc;

                /* 忽略 push_back 失败 —— 映射表丢失不影响正确性 */
                (void)woort_vector_push_back(source_map_entries, 1, &entry);
            }
            last_srcloc_index = op->m_srcloc_index;

            if (!_emit_op(blk, op, c, jump_patches, bi))
                return false;
        }
    }

    return true;
}

/* ========================================================================
 * 跳转修正
 * ======================================================================== */

static bool _patch_jumps(
    woort_IRFunction* f,
    woort_Vector* jump_patches,
    size_t stack_space)
{
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;

    if (jump_patches->m_size == 0)
        return true;

    /*
     * 分配块起始偏移数组
     */
    uint32_t* block_starts = (uint32_t*)malloc(sizeof(uint32_t) * block_count);
    if (block_starts == NULL)
        return false;

    bool need_recalc = true;
    while (need_recalc)
    {
        need_recalc = false;

        /* 计算每个块的起始偏移 */

        /*
         * PUSHRCHK 占据 1 个 bytecode 槽（单指令编码），如果存在，
         * block 0 的实际起始偏移需要加上它。
         */
        uint32_t offset = (stack_space > 0) ? 1 : 0;

        for (uint32_t bi = 0; bi < block_count; ++bi)
        {
            block_starts[bi] = offset;
            woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, bi);
            offset += (uint32_t)blk->m_bytecodes.m_size;
        }

        /* 修正每条跳转指令 */
        for (size_t pi = 0; pi < jump_patches->m_size; ++pi)
        {
            _JumpPatch* patch = (_JumpPatch*)woort_vector_at(jump_patches, pi);
            woort_IRBlock* src_blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, patch->m_block_idx);

            assert(patch->m_target->m_bound);
            const uint32_t target_block_idx = patch->m_target->m_block_index;
            const uint32_t target_addr = block_starts[target_block_idx];
            const uint32_t source_addr = block_starts[patch->m_block_idx] + patch->m_bc_idx;

            woort_Bytecode* bc_ptr =
                (woort_Bytecode*)woort_vector_at(&src_blk->m_bytecodes, patch->m_bc_idx);

            if (patch->m_kind == WOORT_IROP_KIND_JMP)
            {
                /* 无条件跳转：绝对地址 */
                assert(target_addr <= WOORT_UINT26_MAX_VAL);
                if (target_addr <= source_addr)
                    *bc_ptr = woort_OpCode_JBCK(target_addr);
                else
                    *bc_ptr = woort_OpCode_JFWD(target_addr);
            }
            else if (patch->m_kind == WOORT_IROP_KIND_JIFINITED)
            {
                /* JIFINITED：绝对地址（2 字指令，bc_ptr 指向第一字） */
                assert(target_addr <= WOORT_UINT26_MAX_VAL);
                *bc_ptr = woort_OpCode_JIFINITED(target_addr);
            }
            else if (patch->m_kind == WOORT_IROP_KIND_JCC ||
                     patch->m_kind == WOORT_IROP_KIND_JCCZ)
            {
                /* 条件跳转 NZ/Z: 相对偏移 U16 */
                bool is_forward = (target_addr >= source_addr);
                uint32_t rel_offset = is_forward
                    ? (target_addr - source_addr)
                    : (source_addr - target_addr);

                if (rel_offset > UINT16_MAX)
                {
                    /*
                     * 偏移溢出：展开为反转条件跳过 + 无条件跳转
                     * 反转条件: NZ <-> Z
                     */
                    uint32_t inv_m2 = (patch->m_kind == WOORT_IROP_KIND_JCC) ? 1u : 0u;
                    *bc_ptr = woort_OpcodeFormal_OP6_M2_A8_BC16_cons(
                        WOORT_OPCODE_JFWDCND, inv_m2, (uint8_t)(int8_t)patch->m_src0_off, 2);

                    /* 在 bc_idx + 1 处插入无条件跳转 */
                    woort_Bytecode uncond = woort_OpCode_JFWD(0);
                    size_t insert_pos = patch->m_bc_idx + 1;

                    /* 扩容并后移 */
                    woort_Bytecode placeholder = 0;
                    if (!woort_vector_push_back(&src_blk->m_bytecodes, 1, &placeholder))
                    {
                        free(block_starts);
                        return false;
                    }
                    woort_Bytecode* data = (woort_Bytecode*)src_blk->m_bytecodes.m_data;
                    size_t total = src_blk->m_bytecodes.m_size;
                    for (size_t j = total - 1; j > insert_pos; --j)
                        data[j] = data[j - 1];
                    data[insert_pos] = uncond;

                    /* 更新同 block 中后续 patch 的索引 */
                    for (size_t j = 0; j < jump_patches->m_size; ++j)
                    {
                        _JumpPatch* other = (_JumpPatch*)woort_vector_at(jump_patches, j);
                        if (other->m_block_idx == patch->m_block_idx &&
                            other->m_bc_idx > patch->m_bc_idx &&
                            other != patch)
                        {
                            other->m_bc_idx++;
                        }
                    }

                    patch->m_bc_idx = (uint32_t)insert_pos;
                    patch->m_kind = WOORT_IROP_KIND_JMP;
                    need_recalc = true;
                    break;
                }
                else
                {
                    if (is_forward)
                    {
                        if (patch->m_kind == WOORT_IROP_KIND_JCC)
                            *bc_ptr = woort_OpCode_JFWDNZ((int8_t)patch->m_src0_off, (uint16_t)rel_offset);
                        else
                            *bc_ptr = woort_OpCode_JFWDZ((int8_t)patch->m_src0_off, (uint16_t)rel_offset);
                    }
                    else
                    {
                        if (patch->m_kind == WOORT_IROP_KIND_JCC)
                            *bc_ptr = woort_OpCode_JBCKNZ((int8_t)patch->m_src0_off, (uint16_t)rel_offset);
                        else
                            *bc_ptr = woort_OpCode_JBCKZ((int8_t)patch->m_src0_off, (uint16_t)rel_offset);
                    }
                }
            }
            else
            {
                /*
                 * 比较跳转 LT/LE/EQ/NE: 相对偏移 U8
                 */
                bool is_forward = (target_addr >= source_addr);
                uint32_t rel_offset = is_forward
                    ? (target_addr - source_addr)
                    : (source_addr - target_addr);

                if (rel_offset > UINT8_MAX)
                {
                    /*
                     * 偏移溢出：反转条件 + 无条件跳转
                     * LT <-> GE, LE <-> GT, EQ <-> NE
                     */
                    woort_Bytecode inv_code;
                    int8_t a_s8 = (int8_t)patch->m_src0_off;
                    int8_t b_s8 = (int8_t)patch->m_src1_off;

                    switch (patch->m_kind)
                    {
                    case WOORT_IROP_KIND_JCC_LT:
                        inv_code = woort_OpCode_JFWDEG(a_s8, b_s8, 2);
                        break;
                    case WOORT_IROP_KIND_JCC_LE:
                        inv_code = woort_OpCode_JFWDGT(a_s8, b_s8, 2);
                        break;
                    case WOORT_IROP_KIND_JCC_EQ:
                        inv_code = woort_OpCode_JFWDNEQ(a_s8, b_s8, 2);
                        break;
                    case WOORT_IROP_KIND_JCC_NE:
                        inv_code = woort_OpCode_JFWDEQ(a_s8, b_s8, 2);
                        break;
                    default:
                        assert(false && "Unreachable comparison kind");
                        inv_code = 0;
                        break;
                    }

                    *bc_ptr = inv_code;

                    /* 插入无条件跳转 */
                    woort_Bytecode uncond = woort_OpCode_JFWD(0);
                    size_t insert_pos = patch->m_bc_idx + 1;

                    woort_Bytecode placeholder = 0;
                    if (!woort_vector_push_back(&src_blk->m_bytecodes, 1, &placeholder))
                    {
                        free(block_starts);
                        return false;
                    }
                    woort_Bytecode* data = (woort_Bytecode*)src_blk->m_bytecodes.m_data;
                    size_t total = src_blk->m_bytecodes.m_size;
                    for (size_t j = total - 1; j > insert_pos; --j)
                        data[j] = data[j - 1];
                    data[insert_pos] = uncond;

                    for (size_t j = 0; j < jump_patches->m_size; ++j)
                    {
                        _JumpPatch* other = (_JumpPatch*)woort_vector_at(jump_patches, j);
                        if (other->m_block_idx == patch->m_block_idx &&
                            other->m_bc_idx > patch->m_bc_idx &&
                            other != patch)
                        {
                            other->m_bc_idx++;
                        }
                    }

                    patch->m_bc_idx = (uint32_t)insert_pos;
                    patch->m_kind = WOORT_IROP_KIND_JMP;
                    need_recalc = true;
                    break;
                }
                else
                {
                    int8_t a_s8 = (int8_t)patch->m_src0_off;
                    int8_t b_s8 = (int8_t)patch->m_src1_off;
                    uint8_t off8 = (uint8_t)rel_offset;

                    if (is_forward)
                    {
                        switch (patch->m_kind)
                        {
                        case WOORT_IROP_KIND_JCC_LT:
                            *bc_ptr = woort_OpCode_JFWDLT(a_s8, b_s8, off8);
                            break;
                        case WOORT_IROP_KIND_JCC_LE:
                            *bc_ptr = woort_OpCode_JFWDEL(a_s8, b_s8, off8);
                            break;
                        case WOORT_IROP_KIND_JCC_EQ:
                            *bc_ptr = woort_OpCode_JFWDEQ(a_s8, b_s8, off8);
                            break;
                        case WOORT_IROP_KIND_JCC_NE:
                            *bc_ptr = woort_OpCode_JFWDNEQ(a_s8, b_s8, off8);
                            break;
                        default:
                            assert(false);
                            break;
                        }
                    }
                    else
                    {
                        switch (patch->m_kind)
                        {
                        case WOORT_IROP_KIND_JCC_LT:
                            *bc_ptr = woort_OpCode_JBCKLT(a_s8, b_s8, off8);
                            break;
                        case WOORT_IROP_KIND_JCC_LE:
                            *bc_ptr = woort_OpCode_JBCKEL(a_s8, b_s8, off8);
                            break;
                        case WOORT_IROP_KIND_JCC_EQ:
                            *bc_ptr = woort_OpCode_JBCKEQ(a_s8, b_s8, off8);
                            break;
                        case WOORT_IROP_KIND_JCC_NE:
                            *bc_ptr = woort_OpCode_JBCKNEQ(a_s8, b_s8, off8);
                            break;
                        default:
                            assert(false);
                            break;
                        }
                    }
                }
            }
        }
    }

    free(block_starts);
    return true;
}

/* ========================================================================
 * 单个函数的完整编译流程
 * ======================================================================== */

static bool _compile_function(
    woort_IRFunction* f,
    woort_IRCompiler* c,
    woort_Vector* source_map_entries)
{
    /* 第 1 步：分析 + 栈槽分配 */
    size_t stack_space;
    if (!_woort_IRFunction_analyze_and_allocate(f, &stack_space))
        return false;

    /* 第 2 步：发射字节码 */
    woort_Vector jump_patches;
    woort_vector_init(&jump_patches, sizeof(_JumpPatch));

    if (!_emit_function(f, c, stack_space, &jump_patches, source_map_entries))
    {
        woort_vector_deinit(&jump_patches);
        return false;
    }

    /* 第 3 步：跳转修正 */
    if (!_patch_jumps(f, &jump_patches, stack_space))
    {
        woort_vector_deinit(&jump_patches);
        return false;
    }

    woort_vector_deinit(&jump_patches);

    /* 第 4 步：将 block 字节码拼接到 compiler 的 m_commited_codes */
    const uint32_t block_count = (uint32_t)f->m_blocks.m_size;
    f->m_code_offset = c->m_commited_codes.m_size;

    /*
     * PUSHRCHK 预留在函数入口处单独发射，不放在任何 block 内，
     * 以避免后向跳转（如循环）错误地跳到 PUSHRCHK 指令上。
     * 将其放在所有 block 字节码之前，这样 block_starts[0] 指向
     * 第一个实际 IR 指令，后向跳转会跳过 PUSHRCHK。
     */
    if (stack_space > 0)
    {
        assert(stack_space <= WOORT_UINT24_MAX_VAL);
        woort_Bytecode bc = woort_OpCode_PUSHRCHK((uint32_t)stack_space);
        if (!woort_vector_push_back(&c->m_commited_codes, 1, &bc))
            return false;
    }

    /* 更新 m_code_offset 以反映 PUSHRCHK 的大小 */
    f->m_code_offset = c->m_commited_codes.m_size - (stack_space > 0 ? 1 : 0);

    for (uint32_t bi = 0; bi < block_count; ++bi)
    {
        woort_IRBlock* blk = (woort_IRBlock*)woort_vector_at(&f->m_blocks, bi);
        if (blk->m_bytecodes.m_size > 0)
        {
            if (!woort_vector_push_back(
                &c->m_commited_codes,
                blk->m_bytecodes.m_size,
                blk->m_bytecodes.m_data))
                return false;
        }
    }

    f->m_code_length = c->m_commited_codes.m_size - f->m_code_offset;
    return true;
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

void woort_IRCompiler_init(woort_IRCompiler* c)
{
    woort_linklist_init(&c->m_ir_functions, sizeof(woort_IRFunction));
    c->m_constant_alloc_count = 0;
    c->m_static_storage_alloc_count = 0;
    woort_vector_init(&c->m_commited_codes, sizeof(woort_Bytecode));
    woort_StringPool_init(&c->m_string_pool);
}

void woort_IRCompiler_deinit(woort_IRCompiler* c)
{
    for (woort_IRFunction* f = woort_linklist_iter(&c->m_ir_functions);
        f != NULL;
        f = woort_linklist_next(f))
    {
        woort_IRFunction_deinit(f);
    }
    woort_linklist_deinit(&c->m_ir_functions);
    woort_vector_deinit(&c->m_commited_codes);
    woort_StringPool_deinit(&c->m_string_pool);
}

WOORT_NODISCARD bool woort_IRCompiler_add_function(
    woort_IRCompiler* c, uint32_t param_count, uint32_t captured_count, woort_IRFunction** out_f)
{
    void* storage;
    if (!woort_linklist_emplace_back(&c->m_ir_functions, &storage))
        return false;

    woort_IRFunction* f = (woort_IRFunction*)storage;
    woort_IRFunction_init(f, param_count, captured_count);
    *out_f = f;
    return true;
}

WOORT_NODISCARD woort_IRConstantIndex woort_IRCompiler_add_constant(woort_IRCompiler* c)
{
    return c->m_constant_alloc_count++;
}

WOORT_NODISCARD woort_IRStaticIndex woort_IRCompiler_add_static(woort_IRCompiler* c)
{
    return c->m_static_storage_alloc_count++;
}

WOORT_NODISCARD bool woort_IRCompiler_finish(woort_IRCompiler* c, woort_CodeEnv** out_cenv)
{
    /*
     * 收集所有函数的源码映射条目。
     * 使用一个临时 vector 收集每个函数的映射条目，
     * 编译完成后统一转移到 CodeEnv。
     */

    /* 计算函数数量 */
    uint32_t func_count = 0;
    for (woort_IRFunction* f = woort_linklist_iter(&c->m_ir_functions);
        f != NULL;
        f = woort_linklist_next(f))
    {
        func_count++;
    }

    /* 为每个函数分配临时的映射条目收集器 */
    woort_Vector* per_func_entries = NULL;
    if (func_count > 0)
    {
        per_func_entries = (woort_Vector*)malloc(
            sizeof(woort_Vector) * func_count);
        if (per_func_entries == NULL)
            return false;

        for (uint32_t i = 0; i < func_count; ++i)
            woort_vector_init(&per_func_entries[i], sizeof(woort_SourceMap_Entry));
    }

    /* 对每个函数执行完整的编译流程 */
    {
        uint32_t fi = 0;
        for (woort_IRFunction* f = woort_linklist_iter(&c->m_ir_functions);
            f != NULL;
            f = woort_linklist_next(f), ++fi)
        {
            if (!_compile_function(f, c, &per_func_entries[fi]))
            {
                /* 清理临时数据 */
                for (uint32_t j = 0; j < func_count; ++j)
                    woort_vector_deinit(&per_func_entries[j]);
                free(per_func_entries);
                return false;
            }

            /*
             * _compile_function 完成后，该函数的 m_code_offset 已确定。
             * 映射条目中的偏移量是函数内相对偏移，需要加上全局偏移。
             */
            for (size_t ei = 0; ei < per_func_entries[fi].m_size; ++ei)
            {
                woort_SourceMap_Entry* entry =
                    (woort_SourceMap_Entry*)woort_vector_at(
                        &per_func_entries[fi], ei);
                entry->m_bytecode_offset += (uint32_t)f->m_code_offset;
            }
        }
    }

    /* 创建 CodeEnv */
    const size_t data_count =
        (size_t)c->m_constant_alloc_count +
        (size_t)c->m_static_storage_alloc_count;

    bool result = woort_CodeEnv_create(
        (const woort_Bytecode*)c->m_commited_codes.m_data,
        c->m_commited_codes.m_size,
        data_count,
        out_cenv);

    if (result)
    {
        /*
         * 将源码映射数据转移到 CodeEnv。
         * CodeEnv 拥有映射数据的所有权，路径字符串会被复制。
         */
        woort_CodeEnv_set_source_maps(
            *out_cenv,
            per_func_entries,
            func_count);
    }

    /* 清理临时数据 */
    if (per_func_entries != NULL)
    {
        for (uint32_t i = 0; i < func_count; ++i)
            woort_vector_deinit(&per_func_entries[i]);
        free(per_func_entries);
    }
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ const char* woort_IRCompiler_intern_string(
    woort_IRCompiler* c, const char* str)
{
    return woort_StringPool_intern(&c->m_string_pool, str);
}
