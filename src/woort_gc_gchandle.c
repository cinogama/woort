#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "woomem.h"
#include "woort_gc_gchandle.h"
#include "woort_diagnosis.h"
#include "woort_gc.h"

static void _woort_GCHandle_close_impl(woort_GCHandle* gchandle)
{
    gchandle->m_user_destruct_callback(gchandle->m_user_handle);
    if (gchandle->m_dylib != NULL)
        woort_dylib_unload(gchandle->m_dylib, WOORT_DYLIB_UNREF);
}
static void _woort_GCHandle_marker(woort_GCUnit* unit)
{
    woort_GCHandle* const gchandle = (woort_GCHandle*)unit;

    if (gchandle->m_user_handle != NULL)
        gchandle->m_user_mark_callback(gchandle->m_user_handle);
}
static void _woort_GCHandle_destructor(woort_GCUnit* unit)
{
    woort_GCHandle* const gchandle = (woort_GCHandle*)unit;

    if (gchandle->m_user_handle != NULL)
    {
        _woort_GCHandle_close_impl(gchandle);
    }
}

const woort_GCUnitProxy WOORT_GCHANDLE_UNIT_PROXY = {
    .m_destructor = _woort_GCHandle_destructor,
    .m_marker = _woort_GCHandle_marker,
};

const woort_GCHandle* woort_GCHandle_new(
    void* addr,
    /* OPTIONAL */ woort_Value* holding,
    woort_GCHandle_UserDestructFunction destructor,
    /* OPTIONAL */ woort_Dylib* dylib)
{
    assert(addr != NULL);

    woort_GCHandle* const gchandle = woort_GCUnit_alloc_delay_init(
        sizeof(woort_GCHandle));

    gchandle->m_gc_unit.m_proxy = &WOORT_GCHANDLE_UNIT_PROXY;

    if (holding != NULL)
        woort_GC_init_write_barrier_value(&gchandle->m_hold_value, *holding);
    else
        gchandle->m_hold_value.m_integer = 0;

    gchandle->m_user_destruct_callback = destructor;
    gchandle->m_user_handle = addr;
    if (dylib != NULL)
        woort_dylib_keep(dylib);
    gchandle->m_dylib = dylib;

    woort_GCUnit_init_delay_alloc(AF, gchandle);

    return gchandle;
}

const woort_GCHandle* woort_GCHandle_new_with_marker(
    void* addr,
    woort_GCHandle_UserMarkFunction marker,
    woort_GCHandle_UserDestructFunction destructor,
    /* OPTIONAL */ woort_Dylib* dylib)
{
    assert(addr != NULL);

    woort_GCHandle* const gchandle = woort_GCUnit_alloc_delay_init(
        sizeof(woort_GCHandle));

    gchandle->m_gc_unit.m_proxy = &WOORT_GCHANDLE_UNIT_PROXY;

    gchandle->m_user_mark_callback = marker;
    gchandle->m_user_destruct_callback = destructor;
    gchandle->m_user_handle = addr;
    if (dylib != NULL)
        woort_dylib_keep(dylib);
    gchandle->m_dylib = dylib;

    woort_GCUnit_init_delay_alloc(MF, gchandle);

    return gchandle;
}

WOORT_NODISCARD bool woort_GCHandle_close(woort_GCHandle* gchandle)
{
    if (gchandle->m_user_handle == NULL)
        return false;

    _woort_GCHandle_close_impl(gchandle);
    gchandle->m_user_handle = NULL;

    return true;
}
