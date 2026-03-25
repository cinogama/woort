#include "woort_ir_value.h"
#include "woort_ir_op.h"

#include <assert.h>

void woort_IRValue_init_constant(woort_IRValue* ir_value, woort_IRConstantIndex idx)
{
    ir_value->m_source = WOORT_IRVALUE_SOURCE_CONSTANT;
    ir_value->m_constant = idx;
    ir_value->m_constant_need_stack_slot = false;
    ir_value->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
}

void woort_IRValue_init_operate(woort_IRValue* ir_value, woort_IROp* modify_op)
{
    ir_value->m_source = WOORT_IRVALUE_SOURCE_RESULT;
    ir_value->m_operate = modify_op;
    ir_value->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;

    modify_op->m_w = ir_value;
}

void woort_IRValue_init_phi(woort_IRValue* ir_value)
{
    ir_value->m_source = WOORT_IRVALUE_SOURCE_PHI;
    ir_value->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
}

void woort_IRValue_init_argument(woort_IRValue* ir_value, uint32_t argument_idx)
{
    ir_value->m_source = WOORT_IRVALUE_SOURCE_ARGUMENT;
    ir_value->m_argument_idx = argument_idx;
    ir_value->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
}

void woort_IRValue_mark_constant_need_stack_slot(woort_IRValue* ir_value)
{
    assert(ir_value->m_source == WOORT_IRVALUE_SOURCE_CONSTANT);
    ir_value->m_constant_need_stack_slot = true;
}
