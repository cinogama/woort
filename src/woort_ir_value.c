#include "woort_ir_value.h"

void woort_IRValue_init_vreg(woort_IRValue* v, uint32_t id)
{
    v->m_source = WOORT_IRVALUE_SOURCE_VREG;
    v->m_id = id;
    v->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
}

void woort_IRValue_init_argument(woort_IRValue* v, uint32_t id, uint32_t argument_idx)
{
    v->m_source = WOORT_IRVALUE_SOURCE_ARGUMENT;
    v->m_id = id;
    v->m_argument_idx = argument_idx;
    v->m_assigned_stack_offset = 3 + (int32_t)argument_idx;
}
