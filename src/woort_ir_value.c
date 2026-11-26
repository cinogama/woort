#include "woort_ir_value.h"

void woort_IRValue_init_vreg(woort_IRValue* v, uint32_t id)
{
    v->m_source = WOORT_IRVALUE_SOURCE_VREG;
    v->m_id = id;
    v->m_window = NULL;
    v->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
    v->m_is_const_direct = false;
}

void woort_IRValue_init_argument(woort_IRValue* v, uint32_t id, uint32_t argument_idx)
{
    v->m_source = WOORT_IRVALUE_SOURCE_ARGUMENT;
    v->m_id = id;
    v->m_window = NULL;
    v->m_assigned_stack_offset = 3 + (int32_t)argument_idx;
    v->m_is_const_direct = false;
}

void woort_IRValue_init_captured(woort_IRValue* v, uint32_t id, uint32_t captured_idx)
{
    v->m_source = WOORT_IRVALUE_SOURCE_ARGUMENT;
    v->m_id = id;
    v->m_window = NULL;
    v->m_assigned_stack_offset = -(int32_t)captured_idx;
    v->m_is_const_direct = false;
}

void woort_IRValue_init_const(woort_IRValue* v, uint32_t id, woort_IRConstantIndex const_idx)
{
    v->m_source = WOORT_IRVALUE_SOURCE_CONST;
    v->m_id = id;
    v->m_const_idx = const_idx;
    v->m_window = NULL;
    v->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
    v->m_is_const_direct = false;
}

void woort_IRValue_init_slot(woort_IRValue* v, uint32_t id, struct woort_IRWindow* window, uint32_t slot_index)
{
    v->m_source = WOORT_IRVALUE_SOURCE_SLOT;
    v->m_id = id;
    v->m_slot_index = slot_index;
    v->m_window = window;
    v->m_assigned_stack_offset = WOORT_IRVALUE_STACK_NOT_ASSIGN;
    v->m_is_const_direct = false;
}
