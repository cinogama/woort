#pragma once

/*
 * woort_ir_block.h
 */

#include "woort_ir_inst.h"
#include <stdbool.h>

typedef struct woort_IRFunction woort_IRFunction;

typedef struct woort_IRBlockPredInfo
{
    woort_IRBlock**     m_preds;
    uint32_t            m_pred_count;
    uint32_t            m_pred_capacity;

} woort_IRBlockPredInfo;

typedef struct woort_IRBlock
{
    woort_IRFunction*       m_function;
    uint32_t                m_id;
    woort_IRInst*           m_first;
    woort_IRInst*           m_last;
    woort_IRBlock*          m_next;
    woort_IRBlock*          m_prev;

    woort_IRBlockPredInfo   m_pred_info;
    woort_IRBlock*          m_succs[2];
    uint32_t                m_succ_count;

    woort_IRInst*           m_phis;
    bool                    m_is_sealed;

} woort_IRBlock;

static inline bool woort_IRBlock_is_terminated(woort_IRBlock* block)
{
    return block->m_last != NULL && woort_IRInst_is_terminator(block->m_last);
}