#include "woort.h"

#include "woort_gc_struct.h"
#include "woort_codeenv.h"

#include "woort_mem.h"

const woort_GCUnitProxy WOORT_GCSTRUCT_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

WOORT_NODISCARD woort_GCStruct* woort_GCStruct_new(size_t struct_size)
{
    woort_GCStruct* const gcstruct = woort_GCUnit_alloc_delay_init(
        sizeof(woort_GCStruct) 
        + struct_size * sizeof(woort_Value));

    gcstruct->m_gc_unit.m_proxy = &WOORT_GCSTRUCT_UNIT_PROXY;
    gcstruct->m_size = struct_size;

    woort_GCUnit_init_delay_alloc(A, gcstruct);

    return gcstruct;
}

WOORT_NODISCARD woort_GCStruct* woort_GCStruct_new_for_env_constant(
    woort_CodeEnv* cenv, size_t struct_size)
{
    woort_GCStruct* const gcstruct = _woort_GCUnit_alloc_for_env_constant(
        cenv, sizeof(woort_GCStruct) + struct_size * sizeof(woort_Value));

    gcstruct->m_gc_unit.m_proxy = &WOORT_GCSTRUCT_UNIT_PROXY;
    gcstruct->m_size = struct_size;

    woort_GCUnit_init_delay_alloc(A, gcstruct);

    return gcstruct;
}
