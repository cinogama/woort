#pragma once

/*
woort_ir_value.h
*/

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

#define WOORT_IRVALUE_STACK_NOT_ASSIGN

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
