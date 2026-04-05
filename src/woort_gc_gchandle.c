#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "woomem.h"
#include "woort_gc_gchandle.h"
#include "woort_diagnosis.h"

void _woort_GCStruct_marker(woort_GCUnit* unit)
{
    woort_GCHandle* const gcstruct = (woort_GCHandle*)unit;
    gcstruct->m_user_mark_callback(gcstruct->m_user_handle);
}
void _woort_GCHandle_destructor(woort_GCUnit* unit)
{
    woort_GCHandle* const gcstruct = (woort_GCHandle*)unit;
    gcstruct->m_user_destruct_callback(gcstruct->m_user_handle);
}

const woort_GCUnitProxy WOORT_GCHANDLE_UNIT_PROXY = {
    .m_destructor = _woort_GCHandle_destructor,
    .m_marker = _woort_GCStruct_marker,
};

woort_GCHandle* woort_GCHandle_new(
    void* addr,
    /* OPTIONAL */woort_Value* holding,
    woort_GCHandle_UserDestructFunction destructor)
{
    woort_GCHandle* const gchandle = woort_GCUnit_alloc_attrib(
        AF,
        sizeof(woort_GCHandle));

    gchandle->m_gc_unit.m_proxy = &WOORT_GCHANDLE_UNIT_PROXY;

    if (holding != NULL)
        gchandle->m_hold_value = *holding;
    else
        gchandle->m_hold_value.m_integer = 0;

    gchandle->m_user_destruct_callback = destructor;
    gchandle->m_user_handle = addr;

    return gchandle;
}

woort_GCHandle* woort_GCHandle_new_with_marker(
    void* addr,
    woort_GCHandle_UserMarkFunction marker,
    woort_GCHandle_UserDestructFunction destructor)
{
    woort_GCHandle* const gcstruct = woort_GCUnit_alloc_attrib(
        MF,
        sizeof(woort_GCHandle));

    gcstruct->m_gc_unit.m_proxy = &WOORT_GCHANDLE_UNIT_PROXY;

    gcstruct->m_user_mark_callback = marker;
    gcstruct->m_user_destruct_callback = destructor;
    gcstruct->m_user_handle = addr;

    return gcstruct;
}


