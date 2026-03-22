#include "woort_ir_block.h"

#include <string.h>
#include <assert.h>

void woort_IRBlock_init(woort_IRBlock* block)
{
    woort_linklist_init(&block->m_operates, sizeof(woort_IROp));
    woort_vector_init(&block->m_prev_blocks, sizeof(woort_IRBlock*));

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_NOT_FINISHED;
}

void woort_IRBlock_deinit(woort_IRBlock* block)
{
    woort_linklist_deinit(&block->m_operates);
    woort_vector_deinit(&block->m_prev_blocks);
}

static void _woort_IRBlock_add_prev(woort_IRBlock* target, woort_IRBlock* from)
{
    woort_vector_push_back(&target->m_prev_blocks, 1, &from);
}

void woort_IRBlock_br(woort_IRBlock* block, woort_IRBlock* next)
{
    assert(block->m_cond_type == WOORT_IRBLOCK_ENDWAY_NOT_FINISHED);

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_BR;
    block->m_br_next_block = next;
    _woort_IRBlock_add_prev(next, block);
}

void woort_IRBlock_br_cond(
    woort_IRBlock* block,
    woort_IRValue* cond,
    woort_IRBlock* true_next,
    woort_IRBlock* false_next)
{
    assert(block->m_cond_type == WOORT_IRBLOCK_ENDWAY_NOT_FINISHED);

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_BR_COND;
    block->m_br_cond_value = cond;
    block->m_br_next_block_cond_true = true_next;
    block->m_br_next_block_cond_false = false_next;
    _woort_IRBlock_add_prev(true_next, block);
    _woort_IRBlock_add_prev(false_next, block);
}

void woort_IRBlock_br_lt(
    woort_IRBlock* block,
    woort_IRValue* a,
    woort_IRValue* b,
    woort_IRBlock* true_next,
    woort_IRBlock* false_next)
{
    assert(block->m_cond_type == WOORT_IRBLOCK_ENDWAY_NOT_FINISHED);

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT;
    block->m_br_compare_values[0] = a;
    block->m_br_compare_values[1] = b;
    block->m_br_next_block_compare_true = true_next;
    block->m_br_next_block_compare_false = false_next;
    _woort_IRBlock_add_prev(true_next, block);
    _woort_IRBlock_add_prev(false_next, block);
}

void woort_IRBlock_br_le(
    woort_IRBlock* block,
    woort_IRValue* a,
    woort_IRValue* b,
    woort_IRBlock* true_next,
    woort_IRBlock* false_next)
{
    assert(block->m_cond_type == WOORT_IRBLOCK_ENDWAY_NOT_FINISHED);

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE;
    block->m_br_compare_values[0] = a;
    block->m_br_compare_values[1] = b;
    block->m_br_next_block_compare_true = true_next;
    block->m_br_next_block_compare_false = false_next;
    _woort_IRBlock_add_prev(true_next, block);
    _woort_IRBlock_add_prev(false_next, block);
}

void woort_IRBlock_ret(woort_IRBlock* block, woort_IRValue* val)
{
    assert(block->m_cond_type == WOORT_IRBLOCK_ENDWAY_NOT_FINISHED);

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_RET;
    block->m_ret_value_may_null = val;
}

void woort_IRBlock_ret_void(woort_IRBlock* block)
{
    assert(block->m_cond_type == WOORT_IRBLOCK_ENDWAY_NOT_FINISHED);

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_RET;
    block->m_ret_value_may_null = NULL;
}

