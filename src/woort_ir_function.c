#include "woort_ir_function.h"
#include "woort_ir_block.h"
#include "woort_vector.h"

#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

void woort_IRFunction_init(woort_IRFunction* ir_function, uint32_t param_count)
{
    ir_function->m_param_count = param_count;
    ir_function->m_entry_block = NULL;

    woort_linklist_init(&ir_function->m_ir_values, sizeof(woort_IRValue));
    woort_linklist_init(&ir_function->m_ir_blocks, sizeof(woort_IRBlock));
}

void woort_IRFunction_deinit(woort_IRFunction* ir_function)
{
    for (woort_IRBlock* block = woort_linklist_iter(&ir_function->m_ir_blocks);
        block != NULL;
        block = woort_linklist_next(block))
    {
        woort_IRBlock_deinit(block);
    }

    woort_linklist_deinit(&ir_function->m_ir_values);
    woort_linklist_deinit(&ir_function->m_ir_blocks);
}

/* OPTIONAL */ woort_IRBlock* woort_IRFunction_entry_block(woort_IRFunction* f)
{
    if (f->m_entry_block == NULL)
        f->m_entry_block = woort_IRFuntion_add_block(f);

    return f->m_entry_block;
}

/* OPTIONAL */ woort_IRBlock* woort_IRFuntion_add_block(woort_IRFunction* f)
{
    woort_IRBlock* block;
    if (!woort_linklist_emplace_back(&f->m_ir_blocks, (void**)&block))
        return NULL;

    woort_IRBlock_init(block, f);

    if (f->m_entry_block == NULL)
        f->m_entry_block = block;

    return block;
}

/* OPTIONAL */ woort_IRValue* _woort_IRFunction_new_value(woort_IRFunction* f)
{
    woort_IRValue* value;
    if (!woort_linklist_emplace_back(&f->m_ir_values, (void**)&value))
        return NULL;

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFuntion_load_constant(
    woort_IRFunction* f, woort_IRConstantIndex c)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_constant(value, c);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_get_argument(
    woort_IRFunction* f, uint32_t param_idx)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_argument(value, param_idx);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_operate_result(
    woort_IRFunction* f, const woort_IROp* op)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_operate(value, op);

    return value;
}

/* OPTIONAL */ woort_IRValue* woort_IRFunction_phi_value(woort_IRFunction* f)
{
    woort_IRValue* const value = _woort_IRFunction_new_value(f);
    if (value == NULL)
        return NULL;

    woort_IRValue_init_phi(value);

    return value;
}

static WOORT_NODISCARD bool _woort_IRFunction_collect_blocks_rpo_dfs(
    woort_IRBlock* block,
    woort_Vector* visited,
    woort_Vector* postorder)
{
    for (size_t i = 0; i < visited->m_size; ++i)
    {
        woort_IRBlock* const visited_block = *(woort_IRBlock**)woort_vector_at(visited, i);
        if (visited_block == block)
            return true;
    }

    if (!woort_vector_push_back(visited, 1, &block))
        return false;

    switch (block->m_cond_type)
    {
    case WOORT_IRBLOCK_ENDWAY_BR:
        if (!_woort_IRFunction_collect_blocks_rpo_dfs(
            block->m_br_next_block, visited, postorder))
            return false;
        break;

    case WOORT_IRBLOCK_ENDWAY_BR_COND:
        if (!_woort_IRFunction_collect_blocks_rpo_dfs(
            block->m_br_next_block_cond_true, visited, postorder))
            return false;
        if (!_woort_IRFunction_collect_blocks_rpo_dfs(
            block->m_br_next_block_cond_false, visited, postorder))
            return false;
        break;

    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
    case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
        if (!_woort_IRFunction_collect_blocks_rpo_dfs(
            block->m_br_next_block_compare_true, visited, postorder))
            return false;
        if (!_woort_IRFunction_collect_blocks_rpo_dfs(
            block->m_br_next_block_compare_false, visited, postorder))
            return false;
        break;

    default:
        break;
    }

    return woort_vector_push_back(postorder, 1, &block);
}

static int32_t _woort_IRFunction_pop_or_new_slot(
    woort_Vector* free_slots,
    int32_t* next_slot)
{
    if (free_slots->m_size > 0)
    {
        int32_t slot;
        woort_vector_index(free_slots, free_slots->m_size - 1, (void**)&slot);
        free_slots->m_size--;
        return slot;
    }

    return (*next_slot)++;
}

static void _woort_IRFunction_free_slot_after_use(
    woort_IRValue* value,
    woort_Vector* free_slots)
{
    if (value == NULL)
        return;

    value->m_use_count--;
    if (value->m_use_count == 0
        && value->m_assigned_stack_offset != WOORT_IRVALUE_STACK_NOT_ASSIGN)
    {
        woort_vector_push_back(free_slots, 1, &value->m_assigned_stack_offset);
    }
}

uint32_t woort_IRFunction_stack_slot_allocation(woort_IRFunction* f)
{
    for (woort_IRBlock* block = woort_linklist_iter(&f->m_ir_blocks);
        block != NULL;
        block = woort_linklist_next(block))
    {
        for (woort_IRPhi* phi = woort_linklist_iter(&block->m_phis);
            phi != NULL;
            phi = woort_linklist_next(phi))
        {
            for (woort_IRPhi_ReentryRecord* record = woort_linklist_iter(&phi->m_records);
                record != NULL;
                record = woort_linklist_next(record))
            {
                record->m_value->m_use_count++;
            }
        }

        for (woort_IROp* op = woort_linklist_iter(&block->m_operates);
            op != NULL;
            op = woort_linklist_next(op))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (op->m_r[i] != NULL)
                    ((woort_IRValue*)op->m_r[i])->m_use_count++;
            }
        }

        switch (block->m_cond_type)
        {
        case WOORT_IRBLOCK_ENDWAY_BR_COND:
            block->m_br_cond_value->m_use_count++;
            break;

        case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
        case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
            block->m_br_compare_values[0]->m_use_count++;
            block->m_br_compare_values[1]->m_use_count++;
            break;

        case WOORT_IRBLOCK_ENDWAY_RET:
            if (block->m_ret_value_may_null != NULL)
                block->m_ret_value_may_null->m_use_count++;
            break;

        default:
            break;
        }
    }

    for (woort_IRValue* value = woort_linklist_iter(&f->m_ir_values);
        value != NULL;
        value = woort_linklist_next(value))
    {
        if (value->m_source == WOORT_IRVALUE_SOURCE_ARGUMENT)
            value->m_assigned_stack_offset = (int32_t)value->m_argument_idx;
    }

    woort_Vector visited;
    woort_vector_init(&visited, sizeof(woort_IRBlock*));

    woort_Vector postorder;
    woort_vector_init(&postorder, sizeof(woort_IRBlock*));

    if (f->m_entry_block != NULL)
    {
        if (!_woort_IRFunction_collect_blocks_rpo_dfs(
            f->m_entry_block, &visited, &postorder))
        {
            woort_vector_deinit(&visited);
            woort_vector_deinit(&postorder);
            return 0;
        }
    }

    woort_Vector free_slots;
    woort_vector_init(&free_slots, sizeof(int32_t));
    int32_t next_slot = (int32_t)f->m_param_count;

    for (size_t i = postorder.m_size; i > 0; --i)
    {
        woort_IRBlock* const block = *(woort_IRBlock**)woort_vector_at(&postorder, i - 1);

        for (woort_IRPhi* phi = woort_linklist_iter(&block->m_phis);
            phi != NULL;
            phi = woort_linklist_next(phi))
        {
            woort_IRValue* const pv = phi->m_phi_value;
            if (pv->m_assigned_stack_offset == WOORT_IRVALUE_STACK_NOT_ASSIGN)
            {
                pv->m_assigned_stack_offset =
                    _woort_IRFunction_pop_or_new_slot(&free_slots, &next_slot);
            }
        }

        for (woort_IROp* op = woort_linklist_iter(&block->m_operates);
            op != NULL;
            op = woort_linklist_next(op))
        {
            for (int j = 0; j < 3; ++j)
            {
                _woort_IRFunction_free_slot_after_use(
                    (woort_IRValue*)op->m_r[j], &free_slots);
            }

            if (op->m_w != NULL)
            {
                woort_IRValue* const wv = (woort_IRValue*)op->m_w;
                if (wv->m_assigned_stack_offset == WOORT_IRVALUE_STACK_NOT_ASSIGN)
                {
                    assert(wv->m_source == WOORT_IRVALUE_SOURCE_RESULT);

                    wv->m_assigned_stack_offset =
                        _woort_IRFunction_pop_or_new_slot(&free_slots, &next_slot);
                }
            }
        }

        switch (block->m_cond_type)
        {
        case WOORT_IRBLOCK_ENDWAY_BR_COND:
            _woort_IRFunction_free_slot_after_use(
                block->m_br_cond_value, &free_slots);
            break;

        case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LT:
        case WOORT_IRBLOCK_ENDWAY_BR_COMPARE_LE:
            _woort_IRFunction_free_slot_after_use(
                block->m_br_compare_values[0], &free_slots);
            _woort_IRFunction_free_slot_after_use(
                block->m_br_compare_values[1], &free_slots);
            break;

        case WOORT_IRBLOCK_ENDWAY_RET:
            _woort_IRFunction_free_slot_after_use(
                block->m_ret_value_may_null, &free_slots);
            break;

        default:
            break;
        }
    }

    woort_vector_deinit(&free_slots);
    woort_vector_deinit(&postorder);
    woort_vector_deinit(&visited);

    return (uint32_t)next_slot;
}
