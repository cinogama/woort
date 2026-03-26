#include "woort_ir_block.h"
#include "woort_ir_function.h"
#include "woort_diagnosis.h"

#include <string.h>
#include <assert.h>
#include <stdbool.h>

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

void woort_IRBlock_init(woort_IRBlock* block, woort_IRFunction* ir_func)
{
    block->m_ir_func = ir_func;

    woort_linklist_init(&block->m_operates, sizeof(woort_IROp));
    woort_linklist_init(&block->m_phis, sizeof(woort_IRPhi));
    woort_vector_init(&block->m_prev_blocks, sizeof(woort_IRBlock*));

    block->m_cond_type = WOORT_IRBLOCK_ENDWAY_NOT_FINISHED;

    block->m_idom = NULL;
    woort_vector_init(&block->m_dom_children, sizeof(woort_IRBlock*));
    block->m_dom_depth = 0;

    block->m_is_in_loop = false;
    block->m_loop_header = NULL;

    woort_vector_init(&block->m_loading_constants, sizeof(woort_IRValue*));
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
    woort_vector_deinit(&block->m_dom_children);
    woort_vector_deinit(&block->m_loading_constants);
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

// IRBlock operate.

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LOAD(woort_IRBlock* b, woort_IRStaticIndex idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;
    
    op->m_op = WOORT_IROP_KIND_LOAD;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_static_index = idx;
   
    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

void woort_IRBlock_STORE(woort_IRBlock* b, woort_IRStaticIndex idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_STORE;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_static_index = idx;
}

void woort_IRBlock_PUSHCHK(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_PUSHCHK;
    op->m_w = NULL;
    op->m_r[0] = val;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_POP(woort_IRBlock* b)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_POP;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

void woort_IRBlock_POPR(woort_IRBlock* b, uint32_t count)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_POPR;
    op->m_w = NULL;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_count = count;
}

void woort_IRBlock_POPRS(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_POPRS;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_ITOR(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_ITOR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_ITOS(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_ITOS;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_RTOI(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_RTOI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_RTOS(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_RTOS;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_STOI(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_STOI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_STOR(woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_STOR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);

    return result;
}

void woort_IRBlock_CALLNWO(
    woort_IRBlock* b,
    woort_IRConstantIndex f,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_CALLNWO;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_calln_target = f;
    op->m_argument_count = argc_to_pop;

    if (out_ret_val != NULL)
    {
        woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
        *out_ret_val = result;
    }
}

void woort_IRBlock_CALLNFP(
    woort_IRBlock* b,
    woort_IRConstantIndex f,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_CALLNFP;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_calln_target = f;
    op->m_argument_count = argc_to_pop;

    if (out_ret_val != NULL)
    {
        woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
        *out_ret_val = result;
    }
}

void woort_IRBlock_CALLNJIT(
    woort_IRBlock* b,
    woort_IRConstantIndex f,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_CALLNJIT;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_calln_target = f;
    op->m_argument_count = argc_to_pop;

    if (out_ret_val != NULL)
    {
        woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
        *out_ret_val = result;
    }
}

void woort_IRBlock_CALL(
    woort_IRBlock* b,
    woort_IRValue* f_val,
    uint32_t argc_to_pop,
    /* OPTIONAL */ woort_IRValue** out_ret_val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_CALL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(f_val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_argument_count = argc_to_pop;

    if (out_ret_val != NULL)
    {
        woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
        *out_ret_val = result;
    }
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MKCLOSURE(woort_IRBlock* b, uint32_t elem_count, woort_IRConstantIndex f)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MKCLOSURE;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_calln_target = f;
    op->m_argument_count = elem_count;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MKVEC(woort_IRBlock* b, uint32_t elem_count)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MKVEC;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_count = elem_count;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MKMAP(woort_IRBlock* b, uint32_t kvpair_count)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MKMAP;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_count = kvpair_count;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MKSTRUCT(woort_IRBlock* b, uint32_t elem_count)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MKSTRUCT;
    op->m_r[0] = NULL;
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_count = elem_count;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_BOXDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_BOXDYN;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_type = typ;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_UNBOXDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_UNBOXDYN;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_type = typ;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_CHECKDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_CHECKDYN;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_type = typ;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

void woort_IRBlock_PUSHBOXDYN(
    woort_IRBlock* b, uint8_t typ, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_PUSHBOXDYN;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_type = typ;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_ADDI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_ADDI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_SUBI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_SUBI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MULI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MULI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_DIVI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_DIVI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MODI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MODI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_NEGI(
    woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_NEGI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LTI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LTI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_GTI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_GTI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LEI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LEI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_GEI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_GEI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_EQI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_EQI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_NEI(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_NEI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_ADDR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_ADDR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_SUBR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_SUBR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MULR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MULR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_DIVR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_DIVR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_MODR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_MODR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_NEGR(
    woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_NEGR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LTR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LTR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_GTR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_GTR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LER(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LER;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_GER(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_GER;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_EQR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_EQR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_NER(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_NER;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_ADDS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_ADDS;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LTS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LTS;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_GTS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_GTS;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LES(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LES;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_GES(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_GES;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_EQS(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_EQS;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_NES(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_NES;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LAND(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LAND;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LOR(
    woort_IRBlock* b, woort_IRValue* val1, woort_IRValue* val2)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LOR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val1);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val2);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LNOT(
    woort_IRBlock* b, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LNOT;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXVEC(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXVEC;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXVECX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXVECX;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXSTRUCT(
    woort_IRBlock* b, woort_IRValue* c, uint32_t idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXSTRUCT;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_index = idx;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXSTRING(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXSTRING;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXDICTI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXDICTI;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXDICTR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXDICTR;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXDICTB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXDICTB;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_LDIDXDICTX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_LDIDXDICTX;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

void woort_IRBlock_SDIDXVECI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXVECI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXVECR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXVECR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXVECB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXVECB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXVECX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXVECX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTII(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTII;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTIR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTIR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTIB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTIB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTIX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTIX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTRI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTRI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTRR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTRR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTRB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTRB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTRX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTRX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTBI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTBI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTBR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTBR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTBB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTBB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTBX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTBX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTXI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTXI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTXR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTXR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTXB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTXB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXDICTXX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXDICTXX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPII(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPII;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPIR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPIR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPIB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPIB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPIX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPIX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPRI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPRI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPRR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPRR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPRB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPRB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPRX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPRX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPBI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPBI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPBR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPBR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPBB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPBB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPBX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPBX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPXI(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPXI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPXR(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPXR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPXB(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPXB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXMAPXX(
    woort_IRBlock* b, woort_IRValue* c, woort_IRValue* idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXMAPXX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(idx);
    op->m_r[2] = woort_IRValue_ensure_constant_stack_slot(val);
}

void woort_IRBlock_SDIDXSTRUCT(
    woort_IRBlock* b, woort_IRValue* c, uint32_t idx, woort_IRValue* val)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_SDIDXSTRUCT;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = woort_IRValue_ensure_constant_stack_slot(val);
    op->m_r[2] = NULL;
    op->m_index = idx;
}

void woort_IRBlock_UNPACKSTRUCT(woort_IRBlock* b, woort_IRValue* c)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_UNPACKSTRUCT;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_UNPACKVEC(woort_IRBlock* b, woort_IRValue* c)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_UNPACKVEC;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

WOORT_NODISCARD /* OPTIONAL */ woort_IRValue* woort_IRBlock_UNPACKVECX(woort_IRBlock* b, woort_IRValue* c)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return NULL;

    op->m_op = WOORT_IROP_KIND_UNPACKVECX;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;

    woort_IRValue* const result = woort_IRFunction_operate_result(b->m_ir_func, op);
    if (result == NULL)
        return NULL;

    assert(op->m_w == result);
    return result;
}

void woort_IRBlock_PUSHIDXSTRUCT(woort_IRBlock* b, woort_IRValue* c, uint32_t idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_PUSHIDXSTRUCT;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_index = idx;
}

void woort_IRBlock_PUSHIDXSTBOXI(woort_IRBlock* b, woort_IRValue* c, uint32_t idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_PUSHIDXSTBOXI;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_index = idx;
}

void woort_IRBlock_PUSHIDXSTBOXR(woort_IRBlock* b, woort_IRValue* c, uint32_t idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_PUSHIDXSTBOXR;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_index = idx;
}

void woort_IRBlock_PUSHIDXSTBOXB(woort_IRBlock* b, woort_IRValue* c, uint32_t idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_PUSHIDXSTBOXB;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_index = idx;
}

void woort_IRBlock_PUSHIDXSTBOXX(woort_IRBlock* b, woort_IRValue* c, uint32_t idx)
{
    woort_IROp* op;
    if (!woort_linklist_emplace_back(&b->m_operates, (void**)&op))
        return;

    op->m_op = WOORT_IROP_KIND_PUSHIDXSTBOXX;
    op->m_w = NULL;
    op->m_r[0] = woort_IRValue_ensure_constant_stack_slot(c);
    op->m_r[1] = NULL;
    op->m_r[2] = NULL;
    op->m_index = idx;
}
