#include "woort_lir_block.h"
#include "woort_vector.h"

#include <assert.h>

void woort_LIRBlock_init(woort_LIRBlock* block)
{
    woort_vector_init(&block->m_lir_list, sizeof(woort_LIR));
    woort_vector_init(&block->m_prev_blocks, sizeof(woort_LIRBlock*));

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
}

void woort_LIRBlock_jmp(
    woort_LIRBlock* block,
    woort_LIRBlock* dst_block)
{
    assert(
        block->m_next_block == NULL
        && block->m_cond == NULL
        && block->m_cond_next_block == NULL);
    block->m_next_block = dst_block;
}

void woort_LIRBlock_cond_jmp(
    woort_LIRBlock* block,
    woort_LIRRegister* cond,
    woort_LIRBlock* dst_block_if_cond,
    woort_LIRBlock* dst_block_else)
{
    assert(
        block->m_next_block == NULL
        && block->m_cond == NULL
        && block->m_cond_next_block == NULL);

    block->m_cond = cond;
    block->m_cond_next_block = dst_block_if_cond;
    block->m_next_block = dst_block_else;
}
