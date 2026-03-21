/*
 * woort_ir_phi.c
 */

#include "woort_ir_internal.h"
#include "woort_ir_function.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WOORT_IR_PHI_INITIAL_INCOMING_CAPACITY 4

WOORT_NODISCARD bool _woort_ir_phi_init(
    woort_IRPHI** out_phi,
    woort_IRBlock* block,
    uint32_t value_index)
{
    assert(out_phi != NULL);
    assert(block != NULL);

    woort_IRPHI* phi = (woort_IRPHI*)malloc(sizeof(woort_IRPHI));
    if (phi == NULL)
    {
        return false;
    }

    phi->m_block = block;
    phi->m_value.m_defining_block = block;
    phi->m_value.m_index = value_index;
    phi->m_value.m_defining_instr = NULL;

    phi->m_incomings = malloc(sizeof(phi->m_incomings[0]) * WOORT_IR_PHI_INITIAL_INCOMING_CAPACITY);
    if (phi->m_incomings == NULL)
    {
        free(phi);
        return false;
    }

    phi->m_incoming_count = 0;
    phi->m_incoming_capacity = WOORT_IR_PHI_INITIAL_INCOMING_CAPACITY;

    *out_phi = phi;
    return true;
}

void _woort_ir_phi_drop(woort_IRPHI* phi)
{
    if (phi == NULL)
    {
        return;
    }

    if (phi->m_incomings != NULL)
    {
        free(phi->m_incomings);
    }

    free(phi);
}

static WOORT_NODISCARD bool _woort_ir_phi_ensure_capacity(woort_IRPHI* phi)
{
    if (phi->m_incoming_count < phi->m_incoming_capacity)
    {
        return true;
    }

    uint32_t new_capacity = phi->m_incoming_capacity * 2;
    woort_IRPHIIncoming* new_incomings = (woort_IRPHIIncoming*)realloc(
        phi->m_incomings,
        sizeof(woort_IRPHIIncoming) * new_capacity);

    if (new_incomings == NULL)
    {
        return false;
    }

    phi->m_incomings = new_incomings;
    phi->m_incoming_capacity = new_capacity;
    return true;
}

void woort_IRPHI_add_incoming(woort_IRPHI* phi, woort_IRBlock* from_block, const woort_IRValue* value)
{
    assert(phi != NULL);
    assert(from_block != NULL);
    assert(value != NULL);

    if (!_woort_ir_phi_ensure_capacity(phi))
    {
        return;
    }

    phi->m_incomings[phi->m_incoming_count].m_from_block = from_block;
    phi->m_incomings[phi->m_incoming_count].m_value = value;
    phi->m_incoming_count++;
}

WOORT_NODISCARD const woort_IRValue* woort_IRPHI_as_value(woort_IRPHI* phi)
{
    assert(phi != NULL);
    return &phi->m_value;
}
