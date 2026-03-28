#pragma once

/*
 * woort_ir_block.h
 *
 * 新 IR 接口：
 *   - woort_IRLabel: 跳转目标
 *   - woort_IRBlock: 内部概念，finish() 时从 Label/跳转自动切分
 *   - woort_IR_*: 指令发射函数（在 IRFunction 上操作）
 *
 * 用户通过 woort_IR_* 函数向 IRFunction 追加指令。
 * 控制流通过 Label + 显式 JMP/JCC 表达。
 */

#include "woort_ir_value.h"
#include "woort_ir_op.h"
#include "woort_vector.h"
#include "woort_bitset.h"
#include "woort_diagnosis.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct woort_IRFunction woort_IRFunction;

/*
 * Label：跳转目标
 *
 * 由 woort_IRFunction_new_label 创建。
 * 用 woort_IR_bind 绑定到当前指令位置。
 * 在 finish 阶段解析为字节码地址。
 */
struct woort_IRLabel
{
    uint32_t m_id;

    /* finish 阶段填充 */
    bool m_bound;           /* 是否已绑定 */
    uint32_t m_bind_index;  /* 在指令列表中的位置（绑定的 IROp 索引） */
    uint32_t m_block_index; /* 所属 block 编号 */
};

/*
 * 基本块（内部数据结构）
 *
 * 在 finish() 时由框架根据 Label 和跳转指令自动切分。
 * 用户不直接创建或操作 Block。
 */
typedef struct woort_IRBlock
{
    /* 在指令列表中的范围 [m_begin, m_end) */
    uint32_t m_begin;
    uint32_t m_end;

    /* CFG 边 */
    woort_Vector /* uint32_t (block index) */ m_successors;
    woort_Vector /* uint32_t (block index) */ m_predecessors;

    /* 活跃性分析 */
    woort_Bitset m_use;
    woort_Bitset m_def;
    woort_Bitset m_live_in;
    woort_Bitset m_live_out;

    /* Dominator 分析 */
    int32_t m_idom;     /* immediate dominator block index, -1 if none */
    uint32_t m_dom_depth;

    /* 循环信息 */
    bool m_is_in_loop;
    int32_t m_loop_header;  /* loop header block index, -1 if not in loop */

    /* 常量加载放置 */
    woort_Vector /* _woort_ConstLoadInfo */ m_const_loads;

    /* 字节码发射结果 */
    woort_Vector /* woort_Bytecode */ m_bytecodes;

} woort_IRBlock;

void _woort_IRBlock_init(woort_IRBlock* block);
void _woort_IRBlock_deinit(woort_IRBlock* block);

/* ========== 指令发射 API ========== */

/*
 * 所有 woort_IR_* 函数向 f 的线性指令列表追加一条 IROp。
 * 返回 false 表示 OOM。
 */

/* --- 数据移动 --- */
WOORT_NODISCARD bool woort_IR_MOV(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_LOAD_CONST(woort_IRFunction* f, woort_IRValue* dst, woort_IRConstantIndex idx);
WOORT_NODISCARD bool woort_IR_LOAD(woort_IRFunction* f, woort_IRValue* dst, woort_IRStaticIndex idx);
WOORT_NODISCARD bool woort_IR_STORE(woort_IRFunction* f, woort_IRStaticIndex idx, woort_IRValue* src);

/* --- 栈操作 --- */
WOORT_NODISCARD bool woort_IR_PUSHCHK(woort_IRFunction* f, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_POP(woort_IRFunction* f, woort_IRValue* dst);
WOORT_NODISCARD bool woort_IR_POPR(woort_IRFunction* f, uint32_t count);
WOORT_NODISCARD bool woort_IR_POPRS(woort_IRFunction* f, woort_IRValue* count_src);

/* --- 类型转换 --- */
WOORT_NODISCARD bool woort_IR_ITOR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_ITOS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_RTOI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_RTOS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_STOI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_STOR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

/* --- 函数调用 --- */
WOORT_NODISCARD bool woort_IR_CALLNWO(
    woort_IRFunction* f, woort_IRConstantIndex target,
    uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);
WOORT_NODISCARD bool woort_IR_CALLNFP(
    woort_IRFunction* f, woort_IRConstantIndex target,
    uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);
WOORT_NODISCARD bool woort_IR_CALLNJIT(
    woort_IRFunction* f, woort_IRConstantIndex target,
    uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);
WOORT_NODISCARD bool woort_IR_CALL(
    woort_IRFunction* f, woort_IRValue* func_val,
    uint32_t argc, /* OPTIONAL */ woort_IRValue* dst);

/* --- 闭包/容器 --- */
WOORT_NODISCARD bool woort_IR_MKCLOSURE(
    woort_IRFunction* f, woort_IRValue* dst, uint32_t elem_count, woort_IRConstantIndex func_idx);
WOORT_NODISCARD bool woort_IR_MKVEC(woort_IRFunction* f, woort_IRValue* dst, uint32_t elem_count);
WOORT_NODISCARD bool woort_IR_MKMAP(woort_IRFunction* f, woort_IRValue* dst, uint32_t kvpair_count);
WOORT_NODISCARD bool woort_IR_MKSTRUCT(woort_IRFunction* f, woort_IRValue* dst, uint32_t elem_count);

/* --- 动态类型 --- */
WOORT_NODISCARD bool woort_IR_BOXDYN(woort_IRFunction* f, woort_IRValue* dst, uint8_t typ, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_UNBOXDYN(woort_IRFunction* f, woort_IRValue* dst, uint8_t typ, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_CHECKDYN(woort_IRFunction* f, woort_IRValue* dst, uint8_t typ, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_PUSHBOXDYN(woort_IRFunction* f, uint8_t typ, woort_IRValue* src);

/* --- 整数算术 (dst = src1 op src2) --- */
WOORT_NODISCARD bool woort_IR_ADDI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_SUBI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_MULI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_DIVI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_MODI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_NEGI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

/* --- 整数比较 --- */
WOORT_NODISCARD bool woort_IR_LTI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_GTI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_LEI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_GEI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_EQI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_NEI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);

/* --- 实数算术 --- */
WOORT_NODISCARD bool woort_IR_ADDR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_SUBR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_MULR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_DIVR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_MODR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_NEGR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

/* --- 实数比较 --- */
WOORT_NODISCARD bool woort_IR_LTR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_GTR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_LER(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_GER(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_EQR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_NER(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);

/* --- 字符串 --- */
WOORT_NODISCARD bool woort_IR_ADDS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_LTS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_GTS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_LES(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_GES(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_EQS(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_NES(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);

/* --- 逻辑 --- */
WOORT_NODISCARD bool woort_IR_LAND(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_LOR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* a, woort_IRValue* b);
WOORT_NODISCARD bool woort_IR_LNOT(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

/* --- 索引加载 --- */
WOORT_NODISCARD bool woort_IR_LDIDXVEC(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
WOORT_NODISCARD bool woort_IR_LDIDXVECX(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
WOORT_NODISCARD bool woort_IR_LDIDXSTRUCT(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, uint32_t idx);
WOORT_NODISCARD bool woort_IR_LDIDXSTRING(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);

WOORT_NODISCARD bool woort_IR_LDIDXDICTI(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
WOORT_NODISCARD bool woort_IR_LDIDXDICTR(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
WOORT_NODISCARD bool woort_IR_LDIDXDICTB(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);
WOORT_NODISCARD bool woort_IR_LDIDXDICTX(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* container, woort_IRValue* idx);

/* --- 索引存储 (不返回值) --- */
WOORT_NODISCARD bool woort_IR_SDIDXVECI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXVECR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXVECB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXVECX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

WOORT_NODISCARD bool woort_IR_SDIDXDICTII(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTIR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTIB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTIX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTRI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTRR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTRB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTRX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTBI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTBR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTBB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTBX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTXI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTXR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTXB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXDICTXX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

WOORT_NODISCARD bool woort_IR_SDIDXMAPII(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPIR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPIB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPIX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPRI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPRR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPRB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPRX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPBI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPBR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPBB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPBX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPXI(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPXR(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPXB(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_SDIDXMAPXX(woort_IRFunction* f, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

WOORT_NODISCARD bool woort_IR_SDIDXSTRUCT(woort_IRFunction* f, woort_IRValue* c, uint32_t idx, woort_IRValue* val);

/* --- 解包 --- */
WOORT_NODISCARD bool woort_IR_UNPACKSTRUCT(woort_IRFunction* f, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_UNPACKVEC(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);
WOORT_NODISCARD bool woort_IR_UNPACKVECX(woort_IRFunction* f, woort_IRValue* dst, woort_IRValue* src);

/* --- 结构体字段推栈 --- */
WOORT_NODISCARD bool woort_IR_PUSHIDXSTRUCT(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXI(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXR(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXB(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);
WOORT_NODISCARD bool woort_IR_PUSHIDXSTBOXX(woort_IRFunction* f, woort_IRValue* src, uint32_t idx);

/* ============ 控制流 ============ */

/* 绑定 Label 到当前位置 */
WOORT_NODISCARD bool woort_IR_bind(woort_IRFunction* f, woort_IRLabel* label);

/* 无条件跳转 */
WOORT_NODISCARD bool woort_IR_jmp(woort_IRFunction* f, woort_IRLabel* target);

/* 条件跳转: if (cond != 0) goto target */
WOORT_NODISCARD bool woort_IR_jcc(woort_IRFunction* f, woort_IRValue* cond, woort_IRLabel* target);

/* 条件跳转: if (cond == 0) goto target */
WOORT_NODISCARD bool woort_IR_jccz(woort_IRFunction* f, woort_IRValue* cond, woort_IRLabel* target);

/* 比较跳转 */
WOORT_NODISCARD bool woort_IR_jcc_lt(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
WOORT_NODISCARD bool woort_IR_jcc_le(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
WOORT_NODISCARD bool woort_IR_jcc_eq(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
WOORT_NODISCARD bool woort_IR_jcc_gt(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
WOORT_NODISCARD bool woort_IR_jcc_ge(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);
WOORT_NODISCARD bool woort_IR_jcc_ne(woort_IRFunction* f, woort_IRValue* a, woort_IRValue* b, woort_IRLabel* target);

/* ============ 返回 ============ */

WOORT_NODISCARD bool woort_IR_ret(woort_IRFunction* f, woort_IRValue* val);
WOORT_NODISCARD bool woort_IR_ret_void(woort_IRFunction* f);
