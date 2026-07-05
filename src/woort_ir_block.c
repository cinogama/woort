/*
 * woort_ir_block.c
 *
 * 新 IR 指令发射函数实现。
 * 所有 woort_IR_* 函数向 IRFunction 的 m_instructions (woort_Vector of woort_IROp)
 * 追加一条指令，返回 false 表示 OOM。
 */

#include "woort.h"

#include "woort_ir_block.h"
#include "woort_ir_function.h"
#include "woort_ir_srcloc.h"

#include <string.h>

/* ========== IRBlock 初始化/析构 ========== */

void _woort_IRBlock_init(woort_IRBlock* block)
{
    memset(block, 0, sizeof(woort_IRBlock));

    block->m_begin = 0;
    block->m_end = 0;

    woort_vector_init(&block->m_successors, sizeof(uint32_t));
    woort_vector_init(&block->m_predecessors, sizeof(uint32_t));

    /* Bitset 零初始化，finish 阶段按需 woort_bitset_init */
    memset(&block->m_use, 0, sizeof(woort_Bitset));
    memset(&block->m_def, 0, sizeof(woort_Bitset));
    memset(&block->m_live_in, 0, sizeof(woort_Bitset));
    memset(&block->m_live_out, 0, sizeof(woort_Bitset));

    block->m_idom = -1;
    block->m_dom_depth = 0;

    block->m_is_in_loop = false;
    block->m_loop_header = -1;

    woort_vector_init(&block->m_const_loads, sizeof(_woort_ConstLoadInfo));
    woort_vector_init(&block->m_bytecodes, sizeof(uint32_t));
}

void _woort_IRBlock_deinit(woort_IRBlock* block)
{
    woort_vector_deinit(&block->m_successors);
    woort_vector_deinit(&block->m_predecessors);

    woort_bitset_deinit(&block->m_use);
    woort_bitset_deinit(&block->m_def);
    woort_bitset_deinit(&block->m_live_in);
    woort_bitset_deinit(&block->m_live_out);

    woort_vector_deinit(&block->m_const_loads);
    woort_vector_deinit(&block->m_bytecodes);
}

/* ========== 内部辅助宏 ========== */

/*
 * _EMIT_BEGIN: 在 f->m_instructions 末尾分配一条 woort_IROp 并清零，
 *              设置 m_op 和 m_srcloc_index（从栈顶获取当前源码位置索引）。
 *              成功时局部变量 op_ 指向新指令，失败时返回 false。
 */
#define _EMIT_BEGIN(f, kind)                                                    \
    woort_IROp* op_;                                                            \
    {                                                                           \
        void* _storage_;                                                        \
        if (!woort_vector_emplace_back(&(f)->m_instructions, 1, &_storage_))    \
            return false;                                                       \
        op_ = (woort_IROp*)_storage_;                                           \
        memset(op_, 0, sizeof(woort_IROp));                                     \
        op_->m_op = (kind);                                                     \
        op_->m_srcloc_index = _woort_IRFunction_current_srcloc_index(f);        \
    }

#define _EMIT_END() return true

/* 一元运算：dst = op(src) */
#define _DEFINE_UNARY_OP(name, kind)                                            \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f, woort_IRValue* dst, const woort_IRValue* src)      \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = dst;                                                       \
        op_->m_src[0] = src;                                                    \
        _EMIT_END();                                                            \
    }

/* 二元运算：dst = a op b */
#define _DEFINE_BINARY_OP(name, kind)                                           \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f, woort_IRValue* dst,                                \
        const woort_IRValue* a, const woort_IRValue* b)                         \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = dst;                                                       \
        op_->m_src[0] = a;                                                      \
        op_->m_src[1] = b;                                                      \
        _EMIT_END();                                                            \
    }

/* 直接调用：CALLNWO/CALLNFP/CALLNJIT */
#define _DEFINE_CALLN(name, kind)                                               \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f, woort_IRConstantIndex target,                      \
        uint32_t argc, /* OPTIONAL */ woort_IRValue* dst)                       \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = dst;                                                       \
        op_->m_calln_target = target;                                           \
        op_->m_argument_count = argc;                                           \
        _EMIT_END();                                                            \
    }

/* 容器创建：MKVEC/MKMAP/MKSTRUCT */
#define _DEFINE_MKCONT(name, kind)                                              \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f, woort_IRValue* dst, uint32_t count)                \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = dst;                                                       \
        op_->m_count = count;                                                   \
        _EMIT_END();                                                            \
    }

/* 动态类型：BOXDYN/UNBOXDYN/CHECKDYN */
#define _DEFINE_DYNBOX(name, kind)                                              \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f, woort_IRValue* dst,                                \
        uint8_t typ, const woort_IRValue* src)                                  \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = dst;                                                       \
        op_->m_src[0] = src;                                                    \
        op_->m_type = typ;                                                      \
        _EMIT_END();                                                            \
    }

/* 索引加载（双寄存器索引）：dst = container[idx_val] */
#define _DEFINE_LDID_VREG(name, kind)                                          \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f, woort_IRValue* dst,                                \
        const woort_IRValue* container, const woort_IRValue* idx)               \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = dst;                                                       \
        op_->m_src[0] = container;                                              \
        op_->m_src[1] = idx;                                                    \
        _EMIT_END();                                                            \
    }

/* 索引存储（三寄存器）：container[idx] = val, dst=NULL */
#define _DEFINE_SDID_VREG(name, kind)                                          \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f,                                                    \
        const woort_IRValue* c, const woort_IRValue* idx, const woort_IRValue* val)   \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = NULL;                                                      \
        op_->m_src[0] = c;                                                      \
        op_->m_src[1] = idx;                                                    \
        op_->m_src[2] = val;                                                    \
        _EMIT_END();                                                            \
    }

/* 结构体字段推栈：dst=NULL, src[0]=src, m_index=idx */
#define _DEFINE_PUSHID(name, kind)                                             \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f, const woort_IRValue* src, uint32_t idx)            \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_dst = NULL;                                                      \
        op_->m_src[0] = src;                                                    \
        op_->m_index = idx;                                                     \
        _EMIT_END();                                                            \
    }

/* 比较跳转：if (a <cmp> b) goto target */
#define _DEFINE_JCC_CMP(name, kind)                                             \
    WOORT_NODISCARD bool name(                                                  \
        woort_IRFunction* f,                                                    \
        const woort_IRValue* a, const woort_IRValue* b, woort_IRLabel* target)  \
    {                                                                           \
        _EMIT_BEGIN(f, kind);                                                   \
        op_->m_src[0] = a;                                                      \
        op_->m_src[1] = b;                                                      \
        op_->m_jump_target = target;                                            \
        _EMIT_END();                                                            \
    }

/* ========== 数据移动 ========== */

WOORT_NODISCARD bool woort_IR_NOP(woort_IRFunction* f)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_NOP);
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_MOV(
    woort_IRFunction* f, woort_IRValue* dst, const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_MOV);
    op_->m_dst = dst;
    op_->m_src[0] = src;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_LOAD(
    woort_IRFunction* f, woort_IRValue* dst, woort_IRStaticIndex idx)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_LOAD);
    op_->m_dst = dst;
    op_->m_static_index = idx;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_STORE(
    woort_IRFunction* f, woort_IRStaticIndex idx, const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_STORE);
    op_->m_dst = NULL;
    op_->m_src[0] = src;
    op_->m_static_index = idx;
    _EMIT_END();
}

/* ========== pvalue 指针操作 ========== */

WOORT_NODISCARD bool woort_IR_LOADPVALUE(
    woort_IRFunction* f, woort_IRValue* dst, const woort_IRValue* ptr)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_LOADPVALUE);
    op_->m_dst = dst;
    op_->m_src[0] = ptr;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_STOREPVALUE(
    woort_IRFunction* f, const woort_IRValue* ptr, const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_STOREPVALUE);
    op_->m_dst = NULL;
    op_->m_src[0] = ptr;
    op_->m_src[1] = src;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_MKPVALUE(
    woort_IRFunction* f, woort_IRValue* dst, const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_MKPVALUE);
    op_->m_dst = dst;
    op_->m_src[0] = src;
    _EMIT_END();
}

/* ========== 栈操作 ========== */

WOORT_NODISCARD bool woort_IR_PUSHCHK(
    woort_IRFunction* f, const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_PUSHCHK);
    op_->m_dst = NULL;
    op_->m_src[0] = src;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_PUSHSTATICCHK(
    woort_IRFunction* f, woort_IRStaticIndex src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_PUSHSTATICCHK);
    op_->m_dst = NULL;
    op_->m_static_index = src;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_POP(
    woort_IRFunction* f, woort_IRValue* dst)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_POP);
    op_->m_dst = dst;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_POPR(
    woort_IRFunction* f, uint32_t count)
{
    if (count == 0)
        return true;

    _EMIT_BEGIN(f, WOORT_IROP_KIND_POPR);
    op_->m_dst = NULL;
    op_->m_pop_count = count;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_POPRS(
    woort_IRFunction* f, const woort_IRValue* count_src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_POPRS);
    op_->m_dst = NULL;
    op_->m_src[0] = count_src;
    _EMIT_END();
}

/* ========== 类型转换 ========== */

_DEFINE_UNARY_OP(woort_IR_ITOR, WOORT_IROP_KIND_ITOR)
_DEFINE_UNARY_OP(woort_IR_ITOS, WOORT_IROP_KIND_ITOS)
_DEFINE_UNARY_OP(woort_IR_RTOI, WOORT_IROP_KIND_RTOI)
_DEFINE_UNARY_OP(woort_IR_RTOS, WOORT_IROP_KIND_RTOS)

/* ========== 函数调用 ========== */

_DEFINE_CALLN(woort_IR_CALLNWO,  WOORT_IROP_KIND_CALLNWO)
_DEFINE_CALLN(woort_IR_CALLNFP,  WOORT_IROP_KIND_CALLNFP)
_DEFINE_CALLN(woort_IR_CALLNJIT, WOORT_IROP_KIND_CALLNJIT)

WOORT_NODISCARD bool woort_IR_CALL(
    woort_IRFunction* f, const woort_IRValue* func_val,
    uint32_t argc, /* OPTIONAL */ woort_IRValue* dst)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_CALL);
    op_->m_dst = dst;
    op_->m_src[0] = func_val;
    op_->m_call_argument_count = argc;
    _EMIT_END();
}

/* ========== 闭包/容器 ========== */

WOORT_NODISCARD bool woort_IR_MKCLOSURE(
    woort_IRFunction* f, woort_IRValue* dst,
    uint32_t elem_count, woort_IRConstantIndex func_idx)
{
    /*
     * 复用 m_calln_target + m_argument_count 匿名结构体：
     *   m_calln_target   = func_idx
     *   m_argument_count  = elem_count
     */
    _EMIT_BEGIN(f, WOORT_IROP_KIND_MKCLOSURE);
    op_->m_dst = dst;
    op_->m_calln_target = func_idx;
    op_->m_argument_count = elem_count;
    _EMIT_END();
}

_DEFINE_MKCONT(woort_IR_MKVEC,    WOORT_IROP_KIND_MKVEC)
_DEFINE_MKCONT(woort_IR_MKMAP,    WOORT_IROP_KIND_MKMAP)
_DEFINE_MKCONT(woort_IR_MKSTRUCT, WOORT_IROP_KIND_MKSTRUCT)

/* MKUNION: dst = new Union(union_id, src) */
WOORT_NODISCARD bool woort_IR_MKUNION(
    woort_IRFunction* f,
    woort_IRValue* dst,
    const woort_IRValue* src,
    uint32_t union_id)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_MKUNION);
    op_->m_dst = dst;
    op_->m_src[0] = src;
    op_->m_index = union_id;
    _EMIT_END();
}

/* ========== 动态类型 ========== */

_DEFINE_DYNBOX(woort_IR_BOXDYN,   WOORT_IROP_KIND_BOXDYN)
_DEFINE_DYNBOX(woort_IR_UNBOXDYN, WOORT_IROP_KIND_UNBOXDYN)
_DEFINE_DYNBOX(woort_IR_CHECKDYN, WOORT_IROP_KIND_CHECKDYN)

WOORT_NODISCARD bool woort_IR_PUSHBOXDYN(
    woort_IRFunction* f, uint8_t typ, const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_PUSHBOXDYN);
    op_->m_dst = NULL;
    op_->m_src[0] = src;
    op_->m_type = typ;
    _EMIT_END();
}

/* ========== 字符串/BOXED 转换 ========== */

_DEFINE_DYNBOX(woort_IR_CASTSTO,   WOORT_IROP_KIND_CASTSTO)
_DEFINE_DYNBOX(woort_IR_CASTSFROM, WOORT_IROP_KIND_CASTSFROM)
_DEFINE_DYNBOX(woort_IR_CASTDYN,   WOORT_IROP_KIND_CASTDYN)

WOORT_NODISCARD bool woort_IR_ASSERTDYN(
    woort_IRFunction* f, uint8_t typ, const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_ASSERTDYN);
    op_->m_dst = NULL;
    op_->m_src[0] = src;
    op_->m_type = typ;
    _EMIT_END();
}

/* ========== 整数算术 ========== */

_DEFINE_BINARY_OP(woort_IR_ADDI, WOORT_IROP_KIND_ADDI)
_DEFINE_BINARY_OP(woort_IR_SUBI, WOORT_IROP_KIND_SUBI)
_DEFINE_BINARY_OP(woort_IR_MULI, WOORT_IROP_KIND_MULI)
_DEFINE_BINARY_OP(woort_IR_DIVI, WOORT_IROP_KIND_DIVI)
_DEFINE_BINARY_OP(woort_IR_MODI, WOORT_IROP_KIND_MODI)
_DEFINE_UNARY_OP(woort_IR_NEGI,  WOORT_IROP_KIND_NEGI)

/* ========== 整除检查 ========== */

WOORT_NODISCARD bool woort_IR_CHKDIVIL(
    woort_IRFunction* f, const woort_IRValue* a)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_CHKDIVIL);
    op_->m_dst = NULL;
    op_->m_src[0] = a;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_CHKDIVIR(
    woort_IRFunction* f, const woort_IRValue* a)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_CHKDIVIR);
    op_->m_dst = NULL;
    op_->m_src[0] = a;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_CHKDIVIRZ(
    woort_IRFunction* f, const woort_IRValue* a)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_CHKDIVIRZ);
    op_->m_dst = NULL;
    op_->m_src[0] = a;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_CHKDIVILR(
    woort_IRFunction* f, const woort_IRValue* a, const woort_IRValue* b)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_CHKDIVILR);
    op_->m_dst = NULL;
    op_->m_src[0] = a;
    op_->m_src[1] = b;
    _EMIT_END();
}

/* ========== 整数比较 ========== */

_DEFINE_BINARY_OP(woort_IR_LTI, WOORT_IROP_KIND_LTI)
_DEFINE_BINARY_OP(woort_IR_GTI, WOORT_IROP_KIND_GTI)
_DEFINE_BINARY_OP(woort_IR_LEI, WOORT_IROP_KIND_LEI)
_DEFINE_BINARY_OP(woort_IR_GEI, WOORT_IROP_KIND_GEI)
_DEFINE_BINARY_OP(woort_IR_EQI, WOORT_IROP_KIND_EQI)
_DEFINE_BINARY_OP(woort_IR_NEI, WOORT_IROP_KIND_NEI)

/* ========== 实数算术 ========== */

_DEFINE_BINARY_OP(woort_IR_ADDR, WOORT_IROP_KIND_ADDR)
_DEFINE_BINARY_OP(woort_IR_SUBR, WOORT_IROP_KIND_SUBR)
_DEFINE_BINARY_OP(woort_IR_MULR, WOORT_IROP_KIND_MULR)
_DEFINE_BINARY_OP(woort_IR_DIVR, WOORT_IROP_KIND_DIVR)
_DEFINE_BINARY_OP(woort_IR_MODR, WOORT_IROP_KIND_MODR)
_DEFINE_UNARY_OP(woort_IR_NEGR,  WOORT_IROP_KIND_NEGR)

/* ========== 实数比较 ========== */

_DEFINE_BINARY_OP(woort_IR_LTR, WOORT_IROP_KIND_LTR)
_DEFINE_BINARY_OP(woort_IR_GTR, WOORT_IROP_KIND_GTR)
_DEFINE_BINARY_OP(woort_IR_LER, WOORT_IROP_KIND_LER)
_DEFINE_BINARY_OP(woort_IR_GER, WOORT_IROP_KIND_GER)
_DEFINE_BINARY_OP(woort_IR_EQR, WOORT_IROP_KIND_EQR)
_DEFINE_BINARY_OP(woort_IR_NER, WOORT_IROP_KIND_NER)

/* ========== 字符串 ========== */

_DEFINE_BINARY_OP(woort_IR_ADDS, WOORT_IROP_KIND_ADDS)
_DEFINE_BINARY_OP(woort_IR_LTS,  WOORT_IROP_KIND_LTS)
_DEFINE_BINARY_OP(woort_IR_GTS,  WOORT_IROP_KIND_GTS)
_DEFINE_BINARY_OP(woort_IR_LES,  WOORT_IROP_KIND_LES)
_DEFINE_BINARY_OP(woort_IR_GES,  WOORT_IROP_KIND_GES)
_DEFINE_BINARY_OP(woort_IR_EQS,  WOORT_IROP_KIND_EQS)
_DEFINE_BINARY_OP(woort_IR_NES,  WOORT_IROP_KIND_NES)

/* ========== 逻辑 ========== */

_DEFINE_BINARY_OP(woort_IR_LAND, WOORT_IROP_KIND_LAND)
_DEFINE_BINARY_OP(woort_IR_LOR,  WOORT_IROP_KIND_LOR)
_DEFINE_UNARY_OP(woort_IR_LNOT,  WOORT_IROP_KIND_LNOT)

/* ========== 索引加载 ========== */

_DEFINE_LDID_VREG(woort_IR_LDIDVEC,    WOORT_IROP_KIND_LDIDVEC)
_DEFINE_LDID_VREG(woort_IR_LDIDVECX,   WOORT_IROP_KIND_LDIDVECX)
_DEFINE_LDID_VREG(woort_IR_LDIDSTRING, WOORT_IROP_KIND_LDIDSTRING)

_DEFINE_LDID_VREG(woort_IR_LDIDDICTI, WOORT_IROP_KIND_LDIDDICTI)
_DEFINE_LDID_VREG(woort_IR_LDIDDICTR, WOORT_IROP_KIND_LDIDDICTR)
_DEFINE_LDID_VREG(woort_IR_LDIDDICTB, WOORT_IROP_KIND_LDIDDICTB)
_DEFINE_LDID_VREG(woort_IR_LDIDDICTX, WOORT_IROP_KIND_LDIDDICTX)
_DEFINE_LDID_VREG(woort_IR_LDIDDICTIX, WOORT_IROP_KIND_LDIDDICTIX)
_DEFINE_LDID_VREG(woort_IR_LDIDDICTRX, WOORT_IROP_KIND_LDIDDICTRX)
_DEFINE_LDID_VREG(woort_IR_LDIDDICTBX, WOORT_IROP_KIND_LDIDDICTBX)
_DEFINE_LDID_VREG(woort_IR_LDIDDICTXX, WOORT_IROP_KIND_LDIDDICTXX)

/* LDIDSTRUCT: dst = container.field[idx] (立即数索引) */
WOORT_NODISCARD bool woort_IR_LDIDSTRUCT(
    woort_IRFunction* f, woort_IRValue* dst,
    const woort_IRValue* container, uint32_t idx)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_LDIDSTRUCT);
    op_->m_dst = dst;
    op_->m_src[0] = container;
    op_->m_index = idx;
    _EMIT_END();
}

/* ========== 索引存储 ========== */

/* --- vec --- */
_DEFINE_SDID_VREG(woort_IR_STIDVECI, WOORT_IROP_KIND_SDIDVECI)
_DEFINE_SDID_VREG(woort_IR_STIDVECR, WOORT_IROP_KIND_SDIDVECR)
_DEFINE_SDID_VREG(woort_IR_STIDVECB, WOORT_IROP_KIND_SDIDVECB)
_DEFINE_SDID_VREG(woort_IR_STIDVECX, WOORT_IROP_KIND_SDIDVECX)

/* --- dict --- */
_DEFINE_SDID_VREG(woort_IR_STIDDICTII, WOORT_IROP_KIND_SDIDDICTII)
_DEFINE_SDID_VREG(woort_IR_STIDDICTIR, WOORT_IROP_KIND_SDIDDICTIR)
_DEFINE_SDID_VREG(woort_IR_STIDDICTIB, WOORT_IROP_KIND_SDIDDICTIB)
_DEFINE_SDID_VREG(woort_IR_STIDDICTIX, WOORT_IROP_KIND_SDIDDICTIX)
_DEFINE_SDID_VREG(woort_IR_STIDDICTRI, WOORT_IROP_KIND_SDIDDICTRI)
_DEFINE_SDID_VREG(woort_IR_STIDDICTRR, WOORT_IROP_KIND_SDIDDICTRR)
_DEFINE_SDID_VREG(woort_IR_STIDDICTRB, WOORT_IROP_KIND_SDIDDICTRB)
_DEFINE_SDID_VREG(woort_IR_STIDDICTRX, WOORT_IROP_KIND_SDIDDICTRX)
_DEFINE_SDID_VREG(woort_IR_STIDDICTBI, WOORT_IROP_KIND_SDIDDICTBI)
_DEFINE_SDID_VREG(woort_IR_STIDDICTBR, WOORT_IROP_KIND_SDIDDICTBR)
_DEFINE_SDID_VREG(woort_IR_STIDDICTBB, WOORT_IROP_KIND_SDIDDICTBB)
_DEFINE_SDID_VREG(woort_IR_STIDDICTBX, WOORT_IROP_KIND_SDIDDICTBX)
_DEFINE_SDID_VREG(woort_IR_STIDDICTXI, WOORT_IROP_KIND_SDIDDICTXI)
_DEFINE_SDID_VREG(woort_IR_STIDDICTXR, WOORT_IROP_KIND_SDIDDICTXR)
_DEFINE_SDID_VREG(woort_IR_STIDDICTXB, WOORT_IROP_KIND_SDIDDICTXB)
_DEFINE_SDID_VREG(woort_IR_STIDDICTXX, WOORT_IROP_KIND_SDIDDICTXX)

/* --- map --- */
_DEFINE_SDID_VREG(woort_IR_STIDMAPII, WOORT_IROP_KIND_SDIDMAPII)
_DEFINE_SDID_VREG(woort_IR_STIDMAPIR, WOORT_IROP_KIND_SDIDMAPIR)
_DEFINE_SDID_VREG(woort_IR_STIDMAPIB, WOORT_IROP_KIND_SDIDMAPIB)
_DEFINE_SDID_VREG(woort_IR_STIDMAPIX, WOORT_IROP_KIND_SDIDMAPIX)
_DEFINE_SDID_VREG(woort_IR_STIDMAPRI, WOORT_IROP_KIND_SDIDMAPRI)
_DEFINE_SDID_VREG(woort_IR_STIDMAPRR, WOORT_IROP_KIND_SDIDMAPRR)
_DEFINE_SDID_VREG(woort_IR_STIDMAPRB, WOORT_IROP_KIND_SDIDMAPRB)
_DEFINE_SDID_VREG(woort_IR_STIDMAPRX, WOORT_IROP_KIND_SDIDMAPRX)
_DEFINE_SDID_VREG(woort_IR_STIDMAPBI, WOORT_IROP_KIND_SDIDMAPBI)
_DEFINE_SDID_VREG(woort_IR_STIDMAPBR, WOORT_IROP_KIND_SDIDMAPBR)
_DEFINE_SDID_VREG(woort_IR_STIDMAPBB, WOORT_IROP_KIND_SDIDMAPBB)
_DEFINE_SDID_VREG(woort_IR_STIDMAPBX, WOORT_IROP_KIND_SDIDMAPBX)
_DEFINE_SDID_VREG(woort_IR_STIDMAPXI, WOORT_IROP_KIND_SDIDMAPXI)
_DEFINE_SDID_VREG(woort_IR_STIDMAPXR, WOORT_IROP_KIND_SDIDMAPXR)
_DEFINE_SDID_VREG(woort_IR_STIDMAPXB, WOORT_IROP_KIND_SDIDMAPXB)
_DEFINE_SDID_VREG(woort_IR_STIDMAPXX, WOORT_IROP_KIND_SDIDMAPXX)

/* SDIDSTRUCT: container.field[idx] = val (立即数索引) */
WOORT_NODISCARD bool woort_IR_STIDSTRUCT(
    woort_IRFunction* f,
    const woort_IRValue* c, uint32_t idx, const woort_IRValue* val)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_SDIDSTRUCT);
    op_->m_dst = NULL;
    op_->m_src[0] = c;
    op_->m_src[1] = val;
    op_->m_index = idx;
    _EMIT_END();
}

/* ========== 解包 ========== */

WOORT_NODISCARD bool woort_IR_UNPACKVEC(
    woort_IRFunction* f,
    uint8_t count,
    const woort_IRValue* val)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_UNPACKVEC);
    op_->m_dst = NULL;
    op_->m_src[0] = val;
    op_->m_count = count;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_UNPACKVECX(
    woort_IRFunction* f,
    uint8_t count,
    const woort_IRValue* val)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_UNPACKVECX);
    op_->m_dst = NULL;
    op_->m_src[0] = val;
    op_->m_count = count;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_UNPACKVECALL(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t count,
    const woort_IRValue* val)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_UNPACKVECALL);
    op_->m_dst = dst;
    op_->m_src[0] = val;
    op_->m_count = count;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_UNPACKVECXALL(
    woort_IRFunction* f,
    woort_IRValue* dst,
    uint8_t count,
    const woort_IRValue* val)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_UNPACKVECXALL);
    op_->m_dst = dst;
    op_->m_src[0] = val;
    op_->m_count = count;
    _EMIT_END();
}

/* ========== 结构体字段推栈 ========== */

_DEFINE_PUSHID(woort_IR_PUSHIDSTRUCT, WOORT_IROP_KIND_PUSHIDSTRUCT)
_DEFINE_PUSHID(woort_IR_PUSHIDSTBOXI, WOORT_IROP_KIND_PUSHIDSTBOXI)
_DEFINE_PUSHID(woort_IR_PUSHIDSTBOXR, WOORT_IROP_KIND_PUSHIDSTBOXR)
_DEFINE_PUSHID(woort_IR_PUSHIDSTBOXB, WOORT_IROP_KIND_PUSHIDSTBOXB)

/* ========== 原子操作 ========== */

WOORT_NODISCARD bool woort_IR_ASTORE(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    const woort_IRValue* src)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_ASTORE);
    op_->m_dst = NULL;
    op_->m_src[0] = src;
    op_->m_static_index = idx;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_ALOAD(
    woort_IRFunction* f,
    woort_IRValue* dst,
    woort_IRStaticIndex idx)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_ALOAD);
    op_->m_dst = dst;
    op_->m_static_index = idx;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_CAS(
    woort_IRFunction* f,
    woort_IRStaticIndex idx,
    woort_IRValue* expected,
    const woort_IRValue* desired)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_CAS);
    op_->m_dst = expected;
    op_->m_src[0] = desired;
    op_->m_static_index = idx;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_PACKARG(
    woort_IRFunction* f,
    uint16_t named_param_count,
    woort_IRValue* dst)
{
    if (named_param_count > 1023)
        return false;
    _EMIT_BEGIN(f, WOORT_IROP_KIND_PACKARG);
    op_->m_dst = dst;
    op_->m_packarg_count = named_param_count;
    _EMIT_END();
}

/* ========== 控制流 ========== */

WOORT_NODISCARD bool woort_IR_bind(
    woort_IRFunction* f, woort_IRLabel* label)
{
    /* 记录绑定位置（当前指令列表的末尾就是即将插入的位置） */
    label->m_bound = true;
    label->m_bind_index = (uint32_t)f->m_instructions.m_size;

    _EMIT_BEGIN(f, WOORT_IROP_KIND_LABEL);
    op_->m_label = label;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_jmp(
    woort_IRFunction* f, woort_IRLabel* target)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_JMP);
    op_->m_jump_target = target;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_jcc(
    woort_IRFunction* f, const woort_IRValue* cond, woort_IRLabel* target)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_JCC);
    op_->m_src[0] = cond;
    op_->m_jump_target = target;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_jccz(
    woort_IRFunction* f, const woort_IRValue* cond, woort_IRLabel* target)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_JCCZ);
    op_->m_src[0] = cond;
    op_->m_jump_target = target;
    _EMIT_END();
}

/*
 * 比较跳转：保持原始操作数顺序，不在 IR 层做交换。
 * JCC_GT/JCC_GE/JCC_NE 在字节码发射阶段规范化。
 */
_DEFINE_JCC_CMP(woort_IR_jcc_lt, WOORT_IROP_KIND_JCC_LT)
_DEFINE_JCC_CMP(woort_IR_jcc_le, WOORT_IROP_KIND_JCC_LE)
_DEFINE_JCC_CMP(woort_IR_jcc_eq, WOORT_IROP_KIND_JCC_EQ)
_DEFINE_JCC_CMP(woort_IR_jcc_gt, WOORT_IROP_KIND_JCC_GT)
_DEFINE_JCC_CMP(woort_IR_jcc_ge, WOORT_IROP_KIND_JCC_GE)
_DEFINE_JCC_CMP(woort_IR_jcc_ne, WOORT_IROP_KIND_JCC_NE)

WOORT_NODISCARD bool woort_IR_jifinited(
    woort_IRFunction* f,
    woort_IRStaticIndex cond_idx,
    woort_IRLabel* target)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_JIFINITED);
    op_->m_jifinited_static = cond_idx;
    op_->m_jump_target = target;
    _EMIT_END();
}

/* ========== 返回 ========== */

WOORT_NODISCARD bool woort_IR_ret(
    woort_IRFunction* f, const woort_IRValue* val)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_RET);
    op_->m_src[0] = val;
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_ret_void(woort_IRFunction* f)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_RET_VOID);
    _EMIT_END();
}

/* ========== 陷阱/Panic ========== */

WOORT_NODISCARD bool woort_IR_debugtrap(woort_IRFunction* f)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_DEBUGTRAP);
    _EMIT_END();
}

WOORT_NODISCARD bool woort_IR_panic(
    woort_IRFunction* f, const woort_IRValue* msg)
{
    _EMIT_BEGIN(f, WOORT_IROP_KIND_PANIC);
    op_->m_src[0] = msg;
    _EMIT_END();
}

/* ========== 清理内部宏 ========== */

#undef _EMIT_BEGIN
#undef _EMIT_END
#undef _DEFINE_UNARY_OP
#undef _DEFINE_BINARY_OP
#undef _DEFINE_CALLN
#undef _DEFINE_MKCONT
#undef _DEFINE_DYNBOX
#undef _DEFINE_LDID_VREG
#undef _DEFINE_SDID_VREG
#undef _DEFINE_PUSHID
#undef _DEFINE_JCC_CMP
