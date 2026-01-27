#include "woort_lir_block.h"
#include "woort_lir.h"
#include "woort_vector.h"
#include "woort_hashmap.h"
#include "woort_util.h"
#include "woort_log.h"

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
    case WOORT_LIR_OPNUMFORMAL_R:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r.m_r))
            // Out of memory.
            return false;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r.m_r1)
            || !woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r.m_r2))
            // Out of memory.
            return false;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R_R:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r_r.m_r1)
            || !woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r_r.m_r2)
            || !woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r_r.m_r3))
            // Out of memory.
            return false;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R_COUNT:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r_count.m_r1)
            || !woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r_count.m_r2))
            // Out of memory.
            return false;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_COUNT:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_count.m_r))
            // Out of memory.
            return false;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_R_LABEL:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r_label.m_r1)
            || !woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_r_label.m_r2))
            // Out of memory.
            return false;
        break;
    case WOORT_LIR_OPNUMFORMAL_R_LABEL:
        if (!woort_LIRBlock_pre_emit_register(block, lir->m_opnums.m_r_label.m_r))
            // Out of memory.
            return false;
        break;
    case WOORT_LIR_OPNUMFORMAL_LABEL:
        break;
    default:
        WOORT_DEBUG("Unknown formal.");
        abort();
    }

    if (!woort_vector_push_back(&block->m_lir_list, 1, lir))
    {
        // Out of memory.
        return false;
    }
}

WOORT_NODISCARD bool _woort_LIRBlock_scan_block_list(
    woort_LIRBlock* block,
    woort_Vector* modify_blocks)
{
    // BFS
    woort_LinkList block_stack;
    woort_linklist_init(&block_stack, sizeof(woort_LIRBlock*));

    if (!woort_linklist_push_back(&block_stack, &modify_blocks))
    {
        // Out of memory.

        woort_linklist_deinit(&block_stack);
        return false;
    }

    woort_HashMap walked_map;
    woort_hashmap_init(
        &walked_map,
        sizeof(woort_LIRBlock*),
        0,
        woort_util_ptr_hash,
        woort_util_ptr_equal);

    bool ok = true;
    do
    {
        woort_LIRBlock* const current_block;

        if (!woort_linklist_front(&block_stack, &current_block))
            // Finished.
            break;

        (void)woort_linklist_pop_front(&block_stack);

        int _useless = 0;
        switch (woort_hashmap_insert(&walked_map, &current_block, &_useless))
        {
        case WOORT_HASHMAP_RESULT_OK:
            if (woort_vector_push_back(modify_blocks, 1, &current_block))
            {
                // Walk through all next block.
                if ((current_block->m_cond_next_block != NULL
                    && !woort_linklist_push_back(
                        &block_stack, &current_block->m_cond_next_block))
                    || (current_block->m_next_block != NULL
                        && !woort_linklist_push_back(
                            &block_stack, &current_block->m_next_block)))
                {
                    // Out of memory.
                    ok = false;
                }
            }
            else
                // Out of memory.
                ok = false;
            break;
        case WOORT_HASHMAP_RESULT_ALREADY_EXIST:
            break;
        case WOORT_HASHMAP_RESULT_OUT_OF_MEMORY:
            // Out of memory.
            ok = false;
            break;
        }
    } while (ok);

    woort_hashmap_deinit(&walked_map);
    woort_linklist_deinit(&block_stack);
    return ok;
}

typedef struct woort_LIRBlock_BlockedRegisterActivePoint
{
    size_t m_block_id;
    size_t m_lir_id;

} woort_LIRBlock_BlockedRegisterActivePoint;

WOORT_NODISCARD bool woort_LIRBlock_register_assign(
    woort_LIRBlock* entry_block)
{
    // 1. Get all nodes.
    woort_Vector all_blocks;
    woort_vector_init(&all_blocks, sizeof(woort_LIRBlock*));

    if (!_woort_LIRBlock_scan_block_list(entry_block, &all_blocks))
    {
        // Out of memory.
        woort_vector_deinit(&all_blocks);
        return false;
    }

    // 2. Scan all block to mark all register.
    do
    {
        size_t block_id = 0;
        for (woort_LIRBlock
            ** index = (woort_LIRBlock**)all_blocks.m_data,
            ** const end = index + all_blocks.m_size;
            index != end;
            ++index, ++block_id)
        {
            woort_LIRBlock* const this_block = *index;

            this_block;
        }

    } while (true);

    woort_vector_deinit(&all_blocks);
    return true;
}