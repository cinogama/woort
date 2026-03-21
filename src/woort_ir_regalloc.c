/*
 * woort_ir_regalloc.c
 */

#include "woort_ir_internal.h"

#include <stdlib.h>
#include <string.h>

/*
 * 虚拟寄存器分配上下文
 */
typedef struct woort_IRRegAllocCtx
{
    woort_IRFunction* m_func;
    
    int32_t* m_value_to_slot;
    uint32_t m_value_count;
    
    int32_t m_next_slot;
    int32_t m_max_slot;
} woort_IRRegAllocCtx;

WOORT_NODISCARD static bool _woort_ir_regalloc_ctx_init(
    woort_IRRegAllocCtx* ctx,
    woort_IRFunction* func)
{
    ctx->m_func = func;
    ctx->m_value_count = func->m_next_value_index;
    ctx->m_next_slot = -1;
    ctx->m_max_slot = 0;
    
    ctx->m_value_to_slot = (int32_t*)malloc(sizeof(int32_t) * ctx->m_value_count);
    if (ctx->m_value_to_slot == NULL)
    {
        return false;
    }
    
    for (uint32_t i = 0; i < ctx->m_value_count; ++i)
    {
        ctx->m_value_to_slot[i] = INT32_MAX;
    }
    
    return true;
}

static void _woort_ir_regalloc_ctx_drop(woort_IRRegAllocCtx* ctx)
{
    if (ctx->m_value_to_slot != NULL)
    {
        free(ctx->m_value_to_slot);
    }
}

WOORT_NODISCARD static int32_t _woort_ir_regalloc_get_slot(woort_IRRegAllocCtx* ctx, uint32_t value_index)
{
    if (value_index >= ctx->m_value_count)
    {
        return INT32_MAX;
    }
    return ctx->m_value_to_slot[value_index];
}

WOORT_NODISCARD static int32_t _woort_ir_regalloc_alloc_slot(woort_IRRegAllocCtx* ctx, uint32_t value_index)
{
    if (value_index >= ctx->m_value_count)
    {
        return INT32_MAX;
    }
    
    if (ctx->m_value_to_slot[value_index] != INT32_MAX)
    {
        return ctx->m_value_to_slot[value_index];
    }
    
    int32_t slot = ctx->m_next_slot;
    ctx->m_next_slot--;
    
    if (slot < ctx->m_max_slot)
    {
        ctx->m_max_slot = slot;
    }
    
    ctx->m_value_to_slot[value_index] = slot;
    return slot;
}

static void _woort_ir_regalloc_assign_value(woort_IRRegAllocCtx* ctx, const woort_IRValue* val)
{
    if (val == NULL)
    {
        return;
    }
    _woort_ir_regalloc_alloc_slot(ctx, val->m_index);
}

static void _woort_ir_regalloc_assign_block(woort_IRRegAllocCtx* ctx, woort_IRBlock* block)
{
    for (uint32_t i = 0; i < block->m_instr_count; ++i)
    {
        woort_IRInstr* instr = &block->m_instrs[i];
        if (instr->m_result != NULL)
        {
            _woort_ir_regalloc_assign_value(ctx, instr->m_result);
        }
    }
}

static void _woort_ir_regalloc_assign_func(woort_IRRegAllocCtx* ctx)
{
    woort_IRFunction* func = ctx->m_func;
    
    for (uint32_t i = 0; i < func->m_param_count; ++i)
    {
        _woort_ir_regalloc_alloc_slot(ctx, func->m_params[i].m_index);
    }
    
    for (uint32_t i = 0; i < func->m_block_count; ++i)
    {
        _woort_ir_regalloc_assign_block(ctx, func->m_blocks[i]);
    }
    
    for (uint32_t i = 0; i < func->m_phi_count; ++i)
    {
        woort_IRPHI* phi = func->m_phis[i];
        _woort_ir_regalloc_alloc_slot(ctx, phi->m_value.m_index);
    }
}

WOORT_NODISCARD int32_t _woort_ir_regalloc_get_stack_size(woort_IRRegAllocCtx* ctx)
{
    return -ctx->m_max_slot;
}

WOORT_NODISCARD bool _woort_ir_regalloc_run(woort_IRFunction* func, woort_IRRegAllocCtx* out_ctx)
{
    if (!_woort_ir_regalloc_ctx_init(out_ctx, func))
    {
        return false;
    }
    
    _woort_ir_regalloc_assign_func(out_ctx);
    
    return true;
}

/*
 * 获取值对应的栈槽
 */
WOORT_NODISCARD int32_t _woort_ir_value_get_slot(const woort_IRValue* val, woort_IRRegAllocCtx* ctx)
{
    if (val == NULL)
    {
        return 0;
    }
    return _woort_ir_regalloc_get_slot(ctx, val->m_index);
}