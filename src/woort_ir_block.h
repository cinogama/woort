#pragma once

/*
woort_ir_block.h
*/

#include <stdint.h>
#include <stddef.h>

#include "woort_ir_value.h"
#include "woort_ir_op.h"
#include "woort_linklist.h"
#include "woort_vector.h"

typedef struct woort_IRBlock woort_IRBlock;
typedef struct woort_IRFunction woort_IRFunction;

typedef struct woort_IRPhi_ReentryRecord
{
    woort_IRBlock* m_from_block;
    woort_IRValue* m_value;

}woort_IRPhi_ReentryRecord;

typedef struct woort_IRPhi 
{
    woort_IRValue* m_phi_value;

    /*
    Phi 节点的 m_records 数量和来源块应当和所属的 Block 的 m_prev_blocks
    保持一致
    */
    woort_LinkList /* woort_IRPhi_ReentryRecord */ m_records;

} woort_IRPhi;

typedef enum woort_IRBlock_EndWay
{
    WOORT_IRBLOCK_ENDWAY_NOT_FINISHED,

    WOORT_IRBLOCK_ENDWAY_BR,
    WOORT_IRBLOCK_ENDWAY_BR_COND,
    WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT,
    WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE,
    WOORT_IRBLOCK_ENDWAY_RET,

}woort_IRBlock_EndWay;

struct woort_IRBlock {
    woort_IRFunction* m_ir_func;

    woort_LinkList /* woort_IROp */ m_operates;
    woort_LinkList /* woort_IRPhi */ m_phis;

    woort_Vector /* woort_IRBlock* */ m_prev_blocks;

    woort_IRBlock_EndWay m_cond_type;

    union
    {
        woort_IRBlock* m_br_next_block;
        /* OPTIONAL */ woort_IRValue* m_ret_value_may_null;
        struct
        {
            woort_IRValue* m_br_cond_value;
            woort_IRBlock* m_br_next_block_cond_true;
            woort_IRBlock* m_br_next_block_cond_false;
        };
        struct
        {
            woort_IRValue* m_br_compare_values[2];
            woort_IRBlock* m_br_next_block_compare_true;
            woort_IRBlock* m_br_next_block_compare_false;
        };
    };

};

void woort_IRBlock_init(woort_IRBlock* block, woort_IRFunction* ir_func);
void woort_IRBlock_deinit(woort_IRBlock* block);

void woort_IRBlock_br(
    woort_IRBlock* block,
    woort_IRBlock* next);
void woort_IRBlock_br_cond(
    woort_IRBlock* block,
    woort_IRValue* cond,
    woort_IRBlock* true_next,
    woort_IRBlock* false_next);
void woort_IRBlock_br_lt(
    woort_IRBlock* block,
    woort_IRValue* a,
    woort_IRValue* b,
    woort_IRBlock* true_next,
    woort_IRBlock* false_next);
void woort_IRBlock_br_le(
    woort_IRBlock* block,
    woort_IRValue* a,
    woort_IRValue* b,
    woort_IRBlock* true_next,
    woort_IRBlock* false_next);
#define woort_IRBlock_br_gt(block, a, b, true_next, false_next)\
    woort_IRBlock_br_lt(block, b, a, false_next, true_next)
#define woort_IRBlock_br_ge(block, a, b, true_next, false_next)\
    woort_IRBlock_br_le(block, b, a, false_next, true_next)

void woort_IRBlock_ret(
    woort_IRBlock* block, woort_IRValue* val);
void woort_IRBlock_ret_void(woort_IRBlock* block);

woort_IRValue* woort_IRBlock_PHI(woort_IRBlock* block, woort_IRPhi** out_phi);

/*
如果一个 Block 有 PHI 节点，那么所有 br 到此 Block 其他 Block 必须通过此声明
传递目标 Block 的所有 PHI 来源值，IRCompiler 在 finish 阶段负责检查。
*/
void woort_IRPhi_from(
    woort_IRPhi* phi,
    woort_IRBlock* from_block,
    woort_IRValue* val);

woort_IRValue* woort_IRBlock_LOAD(woort_IRBlock* b, woort_IRStaticIndex idx);
void woort_IRBlock_STORE(woort_IRBlock* b, woort_IRStaticIndex idx, woort_IRValue* val);

void woort_IRBlock_PUSHCHK(woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_POP(woort_IRBlock* b);

void woort_IRBlock_POPR(woort_IRBlock* b, uint32_t count);
void woort_IRBlock_POPRS(woort_IRBlock* b, woort_IRValue* val);

woort_IRValue* woort_IRBlock_ITOR(woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_ITOS(woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_RTOI(woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_RTOS(woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_STOI(woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_STOR(woort_IRBlock* b, woort_IRValue* val);

void woort_IRBlock_CALLNWO(
    woort_IRBlock* b,
    woort_IRConstantIndex f,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val);
void woort_IRBlock_CALLNFP(
    woort_IRBlock* b,
    woort_IRConstantIndex f,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val);
void woort_IRBlock_CALLNJIT(
    woort_IRBlock* b,
    woort_IRConstantIndex f,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val);
void woort_IRBlock_CALL(
    woort_IRBlock* b,
    woort_IRValue* f_val,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val);

woort_IRValue* woort_IRBlock_MKVEC(woort_IRBlock* b, uint32_t elem_count);
woort_IRValue* woort_IRBlock_MKMAP(woort_IRBlock* b, uint32_t kvpair_count);
woort_IRValue* woort_IRBlock_MKSTRUCT(woort_IRBlock* b, uint32_t elem_count);

woort_IRValue* woort_IRBlock_BOXDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val);
woort_IRValue* woort_IRBlock_UNBOXDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val);
woort_IRValue* woort_IRBlock_CHECKDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val);
void woort_IRBlock_PUSHBOXDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val);

woort_IRValue* woort_IRBlock_ADDI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_SUBI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_MULI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_DIVI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_MODI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_NEGI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_NEGI(
    woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_LTI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_GTI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_LEI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_GEI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_EQI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_NEI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);

woort_IRValue* woort_IRBlock_ADDR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_SUBR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_MULR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_DIVR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_MODR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_NEGR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_NEGR(
    woort_IRBlock* b, woort_IRValue* val);
woort_IRValue* woort_IRBlock_LTR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_GTR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_LER(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_GER(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_EQR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_NER(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);

woort_IRValue* woort_IRBlock_ADDS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_LTS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_GTS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_LES(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_GES(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_EQS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_NES(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);

woort_IRValue* woort_IRBlock_LAND(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_LOR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2);
woort_IRValue* woort_IRBlock_LNOT(
    woort_IRBlock* b, woort_IRValue* val);

woort_IRValue* woort_IRBlock_LDIDXVEC(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx);
woort_IRValue* woort_IRBlock_LDIDXVECX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx);
woort_IRValue* woort_IRBlock_LDIDXSTRUCT(
    woort_IRBlock* b, woort_IRValue* c, uint32_t idx);
woort_IRValue* woort_IRBlock_LDIDXSTRING(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx);

woort_IRValue* woort_IRBlock_LDIDXDICTI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx);
woort_IRValue* woort_IRBlock_LDIDXDICTR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx);
woort_IRValue* woort_IRBlock_LDIDXDICTB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx);
woort_IRValue* woort_IRBlock_LDIDXDICTX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx);

void woort_IRBlock_SDIDXVECI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXVECR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXVECB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXVECX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXDICTII(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTIR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTIB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTIX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXDICTRI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTRR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTRB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTRX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXDICTBI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTBR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTBB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTBX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXDICTXI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTXR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTXB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXDICTXX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXMAPII(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPIR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPIB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPIX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXMAPRI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPRR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPRB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPRX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXMAPBI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPBR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPBB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPBX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXMAPXI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPXR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPXB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);
void woort_IRBlock_SDIDXMAPXX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val);

void woort_IRBlock_SDIDXSTRUCT(
    woort_IRBlock* b, woort_IRValue* c, uint32_t idx, woort_IRValue* val);

void woort_IRBlock_UNPACKSTRUCT(woort_IRBlock* b, woort_IRValue* c);
woort_IRValue* woort_IRBlock_UNPACKVEC(woort_IRBlock* b, woort_IRValue* c);
woort_IRValue* woort_IRBlock_UNPACKVECX(woort_IRBlock* b, woort_IRValue* c);

void woort_IRBlock_PUSHIDXSTRUCT(woort_IRBlock* b, woort_IRValue* c, uint32_t idx);

void woort_IRBlock_PUSHIDXSTBOXI(woort_IRBlock* b, woort_IRValue* c, uint32_t idx);
void woort_IRBlock_PUSHIDXSTBOXR(woort_IRBlock* b, woort_IRValue* c, uint32_t idx);
void woort_IRBlock_PUSHIDXSTBOXB(woort_IRBlock* b, woort_IRValue* c, uint32_t idx);
void woort_IRBlock_PUSHIDXSTBOXX(woort_IRBlock* b, woort_IRValue* c, uint32_t idx);

// Phi
void woort_IRPhi_init(woort_IRPhi* phi, woort_IRBlock* b);
void woort_IRPhi_deinit(woort_IRPhi* phi);