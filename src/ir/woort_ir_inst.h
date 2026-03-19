#pragma once

/*
 * woort_ir_inst.h
 */

#include "woort_ir_value.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum woort_IROpcode
{
    WOORT_IR_OP_RET,
    WOORT_IR_OP_BR,
    WOORT_IR_OP_COND_BR,

    WOORT_IR_OP_ADD_I,
    WOORT_IR_OP_SUB_I,
    WOORT_IR_OP_MUL_I,
    WOORT_IR_OP_DIV_I,
    WOORT_IR_OP_MOD_I,
    WOORT_IR_OP_NEG_I,

    WOORT_IR_OP_ADD_R,
    WOORT_IR_OP_SUB_R,
    WOORT_IR_OP_MUL_R,
    WOORT_IR_OP_DIV_R,
    WOORT_IR_OP_MOD_R,
    WOORT_IR_OP_NEG_R,

    WOORT_IR_OP_LT_I,
    WOORT_IR_OP_LE_I,
    WOORT_IR_OP_GT_I,
    WOORT_IR_OP_GE_I,
    WOORT_IR_OP_EQ_I,
    WOORT_IR_OP_NE_I,

    WOORT_IR_OP_LT_R,
    WOORT_IR_OP_LE_R,
    WOORT_IR_OP_GT_R,
    WOORT_IR_OP_GE_R,
    WOORT_IR_OP_EQ_R,
    WOORT_IR_OP_NE_R,

    WOORT_IR_OP_ADD_S,
    WOORT_IR_OP_LT_S,
    WOORT_IR_OP_LE_S,
    WOORT_IR_OP_GT_S,
    WOORT_IR_OP_GE_S,
    WOORT_IR_OP_EQ_S,
    WOORT_IR_OP_NE_S,

    WOORT_IR_OP_AND,
    WOORT_IR_OP_OR,
    WOORT_IR_OP_NOT,

    WOORT_IR_OP_MKVEC,
    WOORT_IR_OP_MKMAP,
    WOORT_IR_OP_MKSTRUCT,

    WOORT_IR_OP_LDVEC,
    WOORT_IR_OP_LDSTR,
    WOORT_IR_OP_LDSTRUCT,
    WOORT_IR_OP_LDMAP_I,
    WOORT_IR_OP_LDMAP_R,
    WOORT_IR_OP_LDMAP_B,
    WOORT_IR_OP_LDMAP_X,

    WOORT_IR_OP_STVEC_I,
    WOORT_IR_OP_STVEC_R,
    WOORT_IR_OP_STVEC_B,
    WOORT_IR_OP_STVEC_X,

    WOORT_IR_OP_STMAP_I_I,
    WOORT_IR_OP_STMAP_I_R,
    WOORT_IR_OP_STMAP_I_B,
    WOORT_IR_OP_STMAP_I_X,
    WOORT_IR_OP_STMAP_R_I,
    WOORT_IR_OP_STMAP_R_R,
    WOORT_IR_OP_STMAP_R_B,
    WOORT_IR_OP_STMAP_R_X,
    WOORT_IR_OP_STMAP_B_I,
    WOORT_IR_OP_STMAP_B_R,
    WOORT_IR_OP_STMAP_B_B,
    WOORT_IR_OP_STMAP_B_X,
    WOORT_IR_OP_STMAP_X_I,
    WOORT_IR_OP_STMAP_X_R,
    WOORT_IR_OP_STMAP_X_B,
    WOORT_IR_OP_STMAP_X_X,

    WOORT_IR_OP_STSTRUCT,

    WOORT_IR_OP_CALL,
    WOORT_IR_OP_MKCLOSURE,

    WOORT_IR_OP_CAST_I_TO_R,
    WOORT_IR_OP_CAST_R_TO_I,
    WOORT_IR_OP_BOX_DYN,
    WOORT_IR_OP_UNBOX_DYN,

    WOORT_IR_OP_PHI,

    WOORT_IR_OP_CONST_INT,
    WOORT_IR_OP_CONST_REAL,
    WOORT_IR_OP_CONST_BOOL,
    WOORT_IR_OP_CONST_STR,
    WOORT_IR_OP_CONST_NULL,

    WOORT_IR_OP_PARAM,

} woort_IROpcode;

typedef struct woort_IRInst
{
    woort_IROpcode          m_op;
    woort_IRValue*          m_result;
    woort_IRValue**         m_operands;
    uint32_t                m_operand_count;
    woort_IRInst*           m_next;
    woort_IRInst*           m_prev;
    woort_IRBlock**         m_phi_src_blocks;
    uint32_t                m_phi_incoming_count;

} woort_IRInst;

static inline bool woort_IRInst_is_terminator(woort_IRInst* inst)
{
    return inst->m_op == WOORT_IR_OP_RET
        || inst->m_op == WOORT_IR_OP_BR
        || inst->m_op == WOORT_IR_OP_COND_BR;
}