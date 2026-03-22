#include "woort_ir_block.h"

void woort_IRBlock_init(woort_IRBlock* block)
{
    woort_linklist_init(&block->m_operates, sizeof(woort_IROp));
}

void woort_IRBlock_deinit(woort_IRBlock* block)
{
    woort_linklist_deinit(&block->m_operates);
}