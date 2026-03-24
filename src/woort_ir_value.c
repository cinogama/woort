#include "woort_ir_value.h"

void woort_IRValue_init_constant(woort_IRValue* ir_value, woort_IRConstantIndex idx)
{
    ir_value->m_source = WOORT_IRVALUE_SOURCE_CONSTANT;
    ir_value->m_constant = idx;
    ir_value->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
}

void woort_IRValue_init_operate(woort_IRValue* ir_value, const woort_IROp* op)
{
    ir_value->m_source = WOORT_IRVALUE_SOURCE_RESULT;
    ir_value->m_operate = op;
    ir_value->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
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
