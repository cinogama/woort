#include "woort_ir_builder.h"
#include "woort_ir_arena.h"

#include <stdlib.h>
#include <string.h>

#define WOORT_IR_BLOCK_INITIAL_PRED_CAPACITY 4
#define WOORT_IR_LOCAL_INITIAL_VALS_CAPACITY 16

static woort_IRValue* _woort_IRBuilder_alloc_value(woort_IRBuilder* builder)
{
    woort_IRArena* arena = builder->m_function->m_module->m_arena;
    woort_IRValue* value = woort_IRArena_alloc_type(arena, woort_IRValue);
    if (!value)
    {
        return NULL;
    }
    value->m_id = builder->m_function->m_next_value_id++;
    value->m_def_block = builder->m_insert_block;
    value->m_def_inst = NULL;
    return value;
}

static woort_IRInst* _woort_IRBuilder_alloc_inst(
    woort_IRBuilder* builder,
    woort_IROpcode op,
    uint32_t operand_count)
{
    woort_IRArena* arena = builder->m_function->m_module->m_arena;
    woort_IRInst* inst = woort_IRArena_alloc_type(arena, woort_IRInst);
    if (!inst)
    {
        return NULL;
    }
    inst->m_op = op;
    inst->m_result = NULL;
    inst->m_operand_count = operand_count;

    if (operand_count > 0)
    {
        inst->m_operands = woort_IRArena_alloc_array(arena, woort_IRValue*, operand_count);
        if (!inst->m_operands)
        {
            return NULL;
        }
    }
    else
    {
        inst->m_operands = NULL;
    }

    inst->m_next = NULL;
    inst->m_prev = NULL;
    inst->m_phi_src_blocks = NULL;
    inst->m_phi_incoming_count = 0;

    return inst;
}

static void _woort_IRBlock_append_inst(woort_IRBlock* block, woort_IRInst* inst)
{
    if (!block->m_first)
    {
        block->m_first = inst;
        block->m_last = inst;
    }
    else
    {
        block->m_last->m_next = inst;
        inst->m_prev = block->m_last;
        block->m_last = inst;
    }
}

static void _woort_IRBlock_add_pred(woort_IRBlock* block, woort_IRBlock* pred)
{
    if (block->m_pred_info.m_pred_count >= block->m_pred_info.m_pred_capacity)
    {
        uint32_t new_capacity = block->m_pred_info.m_pred_capacity * 2;
        woort_IRBlock** new_preds = (woort_IRBlock**)realloc(
            block->m_pred_info.m_preds,
            sizeof(woort_IRBlock*) * new_capacity);
        if (!new_preds)
        {
            return;
        }
        block->m_pred_info.m_preds = new_preds;
        block->m_pred_info.m_pred_capacity = new_capacity;
    }
    block->m_pred_info.m_preds[block->m_pred_info.m_pred_count++] = pred;
}

static void _woort_IRFunction_add_block(woort_IRFunction* func, woort_IRBlock* block)
{
    if (!func->m_block_list)
    {
        func->m_block_list = block;
        func->m_entry_block = block;
    }
    else
    {
        woort_IRBlock* last = func->m_block_list;
        while (last->m_next)
        {
            last = last->m_next;
        }
        last->m_next = block;
        block->m_prev = last;
    }
    func->m_block_count++;
}

WOORT_NODISCARD bool woort_IRBuilder_create(
    woort_IRFunction* func,
    woort_IRBuilder** out_builder)
{
    woort_IRBuilder* builder = (woort_IRBuilder*)malloc(sizeof(woort_IRBuilder));
    if (!builder)
    {
        return false;
    }

    builder->m_function = func;
    builder->m_insert_block = NULL;

    *out_builder = builder;
    return true;
}

void woort_IRBuilder_destroy(woort_IRBuilder* builder)
{
    free(builder);
}

WOORT_NODISCARD bool woort_IRBuilder_create_block(
    woort_IRBuilder* builder,
    woort_IRBlock** out_block)
{
    woort_IRArena* arena = builder->m_function->m_module->m_arena;
    woort_IRBlock* block = woort_IRArena_alloc_type(arena, woort_IRBlock);
    if (!block)
    {
        return false;
    }

    block->m_function = builder->m_function;
    block->m_id = builder->m_function->m_next_block_id++;
    block->m_first = NULL;
    block->m_last = NULL;
    block->m_next = NULL;
    block->m_prev = NULL;

    block->m_pred_info.m_preds = woort_IRArena_alloc_array(arena, woort_IRBlock*, WOORT_IR_BLOCK_INITIAL_PRED_CAPACITY);
    if (!block->m_pred_info.m_preds)
    {
        return false;
    }
    block->m_pred_info.m_pred_count = 0;
    block->m_pred_info.m_pred_capacity = WOORT_IR_BLOCK_INITIAL_PRED_CAPACITY;

    block->m_succs[0] = NULL;
    block->m_succs[1] = NULL;
    block->m_succ_count = 0;

    block->m_phis = NULL;
    block->m_is_sealed = false;

    _woort_IRFunction_add_block(builder->m_function, block);

    *out_block = block;
    return true;
}

void woort_IRBuilder_position_at_end(woort_IRBuilder* builder, woort_IRBlock* block)
{
    builder->m_insert_block = block;
}

woort_IRBlock* woort_IRBuilder_get_insert_block(woort_IRBuilder* builder)
{
    return builder->m_insert_block;
}

void woort_IRBlock_seal(woort_IRBlock* block)
{
    block->m_is_sealed = true;
}

WOORT_NODISCARD bool woort_IRBuilder_create_local(
    woort_IRBuilder* builder,
    woort_IRLocal** out_local)
{
    woort_IRFunction* func = builder->m_function;
    woort_IRArena* arena = func->m_module->m_arena;

    if (func->m_local_count >= func->m_local_capacity)
    {
        uint32_t new_capacity = func->m_local_capacity * 2;
        woort_IRLocal** new_locals = (woort_IRLocal**)realloc(
            func->m_locals,
            sizeof(woort_IRLocal*) * new_capacity);
        if (!new_locals)
        {
            return false;
        }
        func->m_locals = new_locals;
        func->m_local_capacity = new_capacity;
    }

    woort_IRLocal* local = woort_IRArena_alloc_type(arena, woort_IRLocal);
    if (!local)
    {
        return false;
    }

    local->m_id = func->m_local_count;
    local->m_function = func;
    local->m_current_vals_capacity = func->m_block_count > 0 ? func->m_block_count : WOORT_IR_LOCAL_INITIAL_VALS_CAPACITY;
    local->m_current_vals = woort_IRArena_alloc_array(arena, woort_IRValue*, local->m_current_vals_capacity);
    if (!local->m_current_vals)
    {
        return false;
    }

    for (uint32_t i = 0; i < local->m_current_vals_capacity; ++i)
    {
        local->m_current_vals[i] = NULL;
    }

    func->m_locals[func->m_local_count++] = local;

    *out_local = local;
    return true;
}

void woort_IRBuilder_set_local(
    woort_IRBuilder* builder,
    woort_IRLocal* local,
    woort_IRValue* value)
{
    if (!builder->m_insert_block)
    {
        return;
    }

    uint32_t block_id = builder->m_insert_block->m_id;
    if (block_id >= local->m_current_vals_capacity)
    {
        return;
    }

    local->m_current_vals[block_id] = value;
}

WOORT_NODISCARD bool woort_IRBuilder_get_local(
    woort_IRBuilder* builder,
    woort_IRLocal* local,
    woort_IRValue** out_value)
{
    (void)builder;
    (void)local;
    (void)out_value;
    return false;
}

void woort_IRBuilder_ret_void(woort_IRBuilder* builder)
{
    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_RET, 0);
    if (!inst)
    {
        return;
    }
    _woort_IRBlock_append_inst(builder->m_insert_block, inst);
}

void woort_IRBuilder_ret(woort_IRBuilder* builder, woort_IRValue* value)
{
    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_RET, 1);
    if (!inst)
    {
        return;
    }
    inst->m_operands[0] = value;
    _woort_IRBlock_append_inst(builder->m_insert_block, inst);
}

void woort_IRBuilder_br(woort_IRBuilder* builder, woort_IRBlock* dest)
{
    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_BR, 1);
    if (!inst)
    {
        return;
    }
    inst->m_operands[0] = (woort_IRValue*)dest;
    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    _woort_IRBlock_add_pred(dest, builder->m_insert_block);

    builder->m_insert_block->m_succs[0] = dest;
    builder->m_insert_block->m_succ_count = 1;
}

WOORT_NODISCARD bool woort_IRBuilder_cond_br(
    woort_IRBuilder* builder,
    woort_IRValue* cond,
    woort_IRBlock* then_block,
    woort_IRBlock* else_block)
{
    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_COND_BR, 3);
    if (!inst)
    {
        return false;
    }
    inst->m_operands[0] = cond;
    inst->m_operands[1] = (woort_IRValue*)then_block;
    inst->m_operands[2] = (woort_IRValue*)else_block;
    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    _woort_IRBlock_add_pred(then_block, builder->m_insert_block);
    _woort_IRBlock_add_pred(else_block, builder->m_insert_block);

    builder->m_insert_block->m_succs[0] = then_block;
    builder->m_insert_block->m_succs[1] = else_block;
    builder->m_insert_block->m_succ_count = 2;

    return true;
}

static woort_IRValue* _woort_IRBuilder_binary_op(
    woort_IRBuilder* builder,
    woort_IROpcode op,
    woort_IRValue* lhs,
    woort_IRValue* rhs)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result)
    {
        return NULL;
    }

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, op, 2);
    if (!inst)
    {
        return NULL;
    }

    inst->m_result = result;
    inst->m_operands[0] = lhs;
    inst->m_operands[1] = rhs;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    return result;
}

static woort_IRValue* _woort_IRBuilder_unary_op(
    woort_IRBuilder* builder,
    woort_IROpcode op,
    woort_IRValue* value)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result)
    {
        return NULL;
    }

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, op, 1);
    if (!inst)
    {
        return NULL;
    }

    inst->m_result = result;
    inst->m_operands[0] = value;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    return result;
}

WOORT_NODISCARD bool woort_IRBuilder_add_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_ADD_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_sub_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_SUB_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_mul_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_MUL_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_div_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_DIV_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_mod_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_MOD_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_neg_i(woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_unary_op(builder, WOORT_IR_OP_NEG_I, value);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_add_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_ADD_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_sub_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_SUB_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_mul_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_MUL_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_div_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_DIV_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_mod_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_MOD_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_neg_r(woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_unary_op(builder, WOORT_IR_OP_NEG_R, value);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_lt_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_LT_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_le_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_LE_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_gt_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_GT_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ge_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_GE_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_eq_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_EQ_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ne_i(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_NE_I, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_lt_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_LT_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_le_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_LE_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_gt_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_GT_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ge_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_GE_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_eq_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_EQ_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ne_r(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_NE_R, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_add_s(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_ADD_S, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_lt_s(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_LT_S, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_le_s(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_LE_S, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_gt_s(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_GT_S, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ge_s(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_GE_S, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_eq_s(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_EQ_S, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ne_s(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_NE_S, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_and(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_AND, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_or(woort_IRBuilder* builder, woort_IRValue* lhs, woort_IRValue* rhs, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_binary_op(builder, WOORT_IR_OP_OR, lhs, rhs);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_not(woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_unary_op(builder, WOORT_IR_OP_NOT, value);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_const_int(woort_IRBuilder* builder, int64_t value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_CONST_INT, 1);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = (woort_IRValue*)(uintptr_t)value;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_const_real(woort_IRBuilder* builder, double value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_CONST_REAL, 1);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = (woort_IRValue*)(uintptr_t)value;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_const_bool(woort_IRBuilder* builder, bool value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_CONST_BOOL, 1);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = (woort_IRValue*)(uintptr_t)(value ? 1 : 0);
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_const_str(woort_IRBuilder* builder, const char* str, size_t len, woort_IRValue** out_result)
{
    (void)str;
    (void)len;
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_CONST_STR, 1);
    if (!inst) return false;

    inst->m_result = result;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_const_null(woort_IRBuilder* builder, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_CONST_NULL, 0);
    if (!inst) return false;

    inst->m_result = result;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_param(woort_IRBuilder* builder, uint32_t index, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_PARAM, 1);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = (woort_IRValue*)(uintptr_t)index;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_cast_i_to_r(woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_unary_op(builder, WOORT_IR_OP_CAST_I_TO_R, value);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_cast_r_to_i(woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_unary_op(builder, WOORT_IR_OP_CAST_R_TO_I, value);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_box_dyn(woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_unary_op(builder, WOORT_IR_OP_BOX_DYN, value);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_unbox_dyn(woort_IRBuilder* builder, woort_IRValue* value, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_unary_op(builder, WOORT_IR_OP_UNBOX_DYN, value);
    if (!result) return false;
    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_mkvec(woort_IRBuilder* builder, woort_IRValue** elems, uint32_t count, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_MKVEC, count);
    if (!inst) return false;

    inst->m_result = result;
    for (uint32_t i = 0; i < count; ++i)
    {
        inst->m_operands[i] = elems[i];
    }
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_mkmap(woort_IRBuilder* builder, woort_IRValue** kvs, uint32_t count, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_MKMAP, count);
    if (!inst) return false;

    inst->m_result = result;
    for (uint32_t i = 0; i < count; ++i)
    {
        inst->m_operands[i] = kvs[i];
    }
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_mkstruct(woort_IRBuilder* builder, woort_IRValue** fields, uint32_t count, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_MKSTRUCT, count);
    if (!inst) return false;

    inst->m_result = result;
    for (uint32_t i = 0; i < count; ++i)
    {
        inst->m_operands[i] = fields[i];
    }
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ldvec(woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_LDVEC, 2);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = vec;
    inst->m_operands[1] = index;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ldstr(woort_IRBuilder* builder, woort_IRValue* str, woort_IRValue* index, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_LDSTR, 2);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = str;
    inst->m_operands[1] = index;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ldstruct(woort_IRBuilder* builder, woort_IRValue* st, uint32_t field_index, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_LDSTRUCT, 2);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = st;
    inst->m_operands[1] = (woort_IRValue*)(uintptr_t)field_index;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ldmap_i(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_LDMAP_I, 2);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = map;
    inst->m_operands[1] = key;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ldmap_r(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_LDMAP_R, 2);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = map;
    inst->m_operands[1] = key;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ldmap_b(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_LDMAP_B, 2);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = map;
    inst->m_operands[1] = key;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_ldmap_x(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue** out_result)
{
    woort_IRValue* result = _woort_IRBuilder_alloc_value(builder);
    if (!result) return false;

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_LDMAP_X, 2);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = map;
    inst->m_operands[1] = key;
    result->m_def_inst = inst;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    *out_result = result;
    return true;
}

static bool _woort_IRBuilder_stelem(woort_IRBuilder* builder, woort_IROpcode op, woort_IRValue* container, woort_IRValue* index, woort_IRValue* value)
{
    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, op, 3);
    if (!inst) return false;

    inst->m_operands[0] = container;
    inst->m_operands[1] = index;
    inst->m_operands[2] = value;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_stvec_i(woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STVEC_I, vec, index, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stvec_r(woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STVEC_R, vec, index, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stvec_b(woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STVEC_B, vec, index, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stvec_x(woort_IRBuilder* builder, woort_IRValue* vec, woort_IRValue* index, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STVEC_X, vec, index, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_i(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_I_I, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_r(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_I_R, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_b(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_I_B, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_i_x(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_I_X, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_i(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_R_I, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_r(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_R_R, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_b(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_R_B, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_r_x(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_R_X, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_i(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_B_I, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_r(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_B_R, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_b(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_B_B, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_b_x(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_B_X, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_i(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_X_I, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_r(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_X_R, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_b(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_X_B, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_stmap_x_x(woort_IRBuilder* builder, woort_IRValue* map, woort_IRValue* key, woort_IRValue* value)
{
    return _woort_IRBuilder_stelem(builder, WOORT_IR_OP_STMAP_X_X, map, key, value);
}

WOORT_NODISCARD bool woort_IRBuilder_ststruct(woort_IRBuilder* builder, woort_IRValue* st, uint32_t field_index, woort_IRValue* value)
{
    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_STSTRUCT, 3);
    if (!inst) return false;

    inst->m_operands[0] = st;
    inst->m_operands[1] = (woort_IRValue*)(uintptr_t)field_index;
    inst->m_operands[2] = value;

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    return true;
}

WOORT_NODISCARD bool woort_IRBuilder_call(
    woort_IRBuilder* builder,
    woort_IRValue* func,
    woort_IRValue** args,
    uint32_t arg_count,
    /* OPTIONAL */ woort_IRValue** out_result)
{
    woort_IRArena* arena = builder->m_function->m_module->m_arena;

    woort_IRValue* result = NULL;
    if (out_result)
    {
        result = _woort_IRBuilder_alloc_value(builder);
        if (!result) return false;
    }

    woort_IRInst* inst = _woort_IRBuilder_alloc_inst(builder, WOORT_IR_OP_CALL, 1 + arg_count);
    if (!inst) return false;

    inst->m_result = result;
    inst->m_operands[0] = func;
    for (uint32_t i = 0; i < arg_count; ++i)
    {
        inst->m_operands[1 + i] = args[i];
    }
    if (result)
    {
        result->m_def_inst = inst;
    }

    _woort_IRBlock_append_inst(builder->m_insert_block, inst);

    if (out_result)
    {
        *out_result = result;
    }

    return true;
}