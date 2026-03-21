/*
 * woort_ir_regalloc.c
 */

#include "woort_ir_internal.h"

#include <stdlib.h>
#include <string.h>

/*
 * 虚拟寄存器分配
 *
 * 将无限的 SSA 值映射到有限的栈槽位
 */

typedef struct woort_IRRegAlloc
{
    uint32_t* m_value_to_slot;
    uint32_t m_value_count;
    uint32_t m_slot_count;
    uint32_t m_max_slot;
} woort_IRRegAlloc;

WOORT_NODISCARD bool _woort_ir_regalloc_init(
    woort_IRRegAlloc* ra,
    uint32_t value_count)
{
    ra->m_value_to_slot = (uint32_t*)malloc(sizeof(uint32_t) * value_count);
    if (ra->m_value_to_slot == NULL)
    {
        return false;
    }

    for (uint32_t i = 0; i < value_count; ++i)
    {
        ra->m_value_to_slot[i] = UINT32_MAX;
    }

    ra->m_value_count = value_count;
    ra->m_slot_count = 0;
    ra->m_max_slot = 0;

    return true;
}

void _woort_ir_regalloc_drop(woort_IRRegAlloc* ra)
{
    if (ra->m_value_to_slot != NULL)
    {
        free(ra->m_value_to_slot);
    }
}

/*
 * 获取值对应的栈槽位，如果尚未分配则分配一个新的
 */
WOORT_NODISCARD uint32_t _woort_ir_regalloc_get_or_alloc_slot(woort_IRRegAlloc* ra, uint32_t value_index)
{
    if (value_index >= ra->m_value_count)
    {
        return UINT32_MAX;
    }

    if (ra->m_value_to_slot[value_index] != UINT32_MAX)
    {
        return ra->m_value_to_slot[value_index];
    }

    uint32_t slot = ra->m_slot_count++;
    ra->m_value_to_slot[value_index] = slot;
    ra->m_max_slot = (slot > ra->m_max_slot) ? slot : ra->m_max_slot;
    return slot;
}

/*
 * 为函数进行寄存器分配
 */
WOORT_NODISCARD bool _woort_ir_regalloc_function(woort_IRFunction* func, woort_IRRegAlloc* ra)
{
    if (!_woort_ir_regalloc_init(ra, func->m_next_value_index))
    {
        return false;
    }

    for (uint32_t i = 0; i < func->m_param_count; ++i)
    {
        _woort_ir_regalloc_get_or_alloc_slot(ra, func->m_params[i].m_index);
    }

    for (uint32_t block_idx = 0; block_idx < func->m_block_count; ++block_idx)
    {
        woort_IRBlock* block = func->m_blocks[block_idx];

        for (uint32_t instr_idx = 0; instr_idx < block->m_instr_count; ++instr_idx)
        {
            woort_IRInstr* instr = &block->m_instrs[instr_idx];
            if (instr->m_result != NULL)
            {
                _woort_ir_regalloc_get_or_alloc_slot(ra, instr->m_result->m_index);
            }
        }
    }

    for (uint32_t phi_idx = 0; phi_idx < func->m_phi_count; ++phi_idx)
    {
        woort_IRPHI* phi = func->m_phis[phi_idx];
        _woort_ir_regalloc_get_or_alloc_slot(ra, phi->m_value.m_index);
    }

    return true;
}
