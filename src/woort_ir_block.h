#pragma once

/*
woort_ir_block.h
*/

#include "woort_vector.h"
#include "woort_ir_operate.h"

typedef struct woort_IR_Block woort_IR_Block;

typedef struct woort_IR_Block_EndWay
{
    /* OPTIONAL */ woort_IR_Block* m_next_block;
    /* OPTIONAL */ woort_IR_Register* m_return_value;

}woort_IR_Block_EndWay;

typedef struct woort_IR_Function woort_IR_Function;

/*
woort_IR_Block

Block 是若干 Op 的合集，是代码连续执行内容的最小单元（即，
一个 Block 内的代码总是按顺序执行，直到执行到块的尾部。

一个块只会以以下方式之一结束：
    * 条件跳转到其他块（或者返回），条件不满足时默认跳转到其他块（或者返回）
    * 直接跳转到其他块（或者返回）
*/
struct woort_IR_Block
{
    woort_IR_Function* m_belong_function;

    woort_Vector /* const woort_IR_Operate_base* */ m_codes;

    // TODO: 此处记录一些活跃性分析和其他乱七八糟相关的状态

    /* OPTIONAL */ woort_IR_Register* m_cond;
    woort_IR_Block_EndWay /* USELESS IF m_cond IS NONE */ m_cond_next;
    woort_IR_Block_EndWay m_normal_next;
};

void woort_IR_Block_NOP(woort_IR_Block* block);
woort_IR_Register* woort_IR_Block_ITOR(woort_IR_Block* block, woort_IR_Register* src);
woort_IR_Register* woort_IR_Block_ITOS(woort_IR_Block* block, woort_IR_Register* src);
woort_IR_Register* woort_IR_Block_RTOI(woort_IR_Block* block, woort_IR_Register* src);
woort_IR_Register* woort_IR_Block_RTOS(woort_IR_Block* block, woort_IR_Register* src);
woort_IR_Register* woort_IR_Block_STOI(woort_IR_Block* block, woort_IR_Register* src);
woort_IR_Register* woort_IR_Block_STOR(woort_IR_Block* block, woort_IR_Register* src);
// void woort_IR_Block_RET(woort_IR_Block* block);
// void woort_IR_Block_RETV(woort_IR_Block* block, woort_IR_Register* src);

woort_IR_Register* woort_IR_Block_ADDI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_SUBI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_MULI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_DIVI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_MODI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_NEGI(woort_IR_Block* block, woort_IR_Register* src);
woort_IR_Register* woort_IR_Block_LTI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_GTI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_LEI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_GEI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_EQI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_NEI(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);

woort_IR_Register* woort_IR_Block_ADDR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_SUBR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_MULR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_DIVR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_MODR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_NEGR(woort_IR_Block* block, woort_IR_Register* src);
woort_IR_Register* woort_IR_Block_LTR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_GTR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_LER(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_GER(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_EQR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_NER(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);

woort_IR_Register* woort_IR_Block_ADDS(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_LTS(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_GTS(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_LES(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_GES(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_EQS(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_NES(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);

woort_IR_Register* woort_IR_Block_LAND(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_LOR(woort_IR_Block* block, woort_IR_Register* lhs, woort_IR_Register* rhs);
woort_IR_Register* woort_IR_Block_LNOT(woort_IR_Block* block, woort_IR_Register* src);

woort_IR_Register* woort_IR_Block_LDIDXVEC(woort_IR_Block* block, woort_IR_Register* vec, woort_IR_Register* idx);
woort_IR_Register* woort_IR_Block_LDIDXVECX(woort_IR_Block* block, woort_IR_Register* vec, woort_IR_Register* idx);
woort_IR_Register* woort_IR_Block_LDIDXSTRUCT(woort_IR_Block* block, woort_IR_Register* st, uint32_t field_idx);
woort_IR_Register* woort_IR_Block_LDIDXSTRING(woort_IR_Block* block, woort_IR_Register* str, woort_IR_Register* idx);
woort_IR_Register* woort_IR_Block_LDIDXDICTI(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key);
woort_IR_Register* woort_IR_Block_LDIDXDICTR(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key);
woort_IR_Register* woort_IR_Block_LDIDXDICTB(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key);
woort_IR_Register* woort_IR_Block_LDIDXDICTX(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key);

void woort_IR_Block_STIDXVECI(woort_IR_Block* block, woort_IR_Register* vec, woort_IR_Register* idx, woort_IR_Register* val);
void woort_IR_Block_STIDXVECR(woort_IR_Block* block, woort_IR_Register* vec, woort_IR_Register* idx, woort_IR_Register* val);
void woort_IR_Block_STIDXVECB(woort_IR_Block* block, woort_IR_Register* vec, woort_IR_Register* idx, woort_IR_Register* val);
void woort_IR_Block_STIDXVECX(woort_IR_Block* block, woort_IR_Register* vec, woort_IR_Register* idx, woort_IR_Register* val);

void woort_IR_Block_STIDXDICTII(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTIR(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTIB(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTIX(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTRI(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTRR(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTRB(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTRX(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTBI(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTBR(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTBB(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTBX(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTXI(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTXR(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTXB(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXDICTXX(woort_IR_Block* block, woort_IR_Register* dict, woort_IR_Register* key, woort_IR_Register* val);

void woort_IR_Block_STIDXMAPII(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPIR(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPIB(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPIX(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPRI(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPRR(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPRB(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPRX(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPBI(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPBR(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPBB(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPBX(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPXI(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPXR(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPXB(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);
void woort_IR_Block_STIDXMAPXX(woort_IR_Block* block, woort_IR_Register* map, woort_IR_Register* key, woort_IR_Register* val);

void woort_IR_Block_STIDSTRUCT(woort_IR_Block* block, uint32_t field_idx, woort_IR_Register* st, woort_IR_Register* val);

void woort_IR_Block_PUSH(woort_IR_Block* block, woort_IR_Register* val);
void woort_IR_Block_POPR(woort_IR_Block* block, uint32_t count);

void woort_IR_Block_PUSHIDXSTBOXI(woort_IR_Block* block, uint32_t field_idx, woort_IR_Register* st);
void woort_IR_Block_PUSHIDXSTBOXR(woort_IR_Block* block, uint32_t field_idx, woort_IR_Register* st);
void woort_IR_Block_PUSHIDXSTBOXB(woort_IR_Block* block, uint32_t field_idx, woort_IR_Register* st);
void woort_IR_Block_PUSHIDXSTBOXX(woort_IR_Block* block, uint32_t field_idx, woort_IR_Register* st);

void woort_IR_Block_UNPACKSTRUCT(woort_IR_Block* block, woort_IR_Register* st, uint32_t field_count);
void woort_IR_Block_UNPACKVEC(woort_IR_Block* block, woort_IR_Register* vec);
void woort_IR_Block_UNPACKVECX(woort_IR_Block* block, woort_IR_Register* vec);

woort_IR_Register* woort_IR_Block_PACKARG(woort_IR_Block* block, uint32_t skip_count);

woort_IR_Register* woort_IR_Block_CALL(woort_IR_Block* block, woort_IR_Register* func, uint32_t argc);
woort_IR_Register* woort_IR_Block_MKVEC(woort_IR_Block* block, uint32_t elem_count);
woort_IR_Register* woort_IR_Block_MKMAP(woort_IR_Block* block, uint32_t pair_count);
woort_IR_Register* woort_IR_Block_MKSTRUCT(woort_IR_Block* block, uint32_t field_count);
woort_IR_Register* woort_IR_Block_MKCLOSURE(woort_IR_Block* block, woort_IR_Register* func, uint32_t upvalue_count);

