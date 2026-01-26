#include "woort_lir_block.h"
#include "woort_lir.h"
#include "woort_vector.h"
#include "woort_hashmap.h"
#include "woort_util.h"

#include <assert.h>

typedef struct woort_LIRBlock_RegisterAliveRange
{
    size_t m_active_form;
    size_t m_active_until;

}woort_LIRBlock_RegisterAliveRange;

void woort_LIRBlock_init(woort_LIRBlock* block, woort_LIRBlockId id)
{
    block->m_id = id;

    woort_vector_init(&block->m_lir_list, sizeof(woort_LIR));
    woort_vector_init(&block->m_prev_blocks, sizeof(woort_LIRBlock*));
    woort_hashmap_init(
        &block->m_block_local_register_active_range,
        sizeof(woort_LIRRegister*),
        sizeof(woort_LIRBlock_RegisterAliveRange),
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    block->m_cond = NULL;
    block->m_cond_next_block = NULL;

    block->m_next_block = NULL;
}
void woort_LIRBlock_deinit(woort_LIRBlock* block)
{
    /*
    Block instance will be freed by LIRFunction.
    We dont need free them here.
    */
    woort_vector_deinit(&block->m_lir_list);
    woort_vector_deinit(&block->m_prev_blocks);
    woort_hashmap_deinit(&block->m_block_local_register_active_range);
}

WOORT_NODISCARD bool woort_LIRBlock_jmp(
    woort_LIRBlock* block,
    woort_LIRBlock* dst_block)
{
    assert(
        block->m_next_block == NULL
        && block->m_cond == NULL
        && block->m_cond_next_block == NULL);

    if (!woort_vector_push_back(&dst_block->m_prev_blocks, 1, &block))
        // Out of memory.
        return false;

    block->m_next_block = dst_block;

    return true;
}

WOORT_NODISCARD bool woort_LIRBlock_cond_jmp(
    woort_LIRBlock* block,
    woort_LIRRegister* cond,
    woort_LIRBlock* dst_block_if_cond,
    woort_LIRBlock* dst_block_else)
{
    assert(
        block->m_next_block == NULL
        && block->m_cond == NULL
        && block->m_cond_next_block == NULL);

    if (!woort_vector_push_back(&dst_block_if_cond->m_prev_blocks, 1, &block))
        // Out of memory.
        return false;

    if (!woort_vector_push_back(&dst_block_else->m_prev_blocks, 1, &block))
    {
        // Out of memory.
        woort_vector_pop_back(&dst_block_if_cond->m_prev_blocks);
        return false;
    }

    block->m_cond = cond;
    block->m_cond_next_block = dst_block_if_cond;
    block->m_next_block = dst_block_else;

    return true;
}

WOORT_NODISCARD bool woort_LIRBlock_pre_emit_register(
    woort_LIRBlock* block, woort_LIRRegister* reg)
{
    woort_LIRBlock_RegisterAliveRange* range;
    switch (woort_hashmap_get_or_emplace(
        &block->m_block_local_register_active_range, &reg, &range))
    {
    case WOORT_HASHMAP_RESULT_OK:
        // New register used in this block.
        range->m_active_form = range->m_active_until = block->m_lir_list.m_size;
        break;
    case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
        // Update last active range.
        range->m_active_until = block->m_lir_list.m_size;
        break;
    case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
        return false;
    }
    return true;
}

WOORT_NODISCARD bool woort_LIRBlock_emit_lir(woort_LIRBlock* block, woort_LIR* lir)
{
    switch (lir->m_opnum_formal)
    {
    case WOORT_LIR_OPNUMFORMAL_CS_R:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_cs_r.m_r))
            // Out of memory.
            return false;
    case WOORT_LIR_OPNUMFORMAL_S_R:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_s_r.m_r))
            // Out of memory.
            return false;
    case WOORT_LIR_OPNUMFORMAL_R:
    case WOORT_LIR_OPNUMFORMAL_R_R:
    case WOORT_LIR_OPNUMFORMAL_R_R_R:
    case WOORT_LIR_OPNUMFORMAL_R_R_COUNT16:
    case WOORT_LIR_OPNUMFORMAL_R_COUNT16:
    case WOORT_LIR_OPNUMFORMAL_R_R_LABEL:
    case WOORT_LIR_OPNUMFORMAL_R_LABEL:
    case WOORT_LIR_OPNUMFORMAL_CS:
    case WOORT_LIR_OPNUMFORMAL_LABEL:
        break;
    }

    if (!woort_vector_push_back(&block->m_lir_list, 1, lir))
    {
        // Out of memory.
        return false;
    }
}

