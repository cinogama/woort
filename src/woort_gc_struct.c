#include "woomem.h"
#include "woort_gc_struct.h"
#include "woort_codeenv.h"

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

WOORT_NODISCARD woort_GCStruct* woort_GCStruct_new_for_env_constant(
    woort_CodeEnv* cenv, size_t struct_size)
{
    woort_GCStruct* gcstruct;

    do
    {
        gcstruct = woort_GCUnit_alloc_attrib_may_fail(
            A, sizeof(woort_GCStruct) + struct_size * sizeof(woort_Value));

        if (gcstruct != NULL)
            break;

        woort_CodeEnv_unlock(cenv);
        {
            _woort_GCUnit_alloc_failed();
        }
        woort_CodeEnv_lock(cenv);

    } while (true);

    gcstruct->m_gc_unit.m_proxy = &WOORT_GCSTRUCT_UNIT_PROXY;
    gcstruct->m_size = struct_size;

    return gcstruct;
}
