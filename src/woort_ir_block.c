#include "woort_ir_block.h"
#include "woort_ir_function.h"
#include "woort_diagnosis.h"

#include <string.h>
#include <assert.h>
#include <stdbool.h>

void woort_IRBlock_init(woort_IRBlock* block, woort_IRFunction* ir_func)
{
    block->m_ir_func = ir_func;

    woort_linklist_init(&block->m_operates, sizeof(woort_IROp));
    woort_linklist_init(&block->m_phis, sizeof(woort_IRPhi));
    woort_vector_init(&block->m_prev_blocks, sizeof(woort_IRBlock*));

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_NOT_FINISHED;
}

void woort_IRBlock_deinit(woort_IRBlock* block)
{
    for (woort_IRPhi* phi = woort_linklist_iter(&block->m_phis);
        phi != NULL;
        phi = woort_linklist_next(phi))
    {
        woort_IRPhi_deinit(phi);
    }

    woort_linklist_deinit(&block->m_operates);
    woort_linklist_deinit(&block->m_phis);
    woort_vector_deinit(&block->m_prev_blocks);
}

WOORT_NODISCARD bool _woort_IRBlock_add_prev(woort_IRBlock* target, woort_IRBlock* from)
{
    return woort_vector_push_back(&target->m_prev_blocks, 1, &from);
}

WOORT_NODISCARD bool woort_IRBlock_br(woort_IRBlock* block, woort_IRBlock* next)
{
    assert(block->m_cond_type == WOORT_IRBLOCK_ENDWAY_NOT_FINISHED);

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_BR;
    block->m_br_next_block = next;

    return _woort_IRBlock_add_prev(next, block);
}

WOORT_NODISCARD bool woort_IRBlock_br_cond(
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

    return
        _woort_IRBlock_add_prev(true_next, block)
        && _woort_IRBlock_add_prev(false_next, block);
}

WOORT_NODISCARD bool woort_IRBlock_br_lt(
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

    return _woort_IRBlock_add_prev(true_next, block)
        && _woort_IRBlock_add_prev(false_next, block);
}

WOORT_NODISCARD bool woort_IRBlock_br_le(
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

    return _woort_IRBlock_add_prev(true_next, block)
        && _woort_IRBlock_add_prev(false_next, block);
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

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_PHI(woort_IRBlock* block, woort_IRPhi** out_phi)
{
    woort_IRValue* const phi_value = woort_IRFunction_phi_value(block->m_ir_func);
    if (phi_value == NULL)
        return NULL;

    if (!woort_linklist_emplace_back(&block->m_phis, (void**)out_phi))
        return NULL;

    woort_IRPhi_init(*out_phi, phi_value);

    return phi_value;
}

void woort_IRPhi_init(woort_IRPhi* phi, woort_IRValue* phi_value)
{
    assert(phi_value->m_source == WOORT_IRVALUE_SOURCE_PHI);

    phi->m_phi_value = phi_value;

    woort_linklist_init(
        &phi->m_records,
        sizeof(woort_IRPhi_ReentryRecord));
}

void woort_IRPhi_deinit(woort_IRPhi* phi)
{
    woort_linklist_deinit(&phi->m_records);
}

WOORT_NODISCARD bool woort_IRPhi_from(
    woort_IRPhi* phi,
    woort_IRBlock* from_block,
    woort_IRValue* val)
{
    woort_IRPhi_ReentryRecord* record;
    if (!woort_linklist_emplace_back(&phi->m_records, (void**)&record))
        return false;

    record->m_from_block = from_block;
    record->m_value = val;

    return true;
}

