#include "woomem.h"
#include "woort_gc_struct.h"

const woort_GCUnitProxy WOORT_GCSTRUCT_UNIT_PROXY = {
    .m_destructor = NULL,
    .m_marker = NULL,
};

woort_GCStruct* woort_GCStruct_new(size_t struct_size)
{
    woort_GCStruct* const gcstruct = woort_GCUnit_alloc_attrib(
        A, 
        sizeof(woort_GCStruct) 
        + struct_size * sizeof(woort_Value));

    gcstruct->m_gc_unit.m_proxy = &WOORT_GCSTRUCT_UNIT_PROXY;
    gcstruct->m_size = struct_size;

    return gcstruct;
}
