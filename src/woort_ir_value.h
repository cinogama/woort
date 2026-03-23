#pragma once

/*
woort_ir_value.h
*/

#include <stddef.h>
#include <stdint.h>

typedef struct woort_IROp woort_IROp;

typedef uint32_t woort_IRConstantIndex;
typedef uint32_t woort_IRStaticIndex;

typedef enum woort_IRValue_Source
{
    WOORT_IRVALUE_SOURCE_CONSTANT,
    WOORT_IRVALUE_SOURCE_RESULT,
    WOORT_IRVALUE_SOURCE_PHI,
    WOORT_IRVALUE_SOURCE_ARGUMENT,

}woort_IRValue_Source;

#define WOORT_IRVALUE_STACK_NOT_ASSIGN INT32_MAX

typedef struct woort_IRValue {

    woort_IRValue_Source m_source;

    union
    {
        woort_IRConstantIndex m_constant;
        const woort_IROp* m_operate;
        uint32_t m_argument_idx;
    };

    int32_t m_assigned_stack_offset;

} woort_IRValue;

void woort_IRValue_init_constant(woort_IRValue* ir_value, woort_IRConstantIndex idx);
void woort_IRValue_init_operate(woort_IRValue* ir_value, const woort_IROp* op);
void woort_IRValue_init_phi(woort_IRValue* ir_value);
void woort_IRValue_init_argument(woort_IRValue* ir_value, uint32_t argument_idx);